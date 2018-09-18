#ifndef _CLIENT_H_
#define _CLIENT_H_
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <pthread.h>

#define BUFSIZE 1024
#define LED 4

void *send_data(void *arg);
void *recv_data(void *arg);
