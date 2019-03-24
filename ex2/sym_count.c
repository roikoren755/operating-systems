#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int processId, fileDescriptor, counter, length; // Global for signal handlers
char symbol, *addr;

void sigPipeHandler(int signal) {
	if (signal == SIGPIPE) {
		fprintf(stderr, "SIGPIPE for process %d. Symbol %c. Counter %d.\n", processId, symbol, counter);
		raise(SIGTERM); // For cleanup
	}
}

void sigTermHandler(int signal) {
	if (signal == SIGTERM) {
		if (munmap(addr, length) < 0 | close(fileDescriptor) < 0) { // Un-map from memory, and close file
			exit(errno); // Error?
		}

		exit(SIGTERM);
	}
}

int main(int argc, char* argv[]) {
	processId = getpid();
	symbol = argv[2][0];

	if (signal(SIGPIPE, sigPipeHandler) == SIG_ERR || // Register handlers
		signal(SIGTERM, sigTermHandler) == SIG_ERR) {
		fprintf(stderr, "An error occurred while registering handlers for process %d.\n", processId);
		raise(SIGTERM);
	}

	fileDescriptor = open(argv[1], O_RDONLY); // Open file prior to mapping to memory
	if (fileDescriptor == -1) {
		fprintf(stderr, "An error occurred while opening the file for process %d.\n", processId);
		raise(SIGTERM);
	}

	struct stat fileStat;
	if (fstat(fileDescriptor, &fileStat) < 0) { // To calculate size of file
		fprintf(stderr, "Could not get file's stat for process %d.\n", processId);
		raise(SIGTERM);
	}

	length = fileStat.st_size;
	addr = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fileDescriptor, 0); // Map file to memory
	if (addr == MAP_FAILED) {
		fprintf(stderr, "Could not map file to virtual memory in process %d.\n", processId);
		raise(SIGTERM);
	}

	for (int i = 0; i < length; i++) { // Read file (in memory! woot!)
		if (addr[i] == symbol) {
			counter++;
		}
	}

	printf("Process %d finished. Symbol %c. Instances %d.\n", processId, symbol, counter); // Because of dup2 in the parent, sent to pipe

	raise(SIGTERM);
}
