#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define NUMBER_OF_PRINTABLE_CHARS 95
#define BUFFER_SIZE 1024

sig_atomic_t threadCount = 0;
pthread_mutex_t mutex;
unsigned int printableCharCounts[NUMBER_OF_PRINTABLE_CHARS];

void sigIntHandler(int signal) {
	if (signal == SIGINT) {
		while (threadCount > 0) {
			sleep(1);
		}

		printf("\n");
		for (int i = 0; i < NUMBER_OF_PRINTABLE_CHARS; i++) {
			printf("char '%c' : %u times\n", i + 32, printableCharCounts[i]);
		}

		fflush(stdout);

		exit(0);
	}
}

void* threadFunction(void* thread_param) {
	threadCount++;
	int connection = *(int*) thread_param;
	unsigned int threadPrintableCharCounts[NUMBER_OF_PRINTABLE_CHARS];
	for (int i = 0; i < NUMBER_OF_PRINTABLE_CHARS; i++) {
		threadPrintableCharCounts[i] = 0;
	}
	int printableCharsCount = 0;
	char buffer[BUFFER_SIZE];

	uint32_t N;
	char* length = (char*) &N;
	int size = sizeof(N);
	int receivedBytes;
	do {
		if ((receivedBytes = read(connection, length, size)) < 0) {
			perror("pcc_server: Could not read message length from client");
			exit(errno);
		}
		
		length += receivedBytes;
		size -= receivedBytes;
	} while (size > 0);

	N = ntohl(N);

	do {
		int toReceive = N > BUFFER_SIZE ? BUFFER_SIZE : N;
		if ((receivedBytes = read(connection, buffer, toReceive)) < 0) {
			perror("pcc_server: Could not read message from client");
			exit(errno);
		}

		for (int i = 0; i < receivedBytes; i++) {
			if (buffer[i] >= 32 && buffer[i] <= 126) {
				threadPrintableCharCounts[buffer[i] - 32]++;
				printableCharsCount++;
			}
		}

		N -= receivedBytes;
	} while (N > 0);

	if (pthread_mutex_lock(&mutex)) {
		perror("pcc_server: Could not lock mutex");
		exit(errno);
	}

	for (int i = 0; i < NUMBER_OF_PRINTABLE_CHARS; i++) {
		printableCharCounts[i] += threadPrintableCharCounts[i];
	}

	if (pthread_mutex_unlock(&mutex)) {
		perror("pcc_server: Could not unlock mutex");
		exit(errno);
	}

	uint32_t networkCount = htonl(printableCharsCount);
	char* byteCount = (char*) &networkCount;
	size = sizeof(networkCount);
	int sent;
	do {
		if ((sent = write(connection, byteCount, size)) < 0) {
			perror("pcc_server: Could not send count to client");
			exit(errno);
		}

		byteCount += sent;
		size -= sent;
	} while (size > 0);

	close(connection);
	threadCount--;
	return (void*) 0;
}

int main(int argc, char* argv[]) {
	if (signal(SIGINT, sigIntHandler) == SIG_ERR) {
		perror("pcc_server: Could not register signal handler");
		return 1;
	}
	for (int i = 0; i < NUMBER_OF_PRINTABLE_CHARS; i++) {
		printableCharCounts[i] = 0;
	}
	uint16_t serverPort = (unsigned int) atoi(argv[1]);

	if (pthread_mutex_init(&mutex, NULL)) { // Init mutex
		perror("pcc_server: Could not initialize mutex");
		return 1;
	}

	int sock;
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("pcc_server: Could not create socket");
		return errno;
	}

	struct sockaddr_in serverAddress;
	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddress.sin_port = htons(serverPort);

	if (bind(sock, (struct sockaddr*) &serverAddress, sizeof(serverAddress)) == -1) {
		perror("pcc_server: Could not bind address");
		return 1;
	}

	if (listen(sock, 10) == -1) {
		perror("pcc_server: Could not listen for connections");
		return 1;
	}

	int connection;
	pthread_t thread_id;
	while (1) {
		connection = accept(sock, NULL, NULL);
		if (connection < 0) {
			if (errno == EINTR) {
				break;
			}
			perror("pcc_server: Failed to accept connection");
			return 1;
		}

		if (pthread_create(&thread_id, NULL, threadFunction, (void*) &connection)) {
			perror("pcc_server: Failed to create thread for connection");
			return 1;
		}
	}

	return 0;
}
