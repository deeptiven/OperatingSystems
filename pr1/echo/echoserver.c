#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUFSIZE 630
#define MESSAGESIZE 16


#define USAGE                                                        \
  "usage:\n"                                                         \
  "  echoserver [options]\n"                                         \
  "options:\n"                                                       \
  "  -p                  Port (Default: 30605)\n"                    \
  "  -m                  Maximum pending connections (default: 1)\n" \
  "  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"port", required_argument, NULL, 'p'},
    {"maxnpending", required_argument, NULL, 'm'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};

int main(int argc, char **argv) {
  int option_char;
  int portno = 30605; /* port to listen on */
  int maxnpending = 1;

  // Parse and set command line arguments
  while ((option_char =
              getopt_long(argc, argv, "p:m:hx", gLongOptions, NULL)) != -1) {
    switch (option_char) {
      case 'p':  // listen-port
        portno = atoi(optarg);
        break;
      default:
        fprintf(stderr, "%s ", USAGE);
        exit(1);
      case 'm':  // server
        maxnpending = atoi(optarg);
        break;
      case 'h':  // help
        fprintf(stdout, "%s ", USAGE);
        exit(0);
        break;
    }
  }

  setbuf(stdout, NULL);  // disable buffering

  if ((portno < 1025) || (portno > 65535)) {
    fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__,
            portno);
    exit(1);
  }
  if (maxnpending < 1) {
    fprintf(stderr, "%s @ %d: invalid pending count (%d)\n", __FILE__, __LINE__,
            maxnpending);
    exit(1);
  }

int sockfd, clientfd, n;
struct sockaddr_storage client_addr;
struct addrinfo serverHints, *serverinfo, *resultlist;
char buffer[MESSAGESIZE];
int returnvalue;
int reuseaddr = 1;
socklen_t sizeofcliaddr;
char *host ="LOCALHOST";

/*Converting port number to a char array. 
  The size is 6 as the highest port number is 65535 (5 characters) + 1 for the null terminator. */
char portchar[6];
snprintf(portchar, 6, "%d", portno);

/*Assigning values to hints and passing it to getddrinfo function*/
memset(&serverHints, 0, sizeof(serverHints)); 
serverHints.ai_family = AF_UNSPEC;
serverHints.ai_socktype = SOCK_STREAM;
serverHints.ai_flags = AI_PASSIVE;

  /*The serverinfo will have the list of the results returned.*/
returnvalue = getaddrinfo(host, portchar, &serverHints, &serverinfo);
if(returnvalue < 0) {
  fprintf(stderr, "There was an error while executing getaddrinfo function.");
}

/*Looping through all the elements in the list until we are able to connect to one. 
  Break from the loop once connected succesfully.*/
for(resultlist = serverinfo ; resultlist!=NULL; resultlist=resultlist->ai_next)
{
  /*Creating a socket. If the function fails (returns <0), we try with the next element in the list.*/
  sockfd = socket(resultlist->ai_family, resultlist->ai_socktype, resultlist->ai_protocol);
  if(sockfd<0) {
    fprintf(stderr, "Error while creating socket");
    continue;
  }

  /*Setting socket options. SO_REUSEADDR will enusre reuse of local addresses.*/
  if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr)) < 0) 
    fprintf(stderr, "Error occurred while executing setsockopt.");

  /*Once we create a socket, we bind it.*/
  if(bind(sockfd, resultlist->ai_addr, resultlist->ai_addrlen) < 0)
  {
    fprintf(stderr, "Error while binding socket");
    
    continue;
  }
  break;
}

freeaddrinfo(serverinfo);

/*Once socket is created , start listening to requests.*/
if(listen(sockfd, maxnpending) < 0)
{
  fprintf(stderr, "Error while listening for connection requests");
}
else 
{
  fprintf(stdout, "Serever listening for connections.");
}

while(1) 
{
  /*Accept a connection. This will return socket fd of the client.*/
  sizeofcliaddr = sizeof(client_addr);
  clientfd = accept(sockfd, (struct sockaddr *)&client_addr, &sizeofcliaddr);
  if(clientfd < 0) {
    fprintf(stderr, "Error while accepting connection.");
    continue;
  }
   
  memset(buffer, 0, MESSAGESIZE); 

  /*Receive the message from the client. This will be put in the buffer.*/
  n = recv(clientfd, buffer, MESSAGESIZE-1, 0);
  if(n == 0) {
    fprintf(stderr, "Nothing was read by the server.");
  }
  else 
  {
    fprintf(stdout, "%s", buffer);
  }

  /*Send the message back to the client using the send function.*/
  if(send(clientfd, buffer, MESSAGESIZE-1 , 0) < 0)
  {
    fprintf(stderr, "There was an error while writing back to socket");
  }
  else 
  {
    fprintf(stdout, "The server sent the message back to client");
  }
}
close(sockfd);
close(clientfd);
return 0;
}
