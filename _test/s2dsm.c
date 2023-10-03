#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

int port_invalid(int port) {
	return port < 1024 || port > 65535;
}

/*
 * Creates a socket server on port and returns the fd.
 * Returns -1 on failure.
 */
int setup_server(int port) {
	int fd;
	struct sockaddr_in address = {0};
	socklen_t socklen = sizeof(address);

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		return -1;
	}

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(port);
	
	if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		return -1;
	}

	if (listen(fd, 1) < 0) {
		return -1;
	}

	return accept(fd, (struct sockaddr *)&address, &socklen);
}

/*
 * Creates a socket client on port and returns the fd.
 */
int setup_client(int port) {
	int fd;
	struct sockaddr_in address = {0};

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		return -1;
	}

	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) <= 0) {
		return -1;
	}

	return connect(fd, (struct sockaddr *)&address, sizeof(address));
}

int main(int argc, char **argv) {
	int listen, send;

	if (argc != 3) {
		printf("Usage: %s <listen port> <send port>\n", *argv);
		return EXIT_FAILURE;
	}

	listen = atoi(argv[1]);
	send = atoi(argv[2]);

	if (listen == send || port_invalid(listen) || port_invalid(send)) {
		printf("Please provide two unique ports between 1024-65535\n");
		return EXIT_FAILURE;
	}

	int sfd = setup_server(listen);
	int cfd = setup_client(send);

	printf("Got listen=%i and send=%i\n", sfd, cfd);
	return 0;
}

