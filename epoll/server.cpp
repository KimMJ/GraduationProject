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
#include <map>
#include "data.hpp"
#include <unordered_map>

#define MAX_CLIENT 1000
#define MAX_EVENTS 1000
#define BUFSIZE 1024
#define DEFAULT_EXPIRE 90
#define MAX_EXPIRE 180
#define CLUSTER 4
#define FIFO_PERMS (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

class Client {
public:
  int client_socket_fd;
  char client_ip[20];
  int num_car;
  bool connected;

  Client (void) {
    this->client_socket_fd = -1;
    memset(client_ip, 0, 20);
    this->num_car = -1;
    this->connected = false;
  }

  Client (int client_socket_fd, char *client_ip, int num_car, bool connected) {
    this->client_socket_fd = client_socket_fd;
    strcpy(this->client_ip, client_ip);
    this->num_car = num_car;
    this->connected = connected;
  }
};

void userpool_add(int client_fd, int client_seq, char * client_ip);
void userpool_delete(int client_fd);
void client_receive(int event_seq);
void epoll_init();
void server_init(int port);
void *server_request_darknet(void *arg);
void *server_process(void *arg);
void *server_send_data(void *arg);
bool setnonblocking(int fd, bool blocking);
void response_darknet_init();

struct epoll_event g_events[MAX_EVENTS];
//struct Client g_clients[MAX_CLIENT];
std::unordered_map<int, Client> g_clients;

int g_epoll_fd, g_server_socket;
bool server_close = true;

//int default_expire = DEFAULT_EXPIRE;
int client_expire = DEFAULT_EXPIRE;
// 일단 1초 가정
double response_darknet = 0.0;

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
  // for (int i = 0; i < MAX_CLIENT; i ++){
  //   g_clients[i].client_socket_fd = -1;
  //   g_clients[i].num_car = -1;
  // }

  server_init(atoi(argv[1]));
  epoll_init();

  /* At first, run this script!  */

  printf("if name pipe already exist, remove it.\n");
  system("rm ../fifo_pipe/*");

  
  if (-1 == (mkfifo("../fifo_pipe/server_send.pipe", FIFO_PERMS))) {
    perror("mkfifo error: ");
    return 1;
  }
  
  if (-1 == (mkfifo("../fifo_pipe/darknet_send.pipe", FIFO_PERMS))) {
    perror("mkfifo error: ");
    return 1;
  }


  if (-1 == (fd_to_client=open("../fifo_pipe/server_send.pipe", O_WRONLY))) {
    perror("server_send open error: ");
    return 1;
  }
  
  if (-1 == (fd_from_darknet=open("../fifo_pipe/darknet_send.pipe", O_RDWR))) {
    perror("darknet_send open error: ");
    return 2;
  }

  response_darknet_init();



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
  char message[BUFSIZE];
  char buf[BUFSIZE];
  while (server_close == false) {
    if (!clients_request_queue.empty()) {
      mtx.lock();
      // int client_fd = clients_request_queue.front();
      int client_seq = clients_request_queue.front();
      clients_request_queue.pop();
      int client_fd = g_clients[client_seq].client_socket_fd;

      mtx.unlock();
      memset(message, 0, BUFSIZE);
      sprintf(message, "%05d", client_fd);
      printf("message : %s, sizeof message : %lu\n", message, strlen(message));

      if (write(fd_to_client, message, strlen(message)) < 0) {
        perror("write error: ");
        return (void*) 3;
      }

      if (read(fd_from_darknet, buf, BUFSIZE) < 0) {
        perror("read error: ");
        return (void*) 4;
      }

      g_clients[client_seq].num_car = atoi(buf);
      printf("receive from darknet: ../images/%05d.jpg result: %d\n", client_fd, g_clients[client_seq].num_car);
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

    printf("epoll >>> event occurred\n");
    for (int i = 0; i < num_fd; i ++){
      if (g_events[i].data.fd == g_server_socket){//first connect time
        client_socket = accept(g_server_socket, (struct sockaddr *) &client_address, (socklen_t *) &client_length);
        if (client_socket < 0){
          printf("accept_error\n");
        } else {

          char buf[10];
          memset(buf, 0, 10);

          int len = recv(client_socket, buf, 10, 0);
          printf("Client info >>> road num : %s\n", buf);

          int client_seq = atoi(buf);

          userpool_add(client_socket, client_seq, inet_ntoa(client_address.sin_addr));

          printf("new client connected\nfd : %d\nip : %s\n", client_socket, inet_ntoa(client_address.sin_addr));
        }
      } else {//already connected. receive handling
        //search client_seq
        for (auto it = g_clients.begin(); it != g_clients.end(); ++it) {
          if (it->second.client_socket_fd == g_events[i].data.fd) {
            client_receive(it->first);
          }
        }
        // client_receive(g_events[i].data.fd);
      }
    }
  }
}

// send some data to clients
// doit with cluster
void *server_send_data(void *arg){
  char buf[BUFSIZE];
  int len;
  int num_client = 0;

  while(true){
    // bool noClient = true;
    // int i;

    bool ready_for_img_processing = false;

    for (auto it = g_clients.begin(); it != g_clients.end(); ++it) {
      if (it->second.num_car < 0) {
        ready_for_img_processing = true;
        break;
      }
    }

    if (ready_for_img_processing) {
      continue;
    }

    // all client.num_car set
    //modify from here
    // TODO : add some codes to process traffic signal
    if (client_expire < MAX_EXPIRE) {
      client_expire += 10;
    } else {
      client_expire = DEFAULT_EXPIRE;
    }

    sprintf(buf, "%d", client_expire);

    for (auto it = g_clients.begin(); it != g_clients.end(); it ++) {
      printf("server send data: client_socket_fd: %d, client_expire: %s\n", it->second.client_socket_fd, buf);
      len = send(it->second.client_socket_fd, buf, strlen(buf), 0);
      it->second.num_car = -1;
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

void userpool_add(int client_fd, int client_seq, char * client_ip){// add data in g_clients
  int i;

  Client client(client_fd, client_ip, -1, true);

  std::pair<int, Client> new_client (client_seq, client);
  g_clients.insert(new_client);

  struct epoll_event events;

  events.events = EPOLLIN;
  events.data.fd = client_fd;

  if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, client_fd, &events) < 0){
    printf("epoll control failed for client\n");
  }
}

void userpool_delete(int client_seq){
  g_clients.erase(client_seq);
}

// receive from client
void client_receive(int event_seq){
  char buf[BUFSIZE];
  int len;
  int event_fd = g_clients[event_seq].client_socket_fd;

  memset(buf, 0, BUFSIZE);
  len = recv(event_fd, buf, 10, 0);

  if (len <= 0){
    userpool_delete(event_seq);
    close(event_fd);
    return;
  }


  int total_size = atoi(buf);
  // printf("file size : %d, len : %d\n", total_size, len);

  char fileName[BUFSIZE];
  sprintf(fileName, "../images/%05d%s", event_fd, ".jpg");

  // printf("trying to %s file open.\n", fileName);
  int fd = open(fileName, O_WRONLY|O_CREAT|O_TRUNC, 0777);

  if (fd == -1){
    printf("file open error\n");
    exit(1);
  }
  int size = (BUFSIZE > total_size) ? total_size : BUFSIZE;
  while (total_size > 0 && (len = recv(event_fd, buf, size, 0)) > 0) {
    // printf("receiving : %d remain : %d\n", len, total_size);
    write(fd, buf, len);
    total_size -= len;
    size = (BUFSIZE > total_size) ? total_size : BUFSIZE;
  }

  // printf("done!\n");
  mtx.lock();
  clients_request_queue.push(event_seq);
  printf("server received data from event_seq : %d\n", event_seq);
  mtx.unlock();
}

void response_darknet_init() {
  char message[] = "sample.demo";
  char buf[BUFSIZE];
  std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
  if (write(fd_to_client, message, strlen(message)) < 0) {
    perror("write error: ");
    exit(1);
  }


  if (read(fd_from_darknet, buf, BUFSIZE) < 0) {
    perror("read error: ");
    exit(1);
  }
  std::chrono::duration<double> sec = std::chrono::system_clock::now() - start;
  response_darknet = sec.count();
  printf("%f\n", response_darknet);
}
