#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>

#include "config.h"
#include "httpreq.h"

/*----------------------------------------------------------
 * Function: Parse_HTTP_Request
 *
 * Purpose:  Reads HTTP request from a socket and fills in a data structure
 *           (see httpreq.h with the request values (method and URI)
 *
 * Parameters:  socket         : the socket to read the request from
 *              request_value  : address of struct to write the values to
 *
 * Returns:  true if successfull, false otherwise
 *
 *-----------------------------------------------------------
 */

bool Parse_HTTP_Request(int socket, struct http_request * request_values) {

  char buffer[MAX_HTTP_REQ_SIZE];
  char request[MAX_HTTP_REQ_SIZE];
  ssize_t recvdBytes;

  // read request
  request[0] = '\0';
  do {
    recvdBytes = recv(socket, buffer, sizeof(buffer), 0);
    if (recvdBytes > 0) {
      strncat(request, buffer, recvdBytes);
    }
  } while (recvdBytes > 0 && (strstr(request, "\r\n\r\n") == NULL));
  printf("received request: %s\n", request);
  
  // parse request 
  char *line, *method;
  char *line_ptr;

  line = strtok_r(request, "\r\n", &line_ptr);

  method = strtok(line, " ");
  request_values->method = malloc (strlen(method) + 1);
  printf("Method is: %s\n", method);
  if (method == NULL)
    return false;
  strcpy(request_values->method, method);
  
  // parse the requested URI
  char * request_URI = strtok(NULL, " ");
  printf("URI is: %s\n", request_URI);
  if (request_URI == NULL)
    return false;
  request_values->URI = malloc (strlen(request_URI)+1);
  strcpy(request_values->URI, request_URI);

  char * version =  strtok(NULL, " ");
  if (version == NULL)
    return false;
  printf("version is: %s\n", version);
    
  // we can ignore headers, so just check that the blank line exists
  if ((strstr(request, "\r\n\r\n") == NULL))
    return true;
  else 
    return false;
}

/*----------------------------------------------------------
 * Function: Is_Valid_Resource
 *
 * Purpose:  Checks if URI is a valid resource
 *
 * Parameters:  URI  : the URI of the requested resource, both absolute
 *                     and relative URIs are accepted
 *
 * Returns:  false : the URI does not refer to a resource on the server
 *           true  : the URI is available on the server
 *
 *-----------------------------------------------------------
 */

bool Is_Valid_Resource(char * URI) {

  char * server_directory, * location;
  char * resource;

  /* set the root server directory */

  if ( (server_directory = (char *) malloc(PATH_MAX)) != NULL)
    getcwd(server_directory, PATH_MAX);

  /* remove http://domain/ from URI */

  resource = strstr(URI, "http://");
  if (resource == NULL) {
    /* no http:// check if first character is /, if not add it */
    if (URI[0] != '/')
      resource = strcat(URI, "/");
    else 
      resource = URI;
  }
  else
    /* if http:// resource must start with '/' */
    resource = strchr(resource, '/');

  if (resource == NULL)
    /* invalid resource format */
    return false;

  /* append root server directory *
   * for example if request is for /images/myphoto.jpg          *
   * and directory for server resources is /var/www/            *
   * then the resource location is /var/www/images/myphoto.jpg  */

  strcat(server_directory, RESOURCE_PATH);
  location = strcat(server_directory, resource);
  printf("server resource location: %s\n", location);

  /* check file access */

  if (!(access(location, R_OK))) {
    puts("access OK\n");
    free(server_directory);
    return true;
  } else {
    puts("access failed\n");
    free(server_directory);
    return false;
  }
}

void Get_Content_Type(const char *resource, char *content_type) {
  const char *extension = strrchr(resource, '.');
  
  if (extension != NULL) {
    if (strcmp(extension, ".html") == 0 || strcmp(extension, ".htm") == 0) {
      strcpy(content_type, "text/html");
    } else if (strcmp(extension, ".txt") == 0) {
      strcpy(content_type, "text/plain");
    } else if (strcmp(extension, ".css") == 0) {
      strcpy(content_type, "text/css");
    } else if (strcmp(extension, ".js") == 0) {
      strcpy(content_type, "application/javascript");
    } else if (strcmp(extension, ".jpg") == 0 || strcmp(extension, ".jpeg") == 0) {
      strcpy(content_type, "image/jpeg");
    } else if (strcmp(extension, ".png") == 0) {
      strcpy(content_type, "image/png");
    } else {
      strcpy(content_type, "application/octet-stream");
    }
  } else {
    strcpy(content_type, "application/octet-stream");
  }
}


/*----------------------------------------------------------
 * Function: Send_Resource
 *
 * Purpose:  Sends the contents of the file referred to in URI on the socket
 *
 * Parameters:  socket  : the socket to send the content on
 *                URI   : the Universal Resource Locator, both absolute and 
 *                        relative URIs are accepted
 *
 * Returns:  void - errors will cause exit with error printed to stderr
 *
 *-----------------------------------------------------------
 */

void Send_Resource(int socket, char *URI, const char *request_method) {

  char * server_directory,  * resource;
  char * location;

  /* set the root server directory */

  if ( (server_directory = (char *) malloc(PATH_MAX)) != NULL)
    getcwd(server_directory, PATH_MAX);

  /* remove http://domain/ from URI */
  resource = strstr(URI, "http://");
  if (resource == NULL) {
    /* no http:// check if first character is /, if not add it */
    if (URI[0] != '/')
      resource = strcat(URI, "/");
    else 
      resource = URI;
  }
  else
    /* if http:// resource must start with '/' */
    resource = strchr(resource, '/');

  /* append root server directory *
   * for example if request is for /images/myphoto.jpg          *
   * and directory for server resources is /var/www/            *
   * then the resource location is /var/www/images/myphoto.jpg  */

  strcat(server_directory, RESOURCE_PATH);
  location = strcat(server_directory, resource);
  /* open file and send contents on socket */

  FILE * file = fopen(location, "r");

  if (file < 0) {
    fprintf(stderr, "Error opening file.\n");
    exit(EXIT_FAILURE);
  }

  char c;
  long sz;
  char content_header[MAX_HEADER_LENGTH + MAX_CONTENT_TYPE_LENGTH];
  char content_type[MAX_CONTENT_TYPE_LENGTH];

  /* get size of file for content_length header */
  fseek(file, 0L, SEEK_END);
  sz = ftell(file);
  rewind(file);

  Get_Content_Type(resource, content_type);
  sprintf(content_header, "Content-Type: %s\r\nContent-Length: %ld\r\n\r\n", content_type, sz);
  printf("Sending headers: %s\n", content_header);
  send(socket, content_header, strlen(content_header), 0);

  if (strcmp(request_method, "GET") == 0) {
    printf("Sending file contents of %s\n", location);
    free(server_directory);

    while ((c = fgetc(file)) != EOF) {
      if (send(socket, &c, 1, 0) < 1) {
        fprintf(stderr, "Error sending file.");
        exit(EXIT_FAILURE);
      }
      printf("%c", c);
    }
    puts("\nfinished reading file\n");
  } else if (strcmp(request_method, "HEAD") == 0) {
    printf("HEAD request, not sending file contents.\n");
  }

  fclose(file);
}

void Send_Error_Response(int socket, int status_code, const char *status_phrase) {
  char response_buffer[MAX_HTTP_RESPONSE_SIZE];
  char error_html_path[PATH_MAX];
  char error_html[MAX_HTTP_RESPONSE_SIZE];

  // Set the path to the error HTML file based on the status code
  sprintf(error_html_path, "%s/%d.html", ERROR_DIR, status_code);

  // Read the contents of the error HTML file
  FILE *file = fopen(error_html_path, "r");
  if (file == NULL) {
    // If the file doesn't exist, use a default error message
    sprintf(error_html, "<html><body><h1>%d %s</h1></body></html>", status_code, status_phrase);
  } else {
    fread(error_html, sizeof(char), MAX_HTTP_RESPONSE_SIZE, file);
    fclose(file);
  }

  // Send the error response headers
  sprintf(response_buffer, "HTTP/1.0 %d %s\r\n", status_code, status_phrase);
  send(socket, response_buffer, strlen(response_buffer), 0);
  sprintf(response_buffer, "Content-Type: text/html\r\nContent-Length: %lu\r\n\r\n", strlen(error_html));
  send(socket, response_buffer, strlen(response_buffer), 0);

  // Send the error HTML body
  send(socket, error_html, strlen(error_html), 0);
}