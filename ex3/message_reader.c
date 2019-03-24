#include "message_slot.h"
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
	int fileDescriptor = open(argv[1], O_RDONLY);
	if (fileDescriptor < 0) {
		printf("ERROR - could not open %s\n", argv[1]);
		return -1;
	}

	int channel = atoi(argv[2]);
	int ioctlReturnValue = ioctl(fileDescriptor, MSG_SLOT_CHANNEL, channel);
	if (ioctlReturnValue < 0) {
		printf("ERROR - could not set channel %d for %s\n", channel, argv[1]);
		close(fileDescriptor);
		return -1;
	}

	char buffer[MAXIMUM_MESSAGE_LENGTH + 1];
	int readBytes = read(fileDescriptor, buffer, MAXIMUM_MESSAGE_LENGTH);
	if (readBytes < 0) {
		printf("ERROR - could not read message from %s\n", argv[1]);
		close(fileDescriptor);
		return -1;
	}

	buffer[readBytes] = '\0';
	close(fileDescriptor);
	printf("%s\n", buffer);
	printf("%d bytes read from %s\n", readBytes, argv[1]);

	return 0;
}
