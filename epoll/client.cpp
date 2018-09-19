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

#define BUFSIZE 1024
#define LED 4
#define DEFAULT_EXPIRE 5

void *send_data(void *arg);
void *recv_data(void *arg);
void *client_process(void *arg);

using namespace std;
using namespace cv;

#define INPUT "../cctv.avi"
#define OUTPUT_FILENAME "output.jpg"

bool socket_connected = false;
int expire = DEFAULT_EXPIRE;
int cur_timer = 0;
enum Light cur_light = RED;

int main(int argc, char **argv){
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

  if (connect(sock, (struct sockaddr *) &server_address, sizeof(server_address)) == -1){
    printf("connect() error\n");
    exit(1);
  }
  socket_connected = true;

  //pthread_create(&snd_thread, NULL, send_data, (void *) &sock);
  //pthread_create(&rcv_thread, NULL, recv_data, (void *) &sock);
  pthread_create(&process_thread, NULL, client_process, (void *) &sock);

  //pthread_join(snd_thread, &thread_result);
  //pthread_join(rcv_thread, &thread_result);
  pthread_join(process_thread, &thread_result);
  
  close(sock);
  return 0;
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

      //notice transfer
      memset(data, 0, BUFSIZE);
      sprintf(data, "%010lu", file_len);
      printf("file size : %s\n", data);
      send(sock, data, strlen(data), 0);

      int len = 0;
      while ((len=read(fd, data, BUFSIZE)) != 0) {
        //puts(data);
        //printf("transferring len : %d\n", len);
        send(sock, data, len, 0);    
      }
      printf("done!\n");
    } 
    /* //chat
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
  int len = 0;

  while (true) {
    memset(data, 0, BUFSIZE);

    len = recv(sock, data, BUFSIZE, MSG_DONTWAIT);

    if (len == 0) {
      //closed
      printf("socket closed\n");
      socket_connected = false;
      return (void *) 1;
    }
    // if some data received
    else if (len > 0) {
      printf("new expire : %d\n", atoi(data));
      expire = atoi(data);
    }
    //error
    else {
      //case 1 : no data
      if (expire > cur_timer) {
        sleep(1);
        cur_timer ++; // TODO : change to use time()
      } else {
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
  }
  return (void*) 1;
}

void *client_process(void *arg) {
  int sock = *(int*) arg;
  char buf[BUFSIZE];
  int len = -1;
  int fd = -1;
  int mov_msec = 0;
  clock_t light_start_time = clock();
  clock_t frame_start_time = clock();

  //OPENCV
  int currentFrame = 0;
  VideoCapture vc = VideoCapture(INPUT);

  if (!vc.isOpened()) {
    cerr << "fail to open the video" << endl;
    return (void*) EXIT_FAILURE;
  }
    cout << "total video frame : " << vc.get(CV_CAP_PROP_FRAME_COUNT) << endl;

  frame_start_time = clock();
  double fps = vc.get(CV_CAP_PROP_FPS);
  clock_t frame_snapshot_time;
  clock_t frame_time;
  
  while (socket_connected == true) {
    frame_snapshot_time = clock();
    frame_time = frame_snapshot_time - frame_start_time;
    
    if (frame_time< 1000) {//10sec
      continue;
    }

    frame_start_time = frame_snapshot_time;
    // modify here to move specific time (msec)
    mov_msec += frame_time;
    vc.set(CAP_PROP_POS_MSEC, mov_msec);

    Mat frame;
    vc >> frame;

    if (frame.empty()) {
      printf("video is end\n");
      exit(0);
    }
    
    imshow("image", frame);
    imwrite(OUTPUT_FILENAME, frame);

    FILE * f = fopen(OUTPUT_FILENAME, "r");
    fseek(f, 0, SEEK_END);
    unsigned long file_len = (unsigned long) ftell(f);

    fd = open(OUTPUT_FILENAME, O_RDONLY);

    if (fd == -1) {
      printf("fatal error : no file error\n");
      exit(1);
    }

    memset(buf, 0, BUFSIZE);
    sprintf(buf, "%010lu", file_len);
    printf("%s file size : %s\n", OUTPUT_FILENAME, buf);
    send(sock, buf, strlen(buf), 0);

    while ((len = read(fd, buf, BUFSIZE)) != 0) {
      send(sock, buf, len, 0);
    }

    printf("file transfer is done\n");

    memset(buf, 0, BUFSIZE);

    len = recv(sock, buf, BUFSIZE, MSG_DONTWAIT);//non_blocking

    if (len == 0) {
      //socket is closed.
      printf("socket is closed\n");
      socket_connected = false;
      break;
    }
    // if some data received
    else if (len > 0) {
      printf("new expire : %d\n", atoi(buf));
      expire = atoi(buf);
    }
    //error
    else {
      // case 1 : no data
      // time check
      
      /*
      if (clock() - start_time >= expire) {
        sleep(1);
        cur_timer ++;
      }
      */

      if (clock() - light_start_time < expire) {
        if (cur_light == RED) {
          printf("RED to GREEN\n");
        } else {
          cur_light = RED;
          printf("GREEN to RED\n");
        }
        //cur_timer = 0;
        expire = DEFAULT_EXPIRE;
        light_start_time = clock();
      }
    }
  }
}
