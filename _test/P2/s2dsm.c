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

#define PGSZ (sysconf(_SC_PAGE_SIZE))
#define S2DSM_BUFLEN 4096

enum msi{ERROR, M, S, I};
struct map_info {
	void *address;
	size_t length;
	enum msi *msi_array;
};
struct service_thread_info {
	struct map_info *map;
	int send_fd;
	pthread_mutex_t *mutex;
	pthread_cond_t *cond;
};
struct page_update {
	int type; /* Type of update
		* 0 - Call, sending a page update.
		* 1 - Response, receiving a response to a page update. */
	int page_no; /* Page this update corresponds to. */
	enum msi msi_flag; /* The meaning of this flag depends on the 'type':
       		* Type 0 (Call):
		* 	- M: 'I modified my page; invalidate yours.'
		* 	- S: 'My page is invalid; can I have yours?'
		* 	- I: unused.
		* Type 1 (Response):
		* 	- M: 'Acknowledged your 'M'; I have invalidated my page.'
		* 	- S: 'Here is my page. Update yours and move to 'S'.' 
		* 	- I: unused. */
	size_t data_len; /* How much data there is. */
	char data[]; /* Placeholder for data. */
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
	struct uffd_msg msg;
	struct uffdio_zeropage uffdio_zeropage;
	struct uffdio_range uffdio_range;
	ssize_t nread;

	uffd = *(int*)arg;
	free(arg);

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
		uffdio_range.len = PGSZ;
		uffdio_zeropage.range = uffdio_range;
		uffdio_zeropage.mode = 0;
		if (ioctl(uffd, UFFDIO_ZEROPAGE, &uffdio_zeropage) == -1) {
			perror("ioctl-UFFDIO_ZEROPAGE");
			exit(EXIT_FAILURE);
		}
	}
}

void write_pages(struct map_info *map, int page_start, int page_end, char *what, int cfd) {
	char *cursor = (char*)(map->address) + (PGSZ * page_start);
	while (page_start <= page_end) {
		/* Check if our page is NOT modified. */
		if (map->msi_array[page_start] != M) {
			/* Invalidate the page on the other end. */
			struct page_update page_req;
			page_req.type = 0;
			page_req.page_no = page_start;
			page_req.msi_flag = M; /* 'I modified my page; invalidate yours.' */
			page_req.data_len = 0;
			write(cfd, &page_req, sizeof(struct page_update));
			map->msi_array[page_start] = M;
		}
		memset(cursor, 0, PGSZ);
		strcpy(cursor, what);
		page_start++;
		cursor += PGSZ;
	}
}

void read_pages(struct map_info *map, int page_start, int page_end,
		pthread_mutex_t *mutex, pthread_cond_t *cond, int cfd) {
	char *cursor = (char*)(map->address) + (PGSZ * page_start);
	while (page_start <= page_end) {
		/* Check if our page is invalid. */
		if (map->msi_array[page_start] == I) {
			/* Fetch page from other process. */
			pthread_mutex_lock(mutex);
			struct page_update page_req;
			page_req.type = 0;
			page_req.page_no = page_start;
			page_req.msi_flag = S; /* 'My page is invalid; can I have yours?' */
			page_req.data_len = 0;
			write(cfd, &page_req, sizeof(struct page_update));
			while (map->msi_array[page_start] == I)
				pthread_cond_wait(cond, mutex); /* Wait for page to be updated. */
			pthread_mutex_unlock(mutex);
		}
		/* Page is valid for our process (M or S state). We can now read it. */
		*cursor = *cursor;
		printf(" [*] Page %i:\n%s\n", page_start, cursor);
		page_start++;
		cursor += PGSZ;
	}
}

char *msi_to_str(enum msi msi) {
	switch (msi) {
		case M:
			return "MODIFIED";
		case S:
			return "SHARED";
		case I:
			return "INVALID";
		default:
			return "ERROR";
	}
}

void print_msi_array(enum msi *msi_array, int page_start, int page_end) {
	enum msi *cursor = msi_array + page_start;
	while (page_start <= page_end) {
		printf(" Page %i: %s\n", page_start, msi_to_str(*cursor));
		page_start++;
		cursor++;
	}
}

void *do_service_loop(void *ptr) {
	struct service_thread_info *sti = (struct service_thread_info*)ptr;
	struct map_info *map = sti->map;
	int cfd = sti->send_fd;
	pthread_mutex_t *mutex = sti->mutex;
	pthread_cond_t *cond = sti->cond;
	free(sti);
	char inst;
	int page, min, max, max_pages = (int)(map->length / PGSZ);
	char *buf = malloc(S2DSM_BUFLEN);	

	while (1) {
		/* Get input */
		printf(" > Which command should I run? (r:read, w:write, v:view msi array): ");
		fgets(buf, S2DSM_BUFLEN, stdin);
		inst = *buf;
		if (inst != 'r' && inst != 'w' && inst != 'v')
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
		switch (inst) {
			case 'w':
				printf(" > Type your new message: ");
				fgets(buf, S2DSM_BUFLEN, stdin);
				write_pages(map, min, max, buf, cfd);
			case 'r':
				read_pages(map, min, max, mutex, cond, cfd);
				break;
			case 'v':
				print_msi_array(map->msi_array, min, max);
				break;
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

	/* If cfd is -1, this is the first instance.
	 * In this case, block on accept() until the
	 * second instance wakes us up. */
	if (cfd == -1) {
		first = 1;
		sfd = server_get_socket(server_fd);
		cfd = setup_client(sendp);
		send(cfd, "", 1, 0);
	} else {
		send(cfd, "", 1, 0);
		sfd = server_get_socket(server_fd);
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

		mapping.length = pages * PGSZ;
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
	int pages = mapping.length / PGSZ;
	enum msi msi_array[pages];
	for (int i = 0; i < pages; i++)
		msi_array[i] = I;
	mapping.msi_array = msi_array;
	/* Both processes now have an mmapped region at the same virtual address. */

	/* Configure userfaultfd */
	int *uffd_fd = malloc(sizeof(int));
	*uffd_fd = setup_uffd(&mapping);

	pthread_t fault_handler;
	int fh_t = pthread_create(&fault_handler, NULL, fault_handler_thread, uffd_fd);
	if (fh_t != 0) {
		perror("pthread_create");
		exit(EXIT_FAILURE);
	}

	/* Spin off service loop thread, in which it asks for an operation on each page. */
	struct service_thread_info *sti = malloc(sizeof(struct service_thread_info));
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
	sti->map = &mapping;
	sti->send_fd = cfd;
	sti->mutex = &mutex;
	sti->cond = &cond;

	pthread_t service_loop;
	int sl_t = pthread_create(&service_loop, NULL, do_service_loop, sti);
	if (sl_t != 0) {
		perror("pthread_create");
		exit(EXIT_FAILURE);
	}

	/* Enter loop to listen on server fd for packets */
	struct page_update *update = malloc(sizeof(struct page_update) + PGSZ);
	while (1) {
		memset(update, 0, sizeof(struct page_update) + PGSZ);
		if (read(sfd, update, sizeof(struct page_update)) < sizeof(struct page_update)) {
		       perror("read");
		       exit(EXIT_FAILURE);
		}
		if (read(sfd, update->data, update->data_len) < update->data_len) {
			perror("read");
			exit(EXIT_FAILURE);
		}

		/* Perform operation on msi_array depending on packet received */
		if (update->type == 0) {
			/* This is a call, meaning the other process is requesting
			 * data from us. We need to send an appropriate response. */
			switch (update->msi_flag) {
				case M: /* 'I modified my page; invalidate yours.' */
					if (update->page_no == -1) {
						/* This is technically never used due to the way the process
						 * sends the request, but we will keep it here in case I
						 * want to optimize the request sending. */
						madvise(mapping.address, pages * PGSZ, MADV_DONTNEED);
						for (int i = 0; i < pages; i++)
							msi_array[i] = I;
					} else {
						msi_array[update->page_no] = I;
						madvise(mapping.address + (update->page_no * PGSZ), PGSZ, MADV_DONTNEED);
					}
					/* Setup acknowledgement. */
					update->type = 1;
					update->data_len = 0;
					write(cfd, update, sizeof(struct page_update));
					break;
				case S: /* 'My page is invalid; can I have yours?' */
					/* This will also only request 1 page at a time, so the range code is unused for now. */
					update->type = 1;
					int min = update->page_no;
					int max = min + 1;
					if (update->page_no == -1) {
						min = 0;
						max = pages;
					}
					/* Send all requested pages. */
					for (int i = min; i < max; i++) {
						void *page_addr = mapping.address + (i * PGSZ);
						/* If our page is also invalid, don't access it and just send 0. */
						size_t len = 0;
						if (msi_array[i] != I) {
							len = strlen(page_addr) + 1;
							strcpy(update->data, page_addr);
						}
						update->data_len = len;
						write(cfd, update, sizeof(struct page_update) + update->data_len);
						msi_array[i] = S;
					}
					break;
				default:
					update->type = 1;
					update->msi_flag = I; /* Unused flag. */
					update->data_len = 0;
					write(cfd, update, sizeof(struct page_update));
			}
		} else {
			/* We have received a response. We need to act appropriately
			 * and do not need to send a response. */
			switch (update->msi_flag) {
				case M: /* 'Acknowledged your 'M'; I have invalidated my page.' */
					// Do nothing (can possibly remove the ack)
					break;
				case S: /* 'Here is my page. Update yours and move to 'S'. */
					pthread_mutex_lock(&mutex);
					/* Read the received page. */
					if (update->data_len > 0) {
						void *page_addr = mapping.address + (update->page_no * PGSZ);
						strcpy(page_addr, update->data);
					}
					msi_array[update->page_no] = S;
					pthread_cond_signal(&cond); /* Signal that we're done writing. */
					pthread_mutex_unlock(&mutex);
				default:
					break;
			}
		}
	}
	return EXIT_SUCCESS;
}

