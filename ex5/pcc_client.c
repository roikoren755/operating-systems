#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

int isIpAddress(char* address) {
	int i = 0;
	while (address[i]) {
		if ((address[i] > '0' && address[i] < '9') || address[i] == '.') {
			i++;
		}

		else {
			return 0;
		}
	}

	return 1;
}

int main(int argc, char* argv[]) {
	uint16_t serverPort = (unsigned int) atoi(argv[2]);
	uint32_t length = (unsigned int) atoi(argv[3]);
	char* buffer = (char*) malloc(sizeof(char) * BUFFER_SIZE);

	int sock;
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("pcc_client: Could not create socket");
		return errno;
	}

	struct sockaddr_in serverAddress;
	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(serverPort);

	int isIp = isIpAddress(argv[1]);
	if (isIp) {
		if (!inet_aton(argv[1], &serverAddress.sin_addr)) {
			perror("pcc_client: Invalid IP address");
			return errno;
		}
	}
	else {
		struct addrinfo* result;
		if (getaddrinfo(argv[1], argv[2], NULL, &result) != 0) {
			perror("pcc_client: Could not get address information");
			return errno;
		}

		struct sockaddr_in* sockAddress = (struct sockaddr_in*)result[0].ai_addr;

		serverAddress.sin_addr.s_addr = sockAddress->sin_addr.s_addr;
	}

	if (connect(sock, (struct sockaddr*) &serverAddress, sizeof(serverAddress)) < 0) {
		perror("pcc_client: Could not connect to server");
		return errno;
	}

	uint32_t networkLength = htonl(length);
	char* byteLength = (char*) &networkLength;
	int size = sizeof(networkLength);
	int sent;
	do {
		if ((sent = write(sock, byteLength, size)) < 0) {
			perror("pcc_client: Could not send message length to server");
			return errno;
		}

		byteLength += sent;
		size -= sent;
	} while (size > 0);

	int urandomDescriptor = open("/dev/urandom", O_RDONLY);
	if (urandomDescriptor == -1) {
		perror("pcc_client: Could not open /dev/urandom");
		return errno;
	}

	int toRead;
	char* sendBuffer;
	do {
		toRead = length > BUFFER_SIZE ? BUFFER_SIZE : length;
		length -= toRead;
		if (read(urandomDescriptor, buffer, toRead) != toRead) {
			perror("pcc_client: Could not read from /dev/urandom");
			return errno;
		}

		sendBuffer = buffer;
		do {
			if ((sent = write(sock, sendBuffer, toRead)) < 0) {
				perror("pcc_client: Could not send message to server");
				return errno;
			}
			
			sendBuffer += sent;
			toRead -= sent;
		} while (toRead > 0);
	} while (length > 0);

	uint32_t C;
	char* ret = (char*) &C;
	size = sizeof(C);
	do {
		if ((sent = read(sock, ret, size)) < 0) {
			perror("pcc_client: Could not read message from server");
			return errno;
		}
		
		ret += sent;
		size -= sent;
	} while (size > 0);

	printf("# of printable characters: %u\n", ntohl(C));

	close(urandomDescriptor);
	close(sock);
	free(buffer);

	return 0;
}
