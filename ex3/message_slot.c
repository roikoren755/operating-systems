#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE

#include "message_slot.h"
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");

typedef struct linked_list_node_t {
	char channels[4][MAXIMUM_MESSAGE_LENGTH];
	int written[4];
	unsigned int minor;
	struct linked_list_node_t* next;
} LinkedListNode;

typedef struct linked_list_t {
	LinkedListNode* head;
} LinkedList;

LinkedList* devices;

/***
 * Given a pointer to the last node in a list, adds a new node, with the given minor
 * number, and resets it to the starting, untouched state.
 */
int initializeNode(LinkedListNode* node, int minor) {
	if (!node) { // Nowhere to add the node!
		printk(KERN_ALERT "message_slot: ERROR - tried to initialize a null device node\n");
		return -EINVAL;
	}

	node->next = (LinkedListNode*) kmalloc(sizeof(LinkedListNode), GFP_KERNEL);
	if (!node->next) { // Could not allocate
		printk(KERN_ALERT "message_slot: ERROR - allocating memory for %d\n", minor);
		return -ENOMEM;
	}

	node = node->next;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < MAXIMUM_MESSAGE_LENGTH; j++) {
			node->channels[i][j] = 0; // Clean channels
		}
		node->written[i] = -1; // Set to "not written to"
	}

	node->minor = minor; // Set minor number

	return 0;
}

static int device_open(struct inode* inode, struct file* file) {
	int result;
	int minor;
	LinkedListNode* currentNode;

	if (!inode || !file) { // What did you send me?
		printk(KERN_ALERT "message_slot: ERROR - illegal arguments passed to device_open\n");
		return -EINVAL;
	}

	minor = iminor(inode);
	currentNode = devices->head;

	if (!currentNode->next) { // First device to be opened
		if ((result = initializeNode(currentNode, minor))) {
			return result;
		}
		else {
			file->private_data = (void*) -1;
			return 0;
		}
	}

	currentNode = currentNode->next; // Not first
	while (currentNode->minor != minor) { // Find the right struct
		if (currentNode->next) {
			currentNode = currentNode->next;
		}
		else { // Got to the end and didn't find minor, so open new node
			if ((result = initializeNode(currentNode, minor))) {
				return result;
			}
		}
	}

	file->private_data = (void*) -1;

	return 0;
}

static int device_release(struct inode* inode, struct file* file) {
	return 0;
}

static ssize_t device_read(struct file* file, char __user* buffer, size_t length, loff_t* offset) {
	int minor;
	int channel;
	int messageLength;
	LinkedListNode* currentNode;

	if (!file || !buffer) { // WHAT WHAT WHAAAATTT???
		printk(KERN_ALERT "message_slot: ERROR - illegal arguments passed to device_read\n");
		return -EINVAL;
	}

	minor = iminor(file_inode(file));
	channel = (int) file->private_data;

	if (channel == -1) { // What it is initialized to on opening, means no ioctl yet
		printk(KERN_ALERT "message_slot: ERROR - tried reading, but no channel set for %d\n", minor);
		return -EINVAL;
	}

	currentNode = devices->head->next;
	while (currentNode && currentNode->minor != minor) { // Find the correct node
		currentNode = currentNode->next;
	}

	if (!currentNode) { // Minor not found
		printk(KERN_ALERT "message_slot: ERROR - tried reading, but %d hasn't been opened\n", minor);
		return -EINVAL;
	}

	if (currentNode->written[channel] == -1) { // Channel hadn't been written to yet
		printk(KERN_ALERT "message_slot: ERROR - tried to read from %d before writing a message to it\n", minor);
		return -EWOULDBLOCK;
	}

	if (length < (messageLength = currentNode->written[channel])) { // Buffer is too small!
		printk(KERN_ALERT "message_slot: ERROR - tried to read from %d, but buffer given contains %d bytes, less than the message's length - %d bytes\n", minor, length, currentNode->written[channel]);
		return -ENOSPC;
	}

	for (int i = 0; i < messageLength; i++) { // Give them what they want!
		if (put_user(currentNode->channels[channel][i], buffer + i)) { // Oops...
			printk(KERN_ALERT "message_slot: ERROR - while trying to pass message to user, for %d\n", minor);
			return -EFAULT;
		}
	}

	return messageLength; // Got it!
}

static ssize_t device_write(struct file* file, const char __user* buffer, size_t length, loff_t* offset) {
	int minor;
	int channel;
	LinkedListNode* currentNode;

	if (!file || !buffer) { // Come again?
		printk(KERN_ALERT "message_slot: ERROR - illegal arguments passed to device_write\n");
		return -EINVAL;
	}

	minor = iminor(file_inode(file));
	channel = (int) file->private_data;
	
	if (channel == -1) { // No channel set
		printk(KERN_ALERT "message_slot: ERROR - tried to write, but no channel has been set for %d\n", minor);
		return -EINVAL;
	}

	if (length > MAXIMUM_MESSAGE_LENGTH) { // Message is too long!
		printk(KERN_ALERT "message_slot: ERROR - tried to write a message that contains %d bytes, more than %d bytes to %d\n", length, MAXIMUM_MESSAGE_LENGTH, minor);
		return -EINVAL;
	}

	currentNode = devices->head->next;
	while (currentNode) { // Find correct node
		if (currentNode->minor != minor) {
			currentNode = currentNode->next;
		}
		else { // Found it!
			for (int i = 0; i < length; i++) {
				if (get_user(currentNode->channels[channel][i], buffer + i)) { // Oops...
					printk(KERN_ALERT "message_slot: ERROR - while trying to get message from user, for %d\n", minor);
					currentNode->written[channel] = 0;
					return -EFAULT;
				}
			}

			currentNode->written[channel] = length; // Update length for channel
			return length;
		}
	}

	// Means minor wasn't found!
	printk(KERN_ALERT "message_slot: ERROR - tried to write to %d, but it wasn't opened!\n", minor);
	return -EINVAL;
}

static long device_ioctl(struct file* file, unsigned int ioctl_command_id, unsigned long ioctl_param) {
	if (!file) { // Huh?
		printk(KERN_ALERT "message_slot: ERROR - illegal arguments passed to device_ioctl\n");
		return -EINVAL;
	}

	if (ioctl_command_id != MSG_SLOT_CHANNEL) { // Blasphemy!
		printk(KERN_ALERT "message_slot: ERROR - illegal ioctl command passed for %d\n", iminor(file_inode(file)));
		return -EINVAL;
	}

	if (ioctl_param < 0 || ioctl_param > 3) { // What is this channel you speak of?
		printk(KERN_ALERT "message_slot: ERROR - channel given (%ld) for %d isn't valid\n", ioctl_param, iminor(file_inode(file)));
		return -EINVAL;
	}

	file->private_data = (void*) ioctl_param; // Aaaahhhh.... Got it ;)
	return 0;
}

struct file_operations Fops = {
	.read = device_read,
	.write = device_write,
	.open = device_open,
	.unlocked_ioctl = device_ioctl,
	.release = device_release,
};

static int __init device_init(void) {
	devices = (LinkedList*) kmalloc(sizeof(LinkedList), GFP_KERNEL); // First - a list
	if (!devices) {
		printk(KERN_ALERT "message_slot: ERROR - could not allocate memory for device driver!\n");
		return -ENOMEM;
	}

	devices->head = (LinkedListNode*) kmalloc(sizeof(LinkedListNode), GFP_KERNEL); // Then - a sentinel/head!
	if (!devices->head) {
		printk(KERN_ALERT "message_slot: ERROR - could not allocate memory for device driver!\n");
		return -ENOMEM;
	}

	if (register_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME, &Fops)) { // Register character device
		printk(KERN_ALERT "message_slot: ERROR - could not register device driver!\n");
		kfree(devices);
		return -EFAULT;
	}

	printk(KERN_INFO "message_slot: registered major number %d\n", MAJOR_NUM);
	return 0;
}

static void __exit device_cleanup(void) {
	if (devices) { // Safety first!
		LinkedListNode* currentNode = devices->head;
		do { // Clean up list nodes
			devices->head = currentNode->next;
			kfree(currentNode);
			currentNode = devices->head;
		} while (currentNode);
	}
	
	kfree(devices); // Clean up list
	unregister_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME); // Unregister character device
	printk(KERN_INFO "message_slot: successfully removed module\n");
}

module_init(device_init);
module_exit(device_cleanup);
