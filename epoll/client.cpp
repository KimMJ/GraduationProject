#include <opencv/cv.hpp>
#include <unistd.h>
#include <string>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <pthread.h>
#include "data.hpp"

#define BUFSIZE 1024
#define LED 4
#define DEFAULT_EXPIRE 5

void *send_data(void *arg);
void *recv_data(void *arg);
void *alarm_timer(void *arg);
using namespace std;
using namespace cv;

#define INPUT "../cctv.avi"
#define OUTPUT_PREFIX "output"
#define OUTPUT_POSTFIX ".jpg"

bool socket_connected = false;
int expire = DEFAULT_EXPIRE;
int cur_timer = 0;
enum Light cur_light = RED;

int main(int argc, char **argv){
  int sock;
  struct sockaddr_in server_address;
  pthread_t snd_thread, rcv_thread, alarm_thread;
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
  pthread_create(&alarm_thread, NULL, alarm_timer, (void *) &sock);

  pthread_join(snd_thread, &thread_result);
  pthread_join(rcv_thread, &thread_result);
  pthread_join(alarm_thread, &thread_result);
  
  close(sock);
  return 0;
}

void *alarm_timer(void *arg) {
  while (true) {
    while (expire > cur_timer){
      sleep(1);
      cur_timer ++;
    }
    if (cur_light == RED) {
      cur_light = GREEN;
      printf("RED to GREEN\n");
    } else {
      cur_light = RED;
      printf("GREEN to RED\n");
    }
    cur_timer = 0;
    expire = DEFAULT_EXPIRE;
  }
}

void *send_data(void *arg){
  int sock = *(int *) arg;
  char data[BUFSIZE] = {0};
  int fd;

  // OPENCV
  int iCurrentFrame = 0;
  VideoCapture vc = VideoCapture(INPUT);
  if (!vc.isOpened()) {
      cerr << "fail to open the video" << endl;
      return (void*)EXIT_FAILURE;
  }

  while (socket_connected == true) {
    double fps = vc.get(CV_CAP_PROP_FPS);
    cout << "total : " << vc.get(CV_CAP_PROP_FRAME_COUNT) << endl;

    while (true) {
        //vc.set(CV_CAP_PROP_POS_FRAMES, iCurrentFrame);
        int posFrame = vc.get(CV_CAP_PROP_POS_FRAMES);

        Mat frame;
        vc >> frame;

        if (frame.empty()) {
          exit(0);
            return (void*) 0;
        }

        if (posFrame % 100 == 0) { // every 500 frames
          imshow("image", frame);
          stringstream ss;
          ss << "";
          //string filename = OUTPUT_PREFIX + ss.str() + OUTPUT_POSTFIX;
          string filename = "output.jpg";
          imwrite(filename.c_str(), frame);
          break;
        }
        waitKey(1);
    }
    // END OF OPENCV

    //fgets(data, BUFSIZE, stdin);

    if (!strcmp(data, "q\n")){
      close(sock);
      exit(0);
    } else {
      FILE *f;
      f = fopen("output.jpg" , "r");
      fseek(f, 0, SEEK_END);
      unsigned long file_len = (unsigned long)ftell(f);

      fd = open("output.jpg", O_RDONLY);
      
      if (fd == -1) {
        printf("no file\n");
        exit(1);
      }

      //scp
      /*
      int ret = system("scp output.jpg kimmj@10.78.152.63:~/raspberryPi/epoll/output3.jpg");
      if (ret != 0) {
        printf("some error occured!\n");
      }
      */
      //notice transfer
      memset(data, 0, BUFSIZE);
      sprintf(data, "%010lu", file_len);
      printf("file size : %s\n", data);
      send(sock, data, strlen(data), 0);

      int len = 0;
      while ((len=read(fd, data, BUFSIZE)) != 0) {
        //puts(data);
        printf("transferring len : %d\n", len);
        send(sock, data, len, 0);    
      }
      printf("done!\n");
    } 
    /*
    else {
      sprintf(data, "%s", data);
      write(sock, data, strlen(data));
    }
    */
  }
}

void *recv_data(void *arg){
  int sock = *(int *) arg;
  char data[BUFSIZE];
  int str_len;
  while (true){
    memset(data, 0, BUFSIZE);

    str_len = recv(sock, data, BUFSIZE, 0);
    
    if (str_len == -1){
      return (void *) 1;
    }
    
    if (str_len == 0){
      printf("socket closed\n");
      socket_connected = false;
      return (void *) 1;
    }
    printf("new expire : %d\n", atoi(data));
    expire = atoi(data);
  }
}
