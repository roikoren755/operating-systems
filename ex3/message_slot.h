#ifndef _MESSAGE_SLOT_H
#define _MESSAGE_SLOT_H

#include <asm/ioctl.h>

#define MAJOR_NUM 244
#define DEVICE_RANGE_NAME "message_slot"
#define MSG_SLOT_CHANNEL _IOW(MAJOR_NUM, 0, unsigned long)
#define MAXIMUM_MESSAGE_LENGTH 128

#endif
