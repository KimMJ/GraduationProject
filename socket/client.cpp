#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>

void error(char * msg){
  perror(msg);
  exit(0);
}

void sendData(int sockfd, int x){
  int n;
  char buffer[32];

  sprintf(buffer, "%d\n", x);
  if ((n = write(sockfd, buffer, strlen(buffer))) < 0){
    error(const_cast<char *>("ERROR writing to socket"));
  }
  buffer[n] = '\0';
}

int getData(int sockfd){
  char buffer[32];
  int n;

  if ((n = read(sockfd, buffer, 31)) < 0){
    error(const_cast<char *>("ERROR reading from socket"));
  }
  buffer[n] = '\0';
  return atoi(buffer);
}

int main(int argc, char *argv[]){
  int sockfd, portno = 51717, n;
  char serverIp[] = "192.168.0.7"; //IP address!!!!!
  struct sockaddr_in serv_addr;
  struct hostent *server;
  char buffer[256];
  int data;

  if (argc < 3){
    printf("contacting %s on port %d\n", serverIp, portno);
  }

  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
    error(const_cast<char *>("ERROR opening socket"));
  }
  
  if ((server = gethostbyname(serverIp)) == NULL){
    error(const_cast<char *>("ERROR, no such host\n"));
  }

  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  bcopy((char *) server->h_addr, (char *) &serv_addr.sin_addr.s_addr, server->h_length);
  serv_addr.sin_port = htons(portno);

  if(connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
    error(const_cast<char *>("ERROR connecting"));
  }

  for (n = 0; n < 10; n ++){
    int k;
    scanf("%d", &k);
    sendData(sockfd, k);
    data = getData(sockfd);
    printf("%d -> %d\n", n, data);
  }
  sendData(sockfd, -2);

  close(sockfd);
  return 0;
}
