#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <wiringPi.h>

//#define NAMESIZE 20
#define BUFSIZE 100
#define LED 4 // BCM_GPIO 23

void *send_message(void *arg);
void *recv_message(void *arg);
void *led_react(void *arg);
void error_handling(char *message);

//char name[NAMESIZE] = "[Default]";
char message[BUFSIZE];
bool ledLight = false;

int main(int argc, char **argv){
  int sock;
  struct sockaddr_in serv_addr;
  pthread_t snd_thread, rcv_thread, led_thread;
  void *thread_result;

  if (!(argc == 3)){
    printf("Usage : %s <ip> <port> \n", argv[0]);
    exit(1);
  }

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == -1){
    error_handling(const_cast<char *>("socket() error"));
  }

  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
  serv_addr.sin_port = htons(atoi(argv[2]));

  if (connect(sock, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1){
    error_handling(const_cast<char *>("connect() error"));
  }

  pthread_create(&snd_thread, NULL, send_message, (void *) &sock);
  pthread_create(&rcv_thread, NULL, recv_message, (void *) &sock);


  //setup
  if (wiringPiSetup() == -1){
    return 1;
  }

  pinMode(LED, OUTPUT);

  pthread_create(&led_thread, NULL, led_react, NULL);

  pthread_join(snd_thread, &thread_result);
  pthread_join(rcv_thread, &thread_result);
  pthread_join(led_thread, &thread_result);

  close(sock);
  return 0;
}

void *send_message(void *arg){
  int sock = *(int *) arg;
  char message[BUFSIZE];
  while (true){
    fgets(message, BUFSIZE, stdin);
    if (!strcmp(message, "q\n")){
      close(sock);
      exit(0);
    }
    sprintf(message, "%s", message);
    write(sock, message, strlen(message));
  }
}

void *recv_message(void *arg){
  int sock = *(int *) arg;
  char message[BUFSIZE];
  int str_len;
  while (true){
    str_len = read(sock, message, BUFSIZE - 1);
    if (str_len == -1){
      return (void *)1;
    }
    message[str_len] = 0;
    fputs(message,stdout);
    if (strcmp(message, "turn off\n") == 0){
      ledLight = false;
    } else if (strcmp(message, "turn on\n") == 0){
      ledLight = true;
    }
  }
}

void *led_react(void *arg){
  while (true){
    if (ledLight == false){
      digitalWrite(LED, 0);
    } else {
      digitalWrite(LED, 1);
    }
  }
}

void error_handling(char *message){
  fputs(message, stderr);
  fputc('\n', stderr);
}
