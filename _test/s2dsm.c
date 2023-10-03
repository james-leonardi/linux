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
struct sockaddr_in saddress = {0};
int setup_server(int port) {
	int fd;

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		return -1;
	}

	saddress.sin_family = AF_INET;
	saddress.sin_addr.s_addr = INADDR_ANY;
	saddress.sin_port = htons(port);
	
	if (bind(fd, (struct sockaddr *)&saddress, sizeof(saddress)) < 0) {
		return -1;
	}

	if (listen(fd, 1) < 0) {
		return -1;
	}

	return fd;
}

/*
 * Accepts the next connection and returns the socket fd.
 * On failure, returns -1.
 */
int server_get_socket(int server_fd) {
	int sockfd;
	socklen_t addrlen = sizeof(struct sockaddr_in);

	if ((sockfd = accept(server_fd, (struct sockaddr *)&saddress, &addrlen)) < 0) {
		return -1;
	}

	return sockfd;
}

/*
 * Creates a socket client on port and returns the fd
 * Returns -1 on failure.
 */
struct sockaddr_in caddress = {0};
int setup_client(int port) {
	int fd;

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		return -1;
	}

	caddress.sin_family = AF_INET;
	caddress.sin_port = htons(port);
	if (inet_pton(AF_INET, "127.0.0.1", &caddress.sin_addr) <= 0) {
		return -1;
	}

	if (connect(fd, (struct sockaddr *)&caddress, sizeof(caddress))) {
		return -1;
	}

	return fd;
}

int main(int argc, char **argv) {
	int listenp, sendp;

	if (argc != 3) {
		printf("Usage: %s <listen port> <send port>\n", *argv);
		return EXIT_FAILURE;
	}

	listenp = atoi(argv[1]);
	sendp = atoi(argv[2]);

	if (listenp == sendp || port_invalid(listenp) || port_invalid(sendp)) {
		printf("Please provide two unique ports between 1024-65535\n");
		return EXIT_FAILURE;
	}

	int sfd = setup_server(listenp);
	int cfd = setup_client(sendp);
	printf("Got listen=%i and send=%i\n", sfd, cfd);

	/* If cfd is -1, this is the first instance.
	 * In this case, block on accept() until the
	 * second instance wakes us up. */
	if (cfd == -1) {
		printf("New server socket: %i\n", server_get_socket(sfd));
	} else {
		send(cfd, "test", 5, 0);
	}
	
	return 0;
}

