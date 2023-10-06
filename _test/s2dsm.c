#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/mman.h>

struct map_info {
	void *address;
	size_t length;
};

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
	int server_fd, sfd, cfd;
	char first = 0;  // Flag to keep track of whether this is the first or second instance

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

	server_fd = setup_server(listenp);
	cfd = setup_client(sendp);
	// fprintf(stderr, "Got server_fd=%i and cfd=%i\n", server_fd, cfd);

	/* If cfd is -1, this is the first instance.
	 * In this case, block on accept() until the
	 * second instance wakes us up. */
	if (cfd == -1) {
		first = 1;
		sfd = server_get_socket(server_fd);
		// fprintf(stderr, "sfd: %i\n", sfd);
		cfd = setup_client(sendp);
		// fprintf(stderr, "Sending byte to %i\n", cfd);
		send(cfd, "", 1, 0);
	} else {
		// fprintf(stderr, "Sending byte to %i\n", cfd);
		send(cfd, "", 1, 0);
		sfd = server_get_socket(server_fd);
		// fprintf(stderr, "sfd: %i\n", sfd);
	}

	/* At this point, both clients can communicate by
	 * sending to cfd, and receiving from sfd.
	 * We clear out the sync byte, then ask for input. */
	char temp;
	read(sfd, &temp, 1);


	/* Get the number of pages to allocate, then send to
	 * waiting second client */
	if (first) {
getinput:
		printf(" > How many pages would you like to allocate (greater than 0)?\n");
		char *num = malloc(100);
		fgets(num, 100, stdin);
		long pages = atol(num);
		free(num);
		if (pages <= 0) {
			printf("Invalid input.\n");
			goto getinput;
		}

		fprintf(stderr, "Received number %li\n", pages);
			
		struct map_info mapping;
		mapping.length = pages * sysconf(_SC_PAGE_SIZE);
		mapping.address = mmap(NULL, mapping.length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (mapping.address == MAP_FAILED) {
			printf("mmap() failed. Please retry.");
			exit(EXIT_FAILURE);
		}

		printf("Map address: %p\nSize: %lu\n", mapping.address, mapping.length);
		send(cfd, &mapping, sizeof(mapping), 0);
	} else {
		struct map_info mapping;
		if (read(sfd, &mapping, sizeof(mapping)) < sizeof(mapping)) {
			printf("read() failed. Please retry.");
			exit(EXIT_FAILURE);
		}
		void *map = mmap(mapping.address, mapping.length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		printf("Map address: %p\nSize: %lu\n", map, mapping.length);
	}

	/* Both processes now have an mmapped region at the same virtual address. */

	return 0;
}

