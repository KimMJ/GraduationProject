#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <fcntl.h>

#include <cassert>
#include <thread>

#include "net_utils.hpp"
#include "ring_buffer.hpp"

#define MAXEVENTS		64
#define MAX_PENDING		256

#define	DEF_CLIENT		1
#define	DEF_SERVER		2


#define DEFAULT_BUFFER_SIZE 4096
#define DEFAULT_RING_BUFFER_SIZE 1024

#define SOCKET_PATH		"OIS_DOMAIN"

int g_epfd = -1;
int g_newfd = -1;
int g_mode = 0;

// Event data includes the socket FD,
// a pointer to a fixed sized buffer,
// number of bytes written and 
// a boolean indicating whether the worker thread should stop.
struct event_data {
	int fd;
	char* buffer;
	int written;
	bool stop;

	event_data():
	fd(-1),
		buffer(new char[DEFAULT_RING_BUFFER_SIZE]),
		written(0),
		stop(false) {

	}

	~event_data() {
		delete[] buffer;
	}
};

// Reverse in place.
void reverse(char* p, int length) {
	int length_before_null = length - 1;
	int i,j;

	for (i = 0, j = length_before_null; i < j; i++, j--) {
		char temp = p[i];
		p[i] = p[j];
		p[j] = temp;
	}
}



/** Returns true on success, or false if there was an error */
bool setnonblocking(int fd, bool blocking=true)
{
	if (fd < 0) return false;

	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return false;
	flags = blocking ? (flags&~O_NONBLOCK) : (flags|O_NONBLOCK);
	return (fcntl(fd, F_SETFL, flags) == 0) ? true : false;
}


void log_error(const char *msg)
{
	printf("%s: %s\n", msg, strerror(errno));
}

int create_server() {
	struct sockaddr_un addr;
	int fd;

	if ((fd = socket(AF_LOCAL, SOCK_STREAM, 0)) < 0) {
		log_error("Failed to create server socket");
		return fd;
	}

	memset(&addr, 0, sizeof(addr));

	addr.sun_family = AF_LOCAL;
	unlink(SOCKET_PATH);
	strcpy(addr.sun_path, SOCKET_PATH);

	if (bind(fd, (struct sockaddr *) &(addr),
		sizeof(addr)) < 0) {
			log_error("Failed to bind server socket");
			return -1;
	}

	if (listen(fd, MAX_PENDING) < 0) {
		log_error("Failed to listen on server socket");
		return -1;
	}

	setnonblocking(fd);

	/* Add handler to handle events on fd */
	return fd;
}


int connect_server() {
	struct sockaddr_un addr;
	int fd;

	if ((fd = socket(AF_LOCAL, SOCK_STREAM, 0)) < 0) {
		log_error("Failed to create client socket");
		return fd;
	}

	memset(&addr, 0, sizeof(addr));

	addr.sun_family = AF_LOCAL;
	strcpy(addr.sun_path, SOCKET_PATH);

	if (connect(fd,
		(struct sockaddr *) &(addr),
		sizeof(addr)) < 0) {
			log_error("Failed to connect to server");
			return -1;
	}

	setnonblocking(fd);
	/* Add handler to handle events */

	return fd;
}

 int recv_file_descriptor(
	int socket) /* Socket from which the file descriptor is read */
{
	struct msghdr message;
	struct iovec iov[1];
	struct cmsghdr *control_message = NULL;
	char ctrl_buf[CMSG_SPACE(sizeof(int))];
	char data[1];
	int res;

	memset(&message, 0, sizeof(struct msghdr));
	memset(ctrl_buf, 0, CMSG_SPACE(sizeof(int)));

	/* For the dummy data */
	iov[0].iov_base = data;
	iov[0].iov_len = sizeof(data);

	message.msg_name = NULL;
	message.msg_namelen = 0;
	message.msg_control = ctrl_buf;
	message.msg_controllen = CMSG_SPACE(sizeof(int));
	message.msg_iov = iov;
	message.msg_iovlen = 1;

	if((res = recvmsg(socket, &message, 0)) <= 0)
		return res;

	/* Iterate through header to find if there is a file descriptor */
	for(control_message = CMSG_FIRSTHDR(&message);
		control_message != NULL;
		control_message = CMSG_NXTHDR(&message,
		control_message))
	{
		if( (control_message->cmsg_level == SOL_SOCKET) &&
			(control_message->cmsg_type == SCM_RIGHTS) )
		{
			return *((int *) CMSG_DATA(control_message));
		}
	}

	return -1;
}

 int
	send_file_descriptor(
	int socket, /* Socket through which the file descriptor is passed */
	int fd_to_send) /* File descriptor to be passed, could be another socket */
{
	struct msghdr message;
	struct iovec iov[1];
	struct cmsghdr *control_message = NULL;
	char ctrl_buf[CMSG_SPACE(sizeof(int))];
	char data[1];

	memset(&message, 0, sizeof(struct msghdr));
	memset(ctrl_buf, 0, CMSG_SPACE(sizeof(int)));

	/* We are passing at least one byte of data so that recvmsg() will not return 0 */
	data[0] = ' ';
	iov[0].iov_base = data;
	iov[0].iov_len = sizeof(data);

	message.msg_name = NULL;
	message.msg_namelen = 0;
	message.msg_iov = iov;
	message.msg_iovlen = 1;
	message.msg_controllen =  CMSG_SPACE(sizeof(int));
	message.msg_control = ctrl_buf;

	control_message = CMSG_FIRSTHDR(&message);
	control_message->cmsg_level = SOL_SOCKET;
	control_message->cmsg_type = SCM_RIGHTS;
	control_message->cmsg_len = CMSG_LEN(sizeof(int));

	*((int *) CMSG_DATA(control_message)) = fd_to_send;

	return sendmsg(socket, &message, 0);
}


//
// socket을 수신하여 epoll에등록한다.
int domainSockServer() {
	if ( g_mode != DEF_SERVER )	return 0;
	int sfd = create_server();
	int fd = -1;
	while(true)
	{
		fd = accept(sfd, NULL, NULL);
		if ( fd > 0 )
		{
			while(fd > 0)
			{
				int newfd = recv_file_descriptor(fd);
				if ( newfd > 0 )
				{
					// 다른 프로세스에서 수신한 소켓을 epoll에 등록한다.
					printf("new socket을 수신하였습니다.: %d,  epoll에 등록합니다.\n", newfd);

					// epoll_add
					struct epoll_event event;
					// Register the new FD to be monitored by epoll.
					event.data.fd = newfd;
					// Register for read events, disconnection events and enable edge triggered behavior for the FD.
					event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
					int retval = epoll_ctl(g_epfd, EPOLL_CTL_ADD, newfd, &event);
					if (retval == -1) {
						perror("epoll_ctl");
						abort();
					}
				}
				else
				{
					printf("recv_file_descriptor() error\n");
					close(fd);
					fd = -1;
				}
			}
		}
	}

	return 0;
}




int domainSockClient()
{
	while(true)
	{
		usleep(10);
		// 클라이언트가 접속하면 g_newfd가 설정됩니다.
		if ( g_newfd != -1 )
		{
			printf("새로운 소켓을 전달합니다.(%d)\n", g_newfd);
			send_file_descriptor(g_epfd, g_newfd);

			struct epoll_event event;
			// Register the new FD to be monitored by epoll.
			event.data.fd = g_newfd;
			// Register for read events, disconnection events and enable edge triggered behavior for the FD.
			event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
			epoll_ctl(g_epfd, EPOLL_CTL_DEL, g_newfd, &event);
			close(g_newfd);
			g_newfd = -1;
		}
	}
	return 0;
}

int process_messages(processor::RingBuffer<event_data>* ring_buffer) {
	int64_t prev_sequence = -1;
	int64_t next_sequence = -1;
	int num_events_processed = 0;
	int s = -1;
	while (true) {
		// Spin till a new sequence is available.
		while (next_sequence <= prev_sequence) {
			_mm_pause();
			next_sequence = ring_buffer->getProducerSequence();
		}
		// Once we have a new sequence process all items between the previous sequence and the new one.
		for (int64_t index = prev_sequence + 1; index <= next_sequence; index++) {
			auto e_data = ring_buffer->get(index);
			auto client_fd = e_data->fd;
			auto buffer = e_data->buffer;
			// Used to signal the server to stop.
			//printf("\nConsumer stop value %s\n", e_data->stop ? "true" : "false");
			if (e_data->stop)
				goto exit_consumer;

			auto buffer_length = e_data->written;
			assert(client_fd != -1);
			assert(buffer_length > 0);

			// Write the buffer to standard output first.
			s = write (1, buffer, buffer_length);
			if (s == -1) {
				perror ("write");
				abort ();
			}

			// Then reverse it and echo it back.
			reverse(buffer, buffer_length);
			s = write(client_fd, buffer, buffer_length);
			if (s == -1) {
				perror ("echo");
				abort ();
			}
			// We are not checking to see if all the bytes have been written.
			// In case they are not written we must use our own epoll loop, express write interest
			// and write when the client socket is ready.
			++num_events_processed;
		}
		// Mark events consumed.
		ring_buffer->markConsumed(next_sequence);
		prev_sequence = next_sequence;
	}
exit_consumer:
	printf("Finished processing all events. Server shutting down. Num events processed = %d\n", num_events_processed);
	return 1;
}

void event_loop(int epfd,
	int sfd,
	processor::RingBuffer<event_data>* ring_buffer) {
		int n, i;
		int retval;

		struct epoll_event event, current_event;
		// Buffer where events are returned.
		struct epoll_event* events = static_cast<epoll_event*>(calloc(MAXEVENTS, sizeof event));

		while (true) {

			n = epoll_wait(epfd, events, MAXEVENTS, -1);

			for (i = 0; i < n; i++) {
				current_event = events[i];

				if ((current_event.events & EPOLLERR) ||
					(current_event.events & EPOLLHUP) ||
					(!(current_event.events & EPOLLIN))) {
						// An error has occured on this fd, or the socket is not ready for reading (why were we notified then?).
						fprintf(stderr, "epoll error - An error has occured on this fd, or the socket is not ready for reading\n");
						close(current_event.data.fd);
				} else if (current_event.events & EPOLLRDHUP) {
					// Stream socket peer closed connection, or shut down writing half of connection.
					// We still to handle disconnection when read()/recv() return 0 or -1 just to be sure.
					printf("Closed connection on descriptor vis EPOLLRDHUP %d\n", current_event.data.fd);
					// Closing the descriptor will make epoll remove it from the set of descriptors which are monitored.
					close(current_event.data.fd);
				} else if (sfd == current_event.data.fd) {
					// We have a notification on the listening socket, which means one or more incoming connections.
					while (true) {
						struct sockaddr in_addr;
						socklen_t in_len;
						int infd;
						char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

						in_len = sizeof in_addr;
						// No need to make these sockets non blocking since accept4() takes care of it.
						infd = accept4(sfd, &in_addr, &in_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
						if (infd == -1) {
							if ((errno == EAGAIN) ||
								(errno == EWOULDBLOCK)) {
									break;  // We have processed all incoming connections.
							} else {
								perror("accept");
								break;
							}
						}

						// Print host and service info.
						retval = getnameinfo(&in_addr, in_len,
							hbuf, sizeof hbuf,
							sbuf, sizeof sbuf,
							NI_NUMERICHOST | NI_NUMERICSERV);
						if (retval == 0) {
							printf("Accepted connection on descriptor %d (host=%s, port=%s)\n", infd, hbuf, sbuf);
						}

						// Register the new FD to be monitored by epoll.
						event.data.fd = infd;
						// Register for read events, disconnection events and enable edge triggered behavior for the FD.
						event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
						retval = epoll_ctl(epfd, EPOLL_CTL_ADD, infd, &event);
						if (retval == -1) {
							perror("epoll_ctl - event add error");
							abort();
						}
						// 클라이언트로 동작하는 경우라면 서버에 방금 연결된 소켓을 전달하고 epoll 목록에서 제거한다.
						if ( g_mode == DEF_CLIENT)
						{
							g_newfd = infd;
						}
					}
				} else {
					// We have data on the fd waiting to be read. Read and  display it.
					// We must read whatever data is available completely, as we are running in edge-triggered mode
					// and won't get a notification again for the same data.
					bool should_close = false, done = false;

					while (!done) {
						ssize_t count;
						// Get the next ring buffer entry.
						auto next_write_index = ring_buffer->nextProducerSequence();
						auto entry = ring_buffer->get(next_write_index);

						// Read the socket data into the buffer associated with the ring buffer entry.
						// Set the entry's fd field to the current socket fd.
						count = read(current_event.data.fd, entry->buffer, DEFAULT_BUFFER_SIZE);
						entry->written = count;
						entry->fd = current_event.data.fd;

						if (count == -1) {
							// EAGAIN or EWOULDBLOCK means we have no more data that can be read.
							// Everything else is a real error.
							if (!(errno == EAGAIN || errno == EWOULDBLOCK)) {
								perror("read");
								should_close = true;
							}
							done = true;
						} else if (count == 0) {
							// Technically we don't need to handle this here, since we wait for EPOLLRDHUP. We handle it just to be sure.
							// End of file. The remote has closed the connection.
							should_close = true;
							done = true;
						} else {
							// Valid data. Process it.
							// Check if the client want's the server to exit.
							// This might never work out even if the client sends an exit signal because TCP might
							// split and rearrange the packets across epoll signal boundaries at the server.
							bool stop = (strncmp(entry->buffer, "exit", 4) == 0);
							entry->stop = stop;

							// Publish the ring buffer entry since all is well.
							ring_buffer->publish(next_write_index);
							if (stop)
								goto exit_loop;
						}
					}


					if (should_close) {
						printf("Closed connection on descriptor %d\n", current_event.data.fd);
						// Closing the descriptor will make epoll remove it from the set of descriptors which are monitored.
						close(current_event.data.fd);
					}
				}
			}
		}
exit_loop:
		free(events);
}

int main (int argc, char *argv[]) {
	int sfd, epfd, retval;
	// Our ring buffer.
	auto ring_buffer = new processor::RingBuffer<event_data>(DEFAULT_RING_BUFFER_SIZE);

	if (argc != 3) {
		fprintf(stderr, "Usage: %s [S|C] [port]\n", argv[0]);
		exit (EXIT_FAILURE);
	}

	if (argv[1][0] == 'C' ) g_mode = DEF_CLIENT;
	else g_mode = DEF_SERVER;


	if ( g_mode == DEF_CLIENT )
	{
		sfd = create_and_bind(argv[2]);
		if (sfd == -1)
			abort ();

		retval = make_socket_non_blocking(sfd);
		if (retval == -1)
			abort ();

		retval = listen(sfd, SOMAXCONN);
		if (retval == -1) {
			perror ("listen");
			abort ();
		}
	}
	epfd = epoll_create1(0);
	if (epfd == -1) {
		perror ("epoll_create");
		abort ();
	}

	g_epfd = epfd;

	// Register the listening socket for epoll events.
	if ( g_mode == DEF_CLIENT )
	{
		struct epoll_event event;
		event.data.fd = sfd;
		event.events = EPOLLIN | EPOLLET;
		retval = epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &event);
		if (retval == -1) {
			perror ("epoll_ctl");
			abort ();
		}
	}


	// Start the worker thread.
	std::thread t{process_messages, ring_buffer};
	std::thread passsock {domainSockServer};
	if ( g_mode == DEF_CLIENT ) g_epfd = connect_server();
	std::thread sendsock { domainSockClient };

	// Start the event loop.
	//event_loop(epfd, sfd, ring_buffer);
	std::thread el {event_loop , epfd, sfd, ring_buffer};

	// Our server is ready to stop. Release all pending resources.
	t.join();
	sendsock.join();
	passsock.join();
	el.join();

	close(sfd);
	delete ring_buffer;

	return EXIT_SUCCESS;
}