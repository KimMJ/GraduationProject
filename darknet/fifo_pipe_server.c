#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#define BUFSIZE 30
#define FIFO_PERMS (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

int main() {
	char message[BUFSIZE];
	char buf[BUFSIZE];
	int fd_to_client;
	int fd_from_darknet;

	if (-1 == (mkfifo("server_send", FIFO_PERMS))) {
		perror("mkfifo error: ");
		return 1;
	}
	
	if (-1 == (mkfifo("client_send", FIFO_PERMS))) {
		perror("mkfifo error: ");
		return 1;
	} 

	if (-1 == (fd_to_client=open("server_send", O_WRONLY))) {
		perror("open error: ");
		return 2;
	}
	
	if (-1 == (fd_from_darknet=open("darknet_send", O_RDWR))) {
		perror("open error: ");
		return 2;
	} 

	while(1) {
		memset(message, 0, BUFSIZE);
		memset(buf, 0, BUFSIZE);

		scanf("%s", message);

		printf("send to darknet: %s\n", message);

		if (write(fd_to_client, message, sizeof(message)) < 0) {
			perror("write error: ");
			return 3;
		}

		if (read(fd_from_darknet, buf, BUFSIZE) < 0) {
			perror("read error: ");
			return 4;
		}

		printf("receive from darknet: %s\n", buf);
		
	}

	return 0;
}
