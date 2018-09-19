#include <stdlib.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <queue>
#include <sys/stat.h>
#include <mutex>
#include "data.hpp"

#define MAX_CLIENT 1000
#define MAX_EVENTS 1000
#define BUFSIZE 1024
#define FIFO_PERMS (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

class client {
public:
  int client_socket_fd;
  char client_ip[20];
};

void userpool_add(int client_fd, char * client_ip);
void userpool_delete(int client_fd);
void client_receive(int event_fd);
void epoll_init();
void server_init(int port);
void *server_process(void *arg);
void *server_send_data(void *arg);
bool setnonblocking(int fd, bool blocking);

struct epoll_event g_events[MAX_EVENTS];
struct client g_clients[MAX_CLIENT];
int g_epoll_fd, g_server_socket;
bool server_close = true;

std::queue<int> clients_request_queue;
int fd_to_client;
int fd_from_darknet;

std::mutex mtx;    

int main(int argc, char **argv){
  if (argc != 2){
    printf("Usage : %s <port>\n", argv[0]);
    exit(1);
  } else if (atoi(argv[1]) < 1024){
    printf("invalid port number\n");
    exit(1);
  }

  //init g_clients
  for (int i = 0; i < MAX_CLIENT; i ++){
    g_clients[i].client_socket_fd = -1;
  }

  server_init(atoi(argv[1]));
  epoll_init();

  /* At first, run this script!  */

  // if (-1 == (mkfifo("../fifo_pipe/server_send", FIFO_PERMS))) {
  //   perror("mkfifo error: ");
  //   return 1;
  // }
  
  // if (-1 == (mkfifo("../fifo_pipe/darknet_send", FIFO_PERMS))) {
  //   perror("mkfifo error: ");
  //   return 1;
  // } 

  if (-1 == (fd_to_client=open("../fifo_pipe/server_send", O_WRONLY))) {
    perror("server_send open error: ");
    return 1;
  }
  
  if (-1 == (fd_from_darknet=open("../fifo_pipe/darknet_send", O_RDWR))) {
    perror("darknet_send open error: ");
    return 2;
  } 



  pthread_t process_thread,notice_thread, darknet_request_thread;
  void *thread_result;

  pthread_create(&process_thread, NULL, server_process, NULL);
  pthread_create(&notice_thread, NULL, server_send_data, NULL);
  pthread_create(&darknet_request_thread, NULL, server_request_darknet, NULL);

  pthread_join(process_thread, &thread_result);
  pthread_join(notice_thread, &thread_result);
  pthread_join(darknet_request_thread, &thread_result);

  close(g_server_socket);
  return 0;
}

void *server_request_darknet(void *arg) {
  int clinetd_fd;
  char message[BUFSIZE];
  char buf[BUFSIZE];
  while (server_close == false) {
    if (!clients_request_queue.empty()) {
      mtx.lock();
      int client_fd = clients_request_queue.front();
      clients_request_queue.pop();
      mtx.unlock();
      sprintf(message, "../images/%d%s", clinetd_fd, ".jpg");

      if (write(fd_to_client, message, sizeof(message)) < 0) {
        perror("write error: ");
        return 3;
      }

      if (read(fd_from_darknet, buf, BUFSIZE) < 0) {
        perror("read error: ");
        return 4;
      }

      printf("receive from darknet: ../images/%d.jpg result: %s\n", buf);
    }
  }
}


void *server_process(void *arg){
  while (server_close == false){
    struct sockaddr_in client_address;
    int client_length = sizeof(client_address);
    int client_socket;
    int num_fd = epoll_wait(g_epoll_fd, g_events, MAX_EVENTS, 100);
    
    if (num_fd == 0){
      continue; // nothing
    } else if (num_fd < 0){
      printf("epoll wait error : %d\n",num_fd );
      continue;
    }

    for (int i = 0; i < num_fd; i ++){
      if (g_events[i].data.fd == g_server_socket){//first connect time
        client_socket = accept(g_server_socket, (struct sockaddr *) &client_address, (socklen_t *) &client_length);
        if (client_socket < 0){
          printf("accept_error\n");
        } else {
          printf("new client connected\nfd : %d\nip : %s\n", client_socket, inet_ntoa(client_address.sin_addr));
          userpool_add(client_socket, inet_ntoa(client_address.sin_addr));
        }
      } else {//already connected. receive handling
        client_receive(g_events[i].data.fd);
      }
    }
  }
}

// send some data to clients
// 
void *server_send_data(void *arg){
  char buf[BUFSIZE];
  int len;
  int new_expire = 100;
  while(true){
    //if ready
    fgets(buf, BUFSIZE, stdin);
    
    
    memset(buf, 0, BUFSIZE);
    sprintf(buf, "%d", new_expire);

    for (int i = 0; i < MAX_CLIENT; i ++){
      if (g_clients[i].client_socket_fd != -1){
        len = send(g_clients[i].client_socket_fd, buf, strlen(buf), 0);
      }
    }
  }
}

bool setnonblocking(int fd, bool blocking=true){
  if (fd < 0) return false;
       
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) return false;
  flags = blocking ? (flags&~O_NONBLOCK) : (flags|O_NONBLOCK);
  return (fcntl(fd, F_SETFL, flags) == 0) ? true : false;
}

void server_init(int port){
  struct sockaddr_in server_address;

  g_server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (g_server_socket == -1){
    printf("socket() error\n");
    exit(1);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  server_address.sin_port = htons(port);

  int n_socket_opt = 1;
  if( setsockopt(g_server_socket, SOL_SOCKET, SO_REUSEADDR, &n_socket_opt, sizeof(n_socket_opt)) < 0 ){
    printf("Server Start Fails. : Can't set reuse address\n");
    close(g_server_socket);
    exit(1);
  }

  if (bind(g_server_socket, (struct sockaddr *) &server_address, sizeof(server_address)) == -1){
    printf("bind() error\n");
    close(g_server_socket);
    exit(1);
  }

  if (listen(g_server_socket, 15) == -1){
    printf("listen() error\n");
    close(g_server_socket);
    exit(1);
  }

  //setnonblocking(g_server_socket, false);
  printf("server start listening\n");
  server_close = false;
}

void epoll_init(){
  struct epoll_event events;

  g_epoll_fd = epoll_create(MAX_EVENTS);
  if (g_epoll_fd < 0){
    printf("epoll create fails\n");
    close(g_server_socket);
    exit(1);
  }
  printf("epoll created\n");

  events.events = EPOLLIN;
  events.data.fd = g_server_socket;

  if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, g_server_socket, &events) < 0){
    printf("epoll control failed\n");
    close(g_server_socket);
    exit(1);
  }
  printf("epoll set succeded\n");
}

void userpool_add(int client_fd, char * client_ip){
  int i;
  for (i = 0; i < MAX_CLIENT; i ++){
    if (g_clients[i].client_socket_fd == -1) break;
  }
  if (i >= MAX_CLIENT) {
    printf("client is full\n");
   close(client_fd);
  }

  g_clients[i].client_socket_fd = client_fd;
  memset(&g_clients[i].client_ip[0], 0, 20);
  strcpy(&g_clients[i].client_ip[0], client_ip);

  struct epoll_event events;

  events.events = EPOLLIN;
  events.data.fd = client_fd;

  if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, client_fd, &events) < 0){
    printf("epoll control failed for client\n");
  }
}

void userpool_delete(int client_fd){
  int i;
  for (int i = 0; i < MAX_CLIENT; i ++){
    if(g_clients[i].client_socket_fd == client_fd){
      printf("client is deleted\n");
      g_clients[i].client_socket_fd = -1;
      break;
    }
  }
}

// receive from client
void client_receive(int event_fd){
  char buf[BUFSIZE];
  int len;
  
  memset(buf, 0, BUFSIZE);
  len = recv(event_fd, buf, 10, 0);

  if (len <= 0){
    userpool_delete(event_fd);
    close(event_fd);
    return;
  }

  
  int total_size = atoi(buf);
  printf("file size : %d, len : %d\n", total_size, len);

  char fileName[BUFSIZE];
  sprintf(fileName, "../images/%d%s", event_fd, ".jpg");

  printf("trying to %s file open.\n", fileName);    
  int fd = open(fileName, O_WRONLY|O_CREAT|O_TRUNC, 0777);
    
  if (fd == -1){
    printf("file open error\n");
    exit(1);
  }
  int size = (BUFSIZE > total_size) ? total_size : BUFSIZE; 
  while (total_size > 0 && (len = recv(event_fd, buf, size, 0)) > 0) {
    printf("receiving : %d remain : %d\n", len, total_size);
    write(fd, buf, len);
    total_size -= len;
    size = (BUFSIZE > total_size) ? total_size : BUFSIZE; 
  }
  
  printf("done!\n");
  mtx.lock();
  clients_request_queue.push(event_fd);
  mtx.unlock();
}
