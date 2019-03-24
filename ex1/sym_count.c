#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

int processId, fileDescriptor, count;
char c;

void sigtermHandler(int signalNo) {
	if (signalNo == SIGTERM) {
		printf("Process %d finishes. Symbol %c. Instances %d\n", processId, c, count);
		close(fileDescriptor);
		if (errno) {
			exit(errno);
		}
		exit(signalNo);
	}
}

void sigcontHandler(int signalNo) {
	if (signalNo == SIGCONT) {
		printf("Process %d continues\n", processId);
	}
}

int main(int argc, char* argv[]) {
	processId = getpid();
	c = argv[2][0];

	if (signal(SIGTERM, sigtermHandler) == SIG_ERR ||
			signal(SIGCONT, sigcontHandler) == SIG_ERR) {
		printf("An error occurred while registering handlers for process %d.\n", processId);
		raise(SIGTERM);
	}

	fileDescriptor = open(argv[1], O_RDONLY);
	if (fileDescriptor < 0) {
		printf("%s\n", strerror(errno));
		raise(SIGTERM);
	}

	char buffer[BUFFER_SIZE + 1];
	int readBytes;

	while ((readBytes = read(fileDescriptor, buffer, BUFFER_SIZE)) >= 0) {
		for (int i = 0; i < readBytes; i++) {
			if (buffer[i] == c) {
				count++;
				printf("Process %d, symbol %c, going to sleep\n", processId, c);
				raise(SIGSTOP);
			}
		}

		if (readBytes < BUFFER_SIZE) {
			raise(SIGTERM);
		}
	}

	if (readBytes < 0) {
		printf("%s\n", strerror(errno));
		raise(SIGTERM);
	}
}
