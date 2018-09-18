#include "client.hpp"

bool socket_connected = false;

int main(int argc, char **argv){
  int sock;
  struct sockaddr_in server_address;
  pthread_t snd_thread, rcv_thread;
  void *thread_result;

  if (!(argc == 3)){
    printf("Usage : %s <ip> <port> \n", argv[0]);
    exit(1);
  }

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == -1) {
    printf("socket() error\n");
    exit(1);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = inet_addr(argv[1]);
  server_address.sin_port = htons(atoi(argv[2]));

  if (connect(sock, (struct sockaddr *) &server_address, sizeof(server_address)) == -1){
    printf("connect() error\n");
    exit(1);
  }
  socket_connected = true;

  pthread_create(&snd_thread, NULL, send_data, (void *) &sock);
  pthread_create(&rcv_thread, NULL, recv_data, (void *) &sock);

  pthread_join(snd_thread, &thread_result);
  pthread_join(rcv_thread, &thread_result);
  
  close(sock);
  return 0;
}

void *send_data(void *arg){
  int sock = *(int *) arg;
  char data[BUFSIZE];
  int fd;
  while (socket_connected == true){
    fgets(data, BUFSIZE, stdin);
    if (!strcmp(data, "q\n")){
      close(sock);
      exit(0);
    } else if (!strcmp(data, "transfer\n")) {
      fd = open("../output_0.jpg", O_RDONLY);
      if (fd == -1) {
        printf("no file\n");
        exit(1);
      }
      int len = 0;
      while ((len=read(fd, data, BUFSIZE)) != 0) {
        puts(data);
        printf("transferring len : %d\n", len);
        write(sock, data, len);    
      }
    } else {
      sprintf(data, "%s", data);
      write(sock, data, strlen(data));
    }
  }
}

void *recv_data(void *arg){
  int sock = *(int *) arg;
  char data[BUFSIZE];
  int str_len;
  while (true){
    str_len = read(sock, data, BUFSIZE - 1);
    if (str_len == -1){
      return (void *) 1;
    }
    if (str_len == 0){
      printf("socket closed\n");
      socket_connected = false;
      return (void *) 1;
    }
    data[str_len] = 0;
    fputs(data, stdout);
  }
}
