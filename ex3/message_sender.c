#include "message_slot.h"
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
	int fileDescriptor = open(argv[1], O_WRONLY);
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

	int length = strlen(argv[3]);
	int writtenBytes = write(fileDescriptor, argv[3], length);
	if (writtenBytes < 0) {
		printf("ERROR - something went wrong while writting to %s\n", argv[1]);
		close(fileDescriptor);
		return -1;
	}

	close(fileDescriptor);
	printf("%d bytes written to %s\n", writtenBytes, argv[1]);

	return 0;
}
