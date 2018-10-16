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
#include <ctime>
#include "data.hpp"
#include "json/json.h"

#define BUFSIZE 1024
#define LED 4
#define DEFAULT_EXPIRE 5

void client_process(int sock);

using namespace std;
using namespace cv;

#define INPUT "../cctv.avi"
#define OUTPUT_FILENAME "output.jpg"

bool socket_connected = false;
int expire = DEFAULT_EXPIRE;
int cur_timer = 0;
enum Light cur_light = RED;

int main(int argc, char **argv){
  //tmp client_road_info
  unsigned int client_road_info = 1;

  int sock;
  struct sockaddr_in server_address;
  pthread_t snd_thread, rcv_thread, process_thread;
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

  while (connect(sock, (struct sockaddr *) &server_address, sizeof(server_address)) == -1){
    printf("connect() error\n");
  }
  socket_connected = true;
  printf("connect is success!!\n");

  char buf[11];
  memset(buf, 0, BUFSIZE);
  sprintf(buf, "%010u", client_road_info);
  printf("%s\n", buf);

  send(sock, buf, strlen(buf), 0);

  //recv some data (ex. how many roads, light info, expire)

  client_process(sock);
  close(sock);
  return 0;
}

void client_process(int sock) {
  char buf[BUFSIZE];
  int len = -1;
  int fd = -1;
  int mov_msec = 0;
  //clock_t light_start_time = clock();
  //clock_t frame_start_time = clock();

  time_t light_start_time = time(NULL);
  time_t frame_start_time = time(NULL);
  //OPENCV
  int currentFrame = 0;
  VideoCapture vc = VideoCapture(INPUT);

  if (!vc.isOpened()) {
    cerr << "fail to open the video" << endl;
    return ;
  }
  cout << "total video frame : " << vc.get(CV_CAP_PROP_FRAME_COUNT) << endl;

  frame_start_time = time(NULL);

  double fps = vc.get(CV_CAP_PROP_FPS);
  time_t frame_snapshot_time;
  double frame_time;
  bool wait_recv = false;

  while (true) {
    /*
    if (frame_time < 10) {//10sec
      continue;
    }
    */
    if (!wait_recv) {
      frame_snapshot_time = time(NULL);
      frame_time = frame_snapshot_time - frame_start_time;
      //1000msec == 1sec
      frame_start_time = frame_snapshot_time;
      // modify here to move specific time (msec)
      mov_msec += frame_time;
      vc.set(CAP_PROP_POS_MSEC, mov_msec * 1000);

      Mat frame;
      vc >> frame;

      if (frame.empty()) {
        printf("video is end\n");
        return;
      }

      imshow("image", frame);
      waitKey(100);
      imwrite(OUTPUT_FILENAME, frame);


      FILE * f = fopen(OUTPUT_FILENAME, "r");
      fseek(f, 0, SEEK_END);
      unsigned long file_len = (unsigned long) ftell(f);

      fd = open(OUTPUT_FILENAME, O_RDONLY);

      if (fd == -1) {
        printf("fatal error : no file error\n");
        return;
      }

      memset(buf, 0, BUFSIZE);
      sprintf(buf, "%010lu", file_len);
      printf("%s file size : %s\n", OUTPUT_FILENAME, buf);
      send(sock, buf, strlen(buf), 0);

      while ((len = read(fd, buf, BUFSIZE)) != 0) {
        send(sock, buf, len, 0);
      }

      printf("file transfer is done\n");
      wait_recv = true;
    }

    memset(buf, 0, BUFSIZE);

    len = recv(sock, buf, BUFSIZE, MSG_DONTWAIT);//non_blocking

    if (len == 0) {
      //socket is closed.
      printf("socket is closed\n");
      break;
    }
    // if some data received
    else if (len > 0) {
      wait_recv = false;
      printf("new expire : %d\n", atoi(buf));
      expire = atoi(buf);
    }
    //error
    else {
      // case 1 : no data
      if (time(NULL) - light_start_time > expire) {
        if (cur_light == RED) {
          cur_light = GREEN;
          printf("RED to GREEN expire : %d\n", expire);
        } else {
          cur_light = RED;
          printf("GREEN to RED expire : %d\n", expire);
        }
        //cur_timer = 0;
        expire = DEFAULT_EXPIRE;
        //light_start_time = clock();
        light_start_time = time(NULL);
      }
    }
  }
}
