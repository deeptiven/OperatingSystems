
#include "gfserver-student.h"
#include <stdio.h>

#define BUFSIZE 64000 

/* 
 * Modify this file to implement the interface specified in
 * gfserver.h.
 */

char* is_header_valid(char* header) 
{
    char* splitheader = strtok(header, " ");
    if(strcmp(splitheader, "GETFILE") != 0) {
        //return -1;
        return NULL;
    }
    
    splitheader = strtok(NULL , " ");
    if(strcmp(splitheader, "GET") != 0) {
        //return -1;
        return NULL;
    }

    splitheader = strtok(NULL, "\r\n\r\n");
    
    /*if(splitheader == NULL) {
        return -1;
    }*/
    /*else {
        (*(*gfs)->ctx)->filePath = splitheader;
        return 0;
    }*/
    fprintf(stdout, "%s\n", splitheader);
    return splitheader;
}

/*char* getpath(char *header) 
{
  char* splitheader = strtok(header, " ");
    if(strcmp(splitheader, "GETFILE") != 0) {
        return NULL;
    }
    
    splitheader = strtok(NULL , " ");
    if(strcmp(splitheader, "GET") != 0) {
        return NULL;
    }

    splitheader = strtok(NULL, "\r\n\r\n");
    
    if(splitheader == NULL) {
        return NULL;
    }
    fprintf(stdout, "%s\n", splitheader);
    return splitheader;
}*/

void gfs_abort(gfcontext_t **ctx){
  close((*ctx)->clientsocketfd);
  free(ctx);
}

gfserver_t* gfserver_create(){
    gfserver_t *gf_server = malloc(sizeof(gfserver_t));
    bzero(gf_server, sizeof(gfserver_t));

    //gf_server->ctx = malloc(sizeof(gfcontext_t));
    //gf_server->ctx = malloc(sizeof(gfcontext_t));
    return gf_server;
}

ssize_t gfs_send(gfcontext_t **ctx, const void *data, size_t len){
    int sock_fd = (*ctx)->clientsocketfd;
    fprintf(stdout, "Sending file contents...\n");

    ssize_t sent = send_file_contents(sock_fd, data, len);

    fprintf(stdout, "%ld bytes of %ld bytes sent", sent, len);

    return sent;
    
}

ssize_t send_file_contents(int sock_fd, const void *data, size_t len) {
    ssize_t totalsent = 0;
    //ssize_t remainining = len - totalsent;
    fprintf(stdout, "Sending data...\n");
    while(totalsent < len) {
        ssize_t sent = send(sock_fd, data+totalsent, len - totalsent, 0);
        if(sent < 0) {
            fprintf(stderr, "Error while sending data");
            exit(1);
        }
        totalsent = totalsent + sent;
    }
    return totalsent;
}

ssize_t gfs_sendheader(gfcontext_t **ctx, gfstatus_t status, size_t file_len){
    char *header = malloc(100 * sizeof(char));
    char *marker = malloc(5*sizeof(char));
    ssize_t marker_len = sprintf(marker, "\r\n\r\n");
    fprintf(stdout, "%s len -> %ld\n", marker, marker_len);
    ssize_t len = 0;
    fprintf(stdout, "Sending header...\n");
    if(status == GF_OK) 
    {
        len = sprintf(header, "GETFILE OK %zu%s", file_len, marker);
    } 
    else if(status == GF_FILE_NOT_FOUND)
    {
        len = sprintf(header, "GETFILE FILE_NOT_FOUND%s", marker);
    } 
    else if(status == GF_INVALID)
    {
        len = sprintf(header, "GETFILE INVALID %u%s", 0, marker);
    }
    else 
    {
        len = sprintf(header, "GETFILE ERROR%s", marker);
    }


    len += 1; // sprintf's return value doesn't include the null at the end
    fprintf(stdout, "Length -> %ld;Header -> %s\n", len, header);
    int send_status = send_file_contents((*ctx)->clientsocketfd, header, len);
    fprintf(stdout, "Sent -> %d\n", send_status);

    if (send_status == -1)   {
      fprintf(stdout, "Error sending header\n");
      return -1;
    }

    free(header);
    free(marker);

    return len;
}

void gfserver_serve(gfserver_t **gfs){
int n;
struct sockaddr_storage client_addr;
struct addrinfo serverHints, *serverinfo, *resultlist;
int returnvalue;
int reuseaddr = 1;
socklen_t sizeofcliaddr;

char portchar[6] = "";

sprintf(portchar, "%d", (*gfs)->port);

memset(&serverHints, 0, sizeof(serverHints)); 
serverHints.ai_family = AF_UNSPEC;
serverHints.ai_socktype = SOCK_STREAM;
serverHints.ai_flags = AI_PASSIVE;

returnvalue = getaddrinfo(NULL, portchar, &serverHints, &serverinfo);
if(returnvalue < 0) {
  fprintf(stderr, "There was an error while executing getaddrinfo function.");
}

for(resultlist = serverinfo ; resultlist!=NULL; resultlist=resultlist->ai_next)
{
  (*gfs)->socketfd = socket(resultlist->ai_family, resultlist->ai_socktype, resultlist->ai_protocol);
  if((*gfs)->socketfd<0) {
    fprintf(stderr, "Error while creating socket");
    continue;
  }

  if(setsockopt((*gfs)->socketfd, SOL_SOCKET, SO_REUSEADDR , &reuseaddr, sizeof(reuseaddr)) < 0) 
    fprintf(stderr, "Error occurred while executing setsockopt.");

  /*if(setsockopt((*gfs)->socketfd, SOL_SOCKET, SO_REUSEPORT , &reuseaddr, sizeof(reuseaddr)) < 0) 
    fprintf(stderr, "Error occurred while executing setsockopt.");  */

  if(bind((*gfs)->socketfd, resultlist->ai_addr, resultlist->ai_addrlen) < 0)
  {
    fprintf(stderr, "Error while binding socket");
    close((*gfs)->socketfd);
    continue;
  }
  break;
}
fprintf(stdout, "sockert created %d", (*gfs)->socketfd);

freeaddrinfo(serverinfo);

//gfserver_set_maxpending(gfs, 5);

if(listen((*gfs)->socketfd, 5) < 0)
{
  fprintf(stderr, "Error while listening for connection requests");
}
else 
{
  fprintf(stdout, "Serever listening for connections.");
}

while(1) 
{
  sizeofcliaddr = sizeof(client_addr);
  
  //(*(*gfs)->ctx)->clientsocketfd = accept((*gfs)->socketfd, (struct sockaddr *)&client_addr, &sizeofcliaddr);
  int clientsockfd = accept((*gfs)->socketfd, (struct sockaddr *)&client_addr, &sizeofcliaddr);
  //(*(*gfs)->ctx) = malloc(sizeof(struct gfcontext_t));
  //(*(*gfs)->ctx)->clientsocketfd = clientSocketFd;
  //int clientsockfd = accept((*gfs)->socketfd, (struct sockaddr *)&client_addr, &sizeofcliaddr);
  struct gfcontext_t *context = malloc(sizeof(struct gfcontext_t));
  context->clientsocketfd = clientsockfd;
  if (context->clientsocketfd < 0){
    fprintf(stderr, "Error while accepting connection.");
    continue;
  }
  fprintf(stdout, "accepted client sockfd = %d\n", context->clientsocketfd);
  char buffer[BUFSIZE];
  //n = recv((*(*gfs)->ctx)->clientsocketfd, buffer, BUFSIZE-1, 0);
  n = recv(context->clientsocketfd, buffer, BUFSIZE-1, 0);

  if(n == 0) {
    fprintf(stderr, "Nothing was read by the server.");
  }
  else 
  {
    fprintf(stdout, "The header received from client : %s", buffer);
  }

  //int is_valid = is_header_valid(buffer);
  char *filepath = is_header_valid(buffer);
  //char *filepath = getpath(buffer);
  fprintf(stdout, "Filepath -> %s\n", filepath);
  if(filepath != NULL) {
      (*gfs)->handler(&context, filepath, (*gfs)->arg);
      //(*gfs)->handler(&((*gfs)->ctx), filepath, (*gfs)->arg);
  } else {
      gfs_sendheader(&context, GF_FILE_NOT_FOUND, 0);
      //gfs_sendheader(&((*gfs)->ctx), GF_FILE_NOT_FOUND, 0);
  }
  free(context);
}

}

void gfserver_set_handlerarg(gfserver_t **gfs, void* arg){
    (*gfs)->arg = arg;
}

void gfserver_set_handler(gfserver_t **gfs, gfh_error_t (*handler)(gfcontext_t **, const char *, void*)){
    (*gfs)->handler = handler;
}

void gfserver_set_maxpending(gfserver_t **gfs, int max_npending){
    (*gfs)->max_npending = max_npending;
}

void gfserver_set_port(gfserver_t **gfs, unsigned short port){
    (*gfs)->port = port;
}



