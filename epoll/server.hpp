#ifndef _SERVER_H_
#define _SERVER_H_
#endif

#include <stdlib.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <queue.h>

#define MAX_CLIENT 1000
#define MAX_EVENTS 1000
#define BUFSIZE 1024

struct client {
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
