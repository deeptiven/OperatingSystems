
#include "gfclient-student.h"


// optional function for cleaup processing.
void gfc_cleanup(gfcrequest_t **gfr){
    free((*gfr));
}

gfcrequest_t *gfc_create(){
    // dummy for now - need to fill this part in
    gfcrequest_t *request = malloc(sizeof(gfcrequest_t));
    return request;

}

size_t gfc_get_bytesreceived(gfcrequest_t **gfr){
    // not yet implemented
    return (*gfr)->bytesreceived;
}

size_t gfc_get_filelen(gfcrequest_t **gfr){
    // not yet implemented
    return (*gfr)->filelen;
}

gfstatus_t gfc_get_status(gfcrequest_t **gfr){
    // not yet implemented
    return (*gfr)->status;
}

void gfc_global_init(){
}

void gfc_global_cleanup(){
}

int gfc_perform(gfcrequest_t **gfr){
    // currently not implemented.  You fill this part in.
  struct addrinfo client_hints, *serverinfo, *list;
  int returnvalue;
  char portchar[6] = "";

  sprintf(portchar, "%d", (*gfr)->port);
  fprintf(stdout, "port -> %s", portchar);

  memset(&client_hints, 0, sizeof(client_hints));
  client_hints.ai_family = AF_UNSPEC;
  client_hints.ai_socktype = SOCK_STREAM;

  returnvalue = getaddrinfo("localhost", portchar, &client_hints, &serverinfo);
  if(returnvalue < 0)
  {
    fprintf(stderr, "Error while executing getaddrinfo");
    exit(1);
  }

  int connected = 0; 
  for(list = serverinfo; list!=NULL; list = list->ai_next)
  {
    fprintf(stdout, "\nFamily -> %d\n", list->ai_family);
    fprintf(stdout, "Socktype -> %d\n", list->ai_socktype);
    fprintf(stdout, "Protocol -> %d\n", list->ai_protocol);
    (*gfr)->socketfd = socket(list->ai_family, list->ai_socktype, list->ai_protocol);
    if((*gfr)->socketfd < 0) 
    {
      fprintf(stderr, "Error while creating socket...\n");
      continue;
    }
    fprintf(stdout, "Client socket created successfullly %d",(*gfr)->socketfd);
    fprintf(stdout, "Address -> %s\n", list->ai_addr->sa_data);
    fprintf(stdout, "Address Len -> %d\n", list->ai_addrlen);
    int connectvar = connect((*gfr)->socketfd, list->ai_addr, list->ai_addrlen);
    if(connectvar < 0)
    {
      fprintf(stdout, "%s\n", strerror(errno));
      fprintf(stderr, "Error occurred while connecting...\n");
      close((*gfr)->socketfd);
      continue;
    }
    connected = 1;
    break;
  }

  if (connected == 0)   {
      fprintf(stderr, "Couldn't connect to the server...\n");
      exit(1); 
  }

  freeaddrinfo(serverinfo);

  char* request = (char*)malloc(BUFSIZE * sizeof(char));
  snprintf(request, BUFSIZE, "GETFILE GET %s\r\n\r\n", (*gfr)->path);

  ssize_t totalSent = 0 ;
  ssize_t requestLength = strlen(request);

    while(totalSent < requestLength) {
        ssize_t sent = send((*gfr)->socketfd , request + totalSent, requestLength - totalSent, 0);
        if(sent < 0) {
            fprintf(stderr, "Request was not sent to server");
            exit(1);
        }
        totalSent = totalSent + sent;
    }

    //shutdown((*gfr)->socketfd, SHUT_WR);

    int headerStatus = process_header(gfr);

    if (headerStatus < 0 || (*gfr)->status != GF_OK)   {
       // close((*gfr)->socketfd);
        return headerStatus;
    }

    fprintf(stdout, "Fetching file contents...\n");
    int contentStatus = fetch_file_contents(gfr);

   //close((*gfr)->socketfd);
   free(request);
    
    return contentStatus;
}

int fetch_file_contents(gfcrequest_t **gfr)   {
    char recv_buffer[BUFSIZE];

    fprintf(stdout, "%ld:%ld\n", (*gfr)->bytesreceived, (*gfr)->filelen);

    while((*gfr)->status == GF_OK && (*gfr)->bytesreceived < (*gfr)->filelen)   {
        fprintf(stdout, "%s:%ld\n", gfc_strstatus(gfc_get_status(gfr)), (*gfr)->bytesreceived);
        ssize_t receivedLength = recv((*gfr)->socketfd, recv_buffer, BUFSIZE, 0);

        fprintf(stdout, "Received Length -> %ld\n", receivedLength);

        if (receivedLength <= 0)    {
            return -1;
        }

        (*gfr)->writefunc(&recv_buffer, receivedLength, (*gfr)->writearg);
        (*gfr)->bytesreceived += receivedLength;
    }

    return 0;
}

int process_header(gfcrequest_t **gfr)  {
    char recv_buffer[BUFSIZE];
    (*gfr)->bytesreceived = 0;
    //check header received from client
    ssize_t receivedLength = recv((*gfr)->socketfd, recv_buffer, BUFSIZE, 0);
    if(receivedLength == 0)
    {
        (*gfr)->status = GF_INVALID;
        (*gfr)->bytesreceived = 0;
        return -1;
    }
    if(receivedLength < 0)
    {
        (*gfr)->status = GF_ERROR;
        return -2;
    }

    //fprintf(stdout, strstr(recv_buffer, "\r\n\r\n"));
    char *token = strtok(recv_buffer, " ");
    int count = 0;
    
    while (token != NULL)   {
        fprintf(stdout, "%s\n", token);
        // Check if GETFILe
        if (count == 0 && strcmp("GETFILE", token) != 0)   {
            (*gfr)->status = GF_INVALID;
            (*gfr)->filelen = 0;
            return -3;
        }

        // Check if OK
        if (count == 1) {
            if (token != NULL && (strcmp("OK", token) != 0)) {
                if (strcmp("FILE_NOT_FOUND\r\n\r\n", token) == 0)    {
                    (*gfr)->status = GF_FILE_NOT_FOUND;
                    (*gfr)->filelen = 0;
                    return -4;
                } else if (strcmp("ERROR\r\n\r\n", token))
                {
                    (*gfr)->status = GF_ERROR;
                    (*gfr)->filelen = 0;
                    return -5;
                } else  {
                    (*gfr)->status = GF_INVALID;
                    (*gfr)->filelen = 0;
                    return -6;
                } 
            }
        } 

        if (count == 2) {
            if( strstr(token, "\r\n\r\n") == NULL)   {
                fprintf(stdout, "Token not found!");
                (*gfr)->status = GF_INVALID;
                (*gfr)->filelen = 0;
                return  -7;
            } else{
                token = strtok(token, "\r\n\r\n");

                if (token == NULL)  {
                    (*gfr)->status = GF_INVALID;
                    (*gfr)->filelen = 0;
                    return  -8;
                }

                int fileLength = atoi(token);
                fprintf(stdout, "FileLenght -> %d\n", fileLength);

                if (fileLength == 0)    {
                    (*gfr)->status = GF_INVALID;
                    (*gfr)->filelen = 0;
                    return  -9;
                } 

                (*gfr)->status = GF_OK;
                (*gfr)->filelen = fileLength;

                // Get initial file content
                token = strtok(NULL, "\r\n\r\n");

                if (token != NULL)  {
                    fprintf(stdout, "Writing %s\n", token);
                    (*gfr)->writefunc(token, strlen(token) + 1, (*gfr)->writearg);
                    (*gfr)->bytesreceived += strlen(token) + 1;
                }

            }
        }

        token = strtok(NULL, " ");
        count += 1;
    }

    return 0;
    

}


void gfc_set_headerarg(gfcrequest_t **gfr, void *headerarg){
    (*gfr)->headerarg = headerarg;
}

void gfc_set_headerfunc(gfcrequest_t **gfr, void (*headerfunc)(void*, size_t, void *)){
    (*gfr)->headerfunc = headerfunc;
}

void gfc_set_path(gfcrequest_t **gfr, const char* path){
    (*gfr)->path = path;
}

void gfc_set_port(gfcrequest_t **gfr, unsigned short port){
    (*gfr)->port = port;
}

void gfc_set_server(gfcrequest_t **gfr, const char* server){
    fprintf(stdout, "Server -> %s\n", server);
    (*gfr)->server = server;
}

void gfc_set_writearg(gfcrequest_t **gfr, void *writearg){
    (*gfr)->writearg = writearg;
}

void gfc_set_writefunc(gfcrequest_t **gfr, void (*writefunc)(void*, size_t, void *)){
    (*gfr)->writefunc = writefunc;
}

const char* gfc_strstatus(gfstatus_t status){
    const char *strstatus = NULL;

    switch (status)
    {
        default: {
            strstatus = "UNKNOWN";
        }
        break;

        case GF_INVALID: {
            strstatus = "INVALID";
        }
        break;

        case GF_FILE_NOT_FOUND: {
            strstatus = "FILE_NOT_FOUND";
        }
        break;

        case GF_ERROR: {
            strstatus = "ERROR";
        }
        break;

        case GF_OK: {
            strstatus = "OK";
        }
        break;
        
    }

    return strstatus;
}

