#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>

#define S2DSM_BUFLEN 4097

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

/*
 * Sets up uffd and returns the file descriptor.
 */
int setup_uffd(struct map_info *map) {
	int uffd;
	struct uffdio_api uffdio_api;
	struct uffdio_register uffdio_register;

	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd == -1) {
		perror("userfaultfd");
		exit(EXIT_FAILURE);
	}

	uffdio_api.api = UFFD_API;
	uffdio_api.features = 0;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1) {
		perror("ioctl-UFFDIO_API");
		exit(EXIT_FAILURE);
	}

	uffdio_register.range.start = (unsigned long) map->address;
	uffdio_register.range.len = map->length;
	uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;
	if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
		perror("ioctl-UFFDIO_REGISTER");
		exit(EXIT_FAILURE);
	}

	return uffd;
}

/*
 * Thread for handling pagefaults..
 */
void *fault_handler_thread(void *arg) {
	int uffd;
	void *page;
	struct uffd_msg msg;
	//struct uffdio_copy uffdio_copy;
	struct uffdio_zeropage uffdio_zeropage;
	struct uffdio_range uffdio_range;
	ssize_t nread;

	uffd = *(int*)arg;
	free(arg);

	page = mmap(NULL, sysconf(_SC_PAGE_SIZE), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (page == MAP_FAILED) {
		perror("mmap");
		exit(EXIT_FAILURE);
	}

	while (1) {
		struct pollfd pollfd;
		int nready;

		pollfd.fd = uffd;
		pollfd.events = POLLIN;
		nready = poll(&pollfd, 1, -1);
		if (nready == -1) {
			perror("poll");
			exit(EXIT_FAILURE);
		}

		nread = read(uffd, &msg, sizeof(msg));
		if (nread < sizeof(msg)) {
			perror("read");
			exit(EXIT_FAILURE);
		}

		if (msg.event != UFFD_EVENT_PAGEFAULT) {
			printf("Unexpected event.\n");
			exit(EXIT_FAILURE);
		}

		printf(" [x] PAGEFAULT\n");

		uffdio_range.start = msg.arg.pagefault.address;
		uffdio_range.len = sysconf(_SC_PAGE_SIZE);
		uffdio_zeropage.range = uffdio_range;
		uffdio_zeropage.mode = 0;
		if (ioctl(uffd, UFFDIO_ZEROPAGE, &uffdio_zeropage) == -1) {
			perror("ioctl-UFFDIO_ZEROPAGE");
			exit(EXIT_FAILURE);
		}
	}
}

void write_pages(struct map_info *map, int page_start, int page_end, char *what) {
	char *cursor = (char*)(map->address) + (sysconf(_SC_PAGE_SIZE) * page_start);
	while (page_start <= page_end) {
		memset(cursor, 0, sysconf(_SC_PAGE_SIZE));
		strcpy(cursor, what);
		page_start++;
		cursor += sysconf(_SC_PAGE_SIZE);
	}
}

void read_pages(struct map_info *map, int page_start, int page_end) {
	char *cursor = (char*)(map->address) + (sysconf(_SC_PAGE_SIZE) * page_start);
	while (page_start <= page_end) {
		*cursor = *cursor;
		printf(" [*] Page %i:\n%s\n", page_start, cursor);
		page_start++;
		cursor += sysconf(_SC_PAGE_SIZE);
	}
}

void do_service_loop(struct map_info *map) {
	char inst;
	int page;
	int min, max, max_pages = (int)(map->length / sysconf(_SC_PAGE_SIZE));
	char *buf = malloc(S2DSM_BUFLEN);

	while (1) {
		/* Get input */
		printf(" > Which command should I run? (r:read, w:write): ");
		fgets(buf, S2DSM_BUFLEN, stdin);
		inst = *buf;
		if (inst != 'r' && inst != 'w')
			continue;
		printf(" > For which page? (0-%i, or -1 for all): ", max_pages - 1);
		fgets(buf, S2DSM_BUFLEN, stdin);
		page = atoi(buf);
		if (page < -1 || page >= max_pages)
			continue;

		/* Setup page ranges */
		if (page == -1) {
			min = 0;
			max = max_pages - 1;
		} else {
			min = page;
			max = page;
		}

		/* Do operation */
		if (inst == 'w') {
			printf(" > Type your new message: ");
			fgets(buf, S2DSM_BUFLEN, stdin);
			write_pages(map, min, max, buf);
			read_pages(map, min, max);
		} else {
			read_pages(map, min, max);
		}
	}
	free(buf);
}

int main(int argc, char **argv) {
	int listenp, sendp;
	int server_fd, sfd, cfd;
	int first = 0;  // Flag to keep track of whether this is the first or second instance

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
	struct map_info mapping;
	if (first) {
getinput:
		printf(" > How many pages would you like to allocate (greater than 0)?\n");
		char *num = malloc(S2DSM_BUFLEN);
		fgets(num, S2DSM_BUFLEN, stdin);
		int pages = atoi(num);
		free(num);
		if (pages <= 0) {
			printf("Invalid input.\n");
			goto getinput;
		}

		// fprintf(stderr, "Received number %li\n", pages);
			
		mapping.length = pages * sysconf(_SC_PAGE_SIZE);
		mapping.address = mmap(NULL, mapping.length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (mapping.address == MAP_FAILED) {
			printf("mmap() failed. Please retry.");
			exit(EXIT_FAILURE);
		}

		printf("Map address: %p\nSize: %lu\n", mapping.address, mapping.length);
		send(cfd, &mapping, sizeof(mapping), 0);
	} else {
		if (read(sfd, &mapping, sizeof(mapping)) < sizeof(mapping)) {
			printf("read() failed. Please retry.");
			exit(EXIT_FAILURE);
		}
		void *map = mmap(mapping.address, mapping.length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		printf("Map address: %p\nSize: %lu\n", map, mapping.length);
	}

	/* Both processes now have an mmapped region at the same virtual address. */

	/* Configure userfaultfd */
	int *uffd_fd = malloc(sizeof(int));
	*uffd_fd = setup_uffd(&mapping);

	pthread_t thread;
	int t = pthread_create(&thread, NULL, fault_handler_thread, uffd_fd);
	if (t != 0) {
		perror("pthread_create");
		exit(EXIT_FAILURE);
	}

	/* Enter service loop, in which it asks for an operation on each page */
	do_service_loop(&mapping);

	return 0;
}

