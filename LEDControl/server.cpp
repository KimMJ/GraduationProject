#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define BUFSIZE 100
#define MAX_CLIENT 10

void* clnt_connection(void *arg);
void send_message(char *message, int len, struct client clnt);
void error_handling(char *message);

int clnt_number = 0;
//int clnt_socks[10];
//socklen_t clnt_addr_sizes[10];

struct client{
  int socket;
  socklen_t addr_size;

  client(int socket, socklen_t addr_size){
    this->socket = socket;
    this->addr_size = addr_size;
  }

  client(){
    this->socket = 0;
    this->addr_size = 0;
  }
};
struct client clients[MAX_CLIENT];

pthread_mutex_t mutx;


int main(int argc, char **argv){
  int serv_sock, clnt_sock;
  struct sockaddr_in serv_addr;
  struct sockaddr_in clnt_addr;
  int clnt_addr_size;
  pthread_t thread;
  
  if (argc != 2){
    printf("Usage : %s <port>\n", argv[0]);
    exit(1);
  }

  if (pthread_mutex_init(&mutx, NULL)){
    error_handling(const_cast<char *>("mutex init error"));
  }

  serv_sock = socket(AF_INET, SOCK_STREAM, 0);
  if (serv_sock == -1){
    error_handling(const_cast<char *>("socket() error"));
  }

  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(atoi(argv[1]));

  if (bind(serv_sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1){
    error_handling(const_cast<char *>("bind() error"));
  }

  if (listen(serv_sock, 5) == -1){
    error_handling(const_cast<char *>("listen() error"));
  }

  while(true){
    clnt_addr_size = sizeof(clnt_addr);
    clnt_sock = accept(serv_sock, (struct sockaddr *) &clnt_addr, (socklen_t *) &clnt_addr_size);
    printf("clnt addr size : %d\n", clnt_addr_size); 

    pthread_mutex_lock(&mutx);
    struct client tmp(clnt_sock, clnt_addr_size);
    clients[clnt_number++] = tmp;
    
    pthread_mutex_unlock(&mutx);

    pthread_create(&thread, NULL, clnt_connection, (void *) &clients[clnt_number-1]);
    printf(" IP : %s \n", inet_ntoa(clnt_addr.sin_addr));
  }
  return 0;
} 

void *clnt_connection(void *arg){
  int clnt_sock = ((struct client *)arg)->socket;
  int str_len = 0;
  char message[BUFSIZE];
  int i;

  while((str_len = read(clnt_sock, message, sizeof(message))) != 0){
    struct sockaddr_in peer_addr;
    getpeername(clnt_sock, (struct sockaddr *) &peer_addr, &((struct client *)arg)->addr_size); // must change!!
    send_message(message, str_len, *(struct client *)arg);
    printf("peer name : %s\n", inet_ntoa(peer_addr.sin_addr));
  }

  pthread_mutex_lock(&mutx);
  for (i = 0; i < clnt_number; i ++){
    if (clnt_sock == clients[i].socket){
      for (; i < clnt_number - 1; i ++){
        clients[i] = clients[i+1];
      }
      break;
    }
  }
  clnt_number --;

  pthread_mutex_unlock(&mutx);

  close(clnt_sock);
  return 0;
}

void send_message(char *message, int len, struct client clnt){
  int i;
  pthread_mutex_lock(&mutx);
  struct sockaddr_in sender_addr, rcv_addr;
  getpeername(clnt.socket, (struct sockaddr *) &sender_addr, &clnt.addr_size);
  std::string sender = inet_ntoa(sender_addr.sin_addr);
  for (i = 0; i < clnt_number; i ++){
    getpeername(clients[i].socket, (struct sockaddr *) &rcv_addr, &clients[i].addr_size);
    std::string receiver = inet_ntoa(rcv_addr.sin_addr);
    printf("%s, %s\n", sender.c_str(), receiver.c_str());
    if (sender != receiver){
      write(clients[i].socket, message, len);
    }
  }

  pthread_mutex_unlock(&mutx);
}

void error_handling(char *message){
  fputs(message, stderr);
  fputc('\n', stderr);
  exit(1);
}
