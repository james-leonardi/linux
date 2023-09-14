#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define SYS_s2_encrypt 335
int main(int argc, char **argv) {
	int opt;
	char *str = NULL;
	int key = 0;
	int s = 0, k = 0;

	while ((opt = getopt(argc, argv, "s:k:")) != -1) {
		switch (opt) {
		case 's':
			s = 1;
			size_t len = strlen(optarg);
			str = malloc(len);
			strncpy(str, optarg, len);
			break;
		case 'k':
			k = 1;
			key = atoi(optarg);
			break;
		default:
			fprintf(stderr, "Usage: %s -s string -k key\n", *argv);
			return(EXIT_FAILURE);
		}
	}

	if (!s || !k) {
		fprintf(stderr, "Usage: %s -s string -k key\n", *argv);
		if (s) free(str);
		return EXIT_FAILURE;
	}

	fprintf(stderr, "[s2_encrypt_test] Giving string '%s' to s2_encrypt()\n", str);	
	long ret = syscall(SYS_s2_encrypt, str, key);
	if (ret) 	fprintf(stderr, "[s2_encrypt_test] Received the following error code from s2_encrypt():\n");
	else		fprintf(stderr, "[s2_encrypt_test] Received string '%s' from s2_encrypt()\n", str);
	printf("%li\n", ret);
	free(str);
	return EXIT_SUCCESS;
}

#undef SYS_s2_encrypt
