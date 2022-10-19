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
#include<arpa/inet.h>

/* A buffer large enough to contain the longest allowed string */
#define BUFSIZE 630
#define MESSAGESIZE 16

#define USAGE                                                          \
  "usage:\n"                                                           \
  "  echoclient [options]\n"                                           \
  "options:\n"                                                         \
  "  -s                  Server (Default: localhost)\n"                \
  "  -p                  Port (Default: 30605)\n"                      \
  "  -m                  Message to send to server (Default: \"Hello " \
  "spring.\")\n"                                                       \
  "  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"server", required_argument, NULL, 's'},
    {"port", required_argument, NULL, 'p'},
    {"message", required_argument, NULL, 'm'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};


/* Main ========================================================= */
int main(int argc, char **argv) {
  int option_char = 0;
  char *hostname = "localhost";
  unsigned short portno = 30605;
  char *message = "Hello Spring!!";

  // Parse and set command line arguments
  while ((option_char =
              getopt_long(argc, argv, "s:p:m:hx", gLongOptions, NULL)) != -1) {
    switch (option_char) {
      case 's':  // server
        hostname = optarg;
        break;
      case 'p':  // listen-port
        portno = atoi(optarg);
        break;
      default:
        fprintf(stderr, "%s", USAGE);
        exit(1);
      case 'm':  // message
        message = optarg;
        break;
      case 'h':  // help
        fprintf(stdout, "%s", USAGE);
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

  if (NULL == message) {
    fprintf(stderr, "%s @ %d: invalid message\n", __FILE__, __LINE__);
    exit(1);
  }

  if (NULL == hostname) {
    fprintf(stderr, "%s @ %d: invalid host name\n", __FILE__, __LINE__);
    exit(1);
  }

  /* Socket Code Here */
  
  int sockfd;
  char buffer[MESSAGESIZE];
  struct addrinfo client_hints, *serverinfo, *list;
  int returnvalue;
  char portchar[6];

  /*Converting port number to a char array. 
  The size is 6 as the highest port number is 65535 (5 characters) + 1 for the null terminator. */
  snprintf(portchar, 6, "%d", portno);

  /*Assigning values to hints and passing it to getddrinfo function*/
  memset(&client_hints, 0, sizeof(client_hints));
  client_hints.ai_family = AF_UNSPEC;
  client_hints.ai_socktype = SOCK_STREAM;

  /*The serverinfo will have the list of the results returned.*/
  returnvalue = getaddrinfo(hostname, portchar, &client_hints, &serverinfo);
  if(returnvalue < 0)
  {
    fprintf(stderr, "Error while executing getaddrinfo");
    exit(1);
  }

  /*Looping through all the elements in the list until we are able to connect to one. 
  Break from the loop once connected succesfully.*/
  for(list = serverinfo; list!=NULL; list = list->ai_next)
  {
    sockfd = socket(list->ai_family, list->ai_socktype, list->ai_protocol);
    if(sockfd < 0) 
    {
      fprintf(stderr, "Error while creating socket");
      continue;
    }

    int connectvar = connect(sockfd, list->ai_addr, list->ai_addrlen);
    if(connectvar< 0)
    {
      fprintf(stderr, "Error occurred while connecting ");
      printf("%d", connectvar);
      close(sockfd);
      continue;
    }
    break;
  }

  if(list == NULL) {
    fprintf(stderr, "Couldn't connect to server");
    exit(1);
  }

  freeaddrinfo(serverinfo);

  /*once connected to the server, we send the message using the send function.*/
  if(send(sockfd , message, strlen(message), 0) < 0)
  {
    fprintf(stderr, "Error occurred in client while sending message");
    close(sockfd);
    exit(1);
  }

  memset(buffer, 0, MESSAGESIZE); 

  /*Once we get the reply back from the server, we receive it using recv function. 
  The message will be stored in buffer.*/
  if(recv(sockfd, buffer, MESSAGESIZE-1, 0) < 0)
  {
    fprintf(stderr, "Error occurred while client was reading server response.");
    close(sockfd);
    exit(1);
  }
  else 
  {
    /*If the return value of recv function is greater than 0, we know that we have received the message. 
    Here, we print the received message.*/
    fprintf(stdout, "%s", buffer);
  }
  close(sockfd);
  return 0;
}



  


  




  

