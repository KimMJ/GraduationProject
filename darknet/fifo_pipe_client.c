#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#define BUFSIZE 30
#define FIFO_PERMS (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

int main() {
	
	char str[] = "my name is yeongho";
	char buf[BUFSIZE];
	int fd_to_server;
	int fd_from_server;

	if (-1 == (fd_from_server=open("server_send", O_RDWR))) {
		perror("open error: ");
		return 2;
	}
	
	if (-1 == (fd_to_server=open("client_send", O_WRONLY))) {
		perror("open error: ");
		return 2;
	} 
	while(1) {

		memset(buf, 0, BUFSIZE);

		if (read(fd_from_server, buf, BUFSIZE) < 0) {
			perror("write error: ");
			return 3;
		}

		printf("2. client receive message: %s\n", buf);

		printf("3. client will send message: %s\n", str);

		if (write(fd_to_server, str, sizeof(str)) < 0) {
			perror("read error: ");
			return 4;
		}
	}
	

	return 0;
}
