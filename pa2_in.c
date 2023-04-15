/**
 * File:	pa2_in.c
 * Adapted for Linux 5.15 by: John Aedo
 * Forked by: Jake Norris and D'Antae Aronne
 * Class:	COP4600-SP23
 */

#include <linux/module.h>	  // Core header for modules.
#include <linux/device.h>	  // Supports driver model.
#include <linux/kernel.h>	  // Kernel header for convenient functions.
#include <linux/fs.h>		  // File-system support.
#include <linux/uaccess.h>	  // User access copy function support.
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/mutex.h>

#define DEVICE_NAME "pa2_in" // Device name.
#define CLASS_NAME "char_in"	  ///< The device class -- this is a character device driver
#define SUCCESS 0
#define BUF_LEN 1024          // Max length of a message

MODULE_LICENSE("GPL");						 ///< The license type -- this affects available functionality
MODULE_AUTHOR("John Aedo");					 ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("pa2_in Kernel Module"); ///< The description -- see modinfo
MODULE_VERSION("0.1");						 ///< A version number to inform users

/**
 * Important variables that store data and keep track of relevant information.
 */
static int major_number; // Stores the major number of the device driver
static int device_open = 0; // Boolean to track whether the device is open

static struct class *pa2_inClass = NULL;	///< The device-driver class struct pointer
static struct device *pa2_inDevice = NULL; ///< The device-driver device struct pointer

static struct msgs
{
    char *msg;
    int msg_size;
    struct msgs *next;
};

struct queue
{
    struct msgs *top;
    struct msgs *bottom;
}*q;
EXPORT_SYMBOL(q);

DEFINE_MUTEX(pa2_mutex);
EXPORT_SYMBOL(pa2_mutex);

DECLARE_WAIT_QUEUE_HEAD(wq);
EXPORT_SYMBOL(wq);

//static char msg[BUF_LEN]; // Message the device will give when asked
//static int msg_size; // Size of the message written to the device
int all_msg_size;// Size of all the messages written to the device
EXPORT_SYMBOL(all_msg_size);

/**
 * Prototype functions for file operations.
 */
static int open(struct inode *, struct file *);
static int close(struct inode *, struct file *);
static ssize_t write(struct file *, const char *, size_t, loff_t *);

/**
 * File operations structure and the functions it points to.
 */
static struct file_operations fops =
	{
		.owner = THIS_MODULE,
		.open = open,
		.release = close,
		.write = write,
};


/**
 * Initializes module at installation
 */
int init_module(void)
{
	printk(KERN_INFO "pa2_in: installing module.\n");

	// Allocate a major number for the device.
	major_number = register_chrdev(0, DEVICE_NAME, &fops);
	if (major_number < 0)
	{
		printk(KERN_ALERT "pa2_in could not register number.\n");
		return major_number;
	}
	printk(KERN_INFO "pa2_in: registered correctly with major number %d\n", major_number);

	// Register the device class
	pa2_inClass = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(pa2_inClass))
	{ // Check for error and clean up if there is
		unregister_chrdev(major_number, DEVICE_NAME);
		printk(KERN_ALERT "Failed to register device class\n");
		return PTR_ERR(pa2_inClass); // Correct way to return an error on a pointer
	}
	printk(KERN_INFO "pa2_in: device class registered correctly\n");

	// Register the device driver
	pa2_inDevice = device_create(pa2_inClass, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
	if (IS_ERR(pa2_inDevice))
	{								 // Clean up if there is an error
		class_destroy(pa2_inClass); // Repeated code but the alternative is goto statements
		unregister_chrdev(major_number, DEVICE_NAME);
		printk(KERN_ALERT "Failed to create the device\n");
		return PTR_ERR(pa2_inDevice);
	}

	// Initialize the mutex lock
	mutex_init(&pa2_mutex);

	printk(KERN_INFO "pa2_in: device class created correctly\n"); // Made it! device was initialized

	return SUCCESS;
}

/*
 * Removes module, sends appropriate message to kernel
 */
void cleanup_module(void)
{
	printk(KERN_INFO "pa2_in: removing module.\n");
	device_destroy(pa2_inClass, MKDEV(major_number, 0)); // remove the device
	class_unregister(pa2_inClass);						  // unregister the device class
	class_destroy(pa2_inClass);						  // remove the device class
	unregister_chrdev(major_number, DEVICE_NAME);		  // unregister the major number
	printk(KERN_INFO "pa2_in: Goodbye from the LKM!\n");
	unregister_chrdev(major_number, DEVICE_NAME);
	kfree(q);
	return;
}

/*
 * Opens device module, sends appropriate message to kernel
 */
static int open(struct inode *inodep, struct file *filep)
{
	// Return an error if the device is already open, and report to the kernel.
	if (device_open)
	{
		printk(KERN_INFO "pa2_in: device is busy.\n");
		return -EBUSY;
	}

	/*---------- Critical Section Start ----------*/
	printk(KERN_INFO "%s: entering critical section", DEVICE_NAME);
	if(!mutex_trylock(&pa2_mutex))
	{
		printk(KERN_ALERT "%s: device is in use by another process, waiting for it to finish", DEVICE_NAME);
		wait_event_interruptible(wq, mutex_trylock(&pa2_mutex));
	}

	printk(KERN_INFO "%s: acquired lock\n", DEVICE_NAME);

	q = kmalloc(sizeof(struct queue), GFP_KERNEL);

	all_msg_size = 0;

	// Increment to indicate we have now opened the device
	device_open++;

	// Return success upon opening the device without error, and report to the kernel.
	printk(KERN_INFO "pa2_in: device opened.\n");
	return SUCCESS;
}

/*
 * Closes device module, sends appropriate message to kernel
 */
static int close(struct inode *inodep, struct file *filep)
{
	// Decrement to indicate the device is now closed
	device_open--;

	mutex_unlock(&pa2_mutex);
	wake_up(&wq);
	printk(KERN_INFO "%s: released lock\n", DEVICE_NAME);

	printk(KERN_INFO "%s: exiting critical section", DEVICE_NAME);
	/*---------- Critical Section End ----------*/

	// Return success upon opening the device without error, and report it to the kernel.
	printk(KERN_INFO "pa2_in: device closed.\n");
	return SUCCESS;
}

/*
 * Writes to the device. This whole function is encapsulated in the critical section.
 */
static ssize_t write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
	// If the file is larger than the amount of bytes the device can hold, return an error.
	int remaining_bytes = -1;
	if ((len + all_msg_size) > BUF_LEN)
	{
		remaining_bytes = BUF_LEN - all_msg_size;
	}
	
	if (all_msg_size == 0){
		q->top = NULL;
		q->bottom = NULL;
	}
	
	if (all_msg_size >= BUF_LEN){
		q->top = NULL;
		q->bottom = NULL;
		printk(KERN_INFO "%s: Buffer is full", DEVICE_NAME);	
	}

	// Write the input to the device, and update the length of the message.
	// Work as a FIFO queue, so that multiple messages can be stored.
	struct msgs *ptr = kmalloc(sizeof(struct msgs), GFP_KERNEL);

	if (remaining_bytes == -1)
	{
		int msg_mem_size = (len + 1) * sizeof(char);
		ptr->msg = kmalloc(msg_mem_size, GFP_KERNEL);
		sprintf(ptr->msg, "%s", buffer);
		ptr->msg_size = len + 1;
		all_msg_size += len +1;
		printk(KERN_INFO "%s: Writing %d bytes", DEVICE_NAME, len + 1);
	}
	else
	{
		int msg_mem_size = (remaining_bytes + 1) * sizeof(char);
		ptr->msg = kmalloc(msg_mem_size, GFP_KERNEL);
		sprintf(ptr->msg, "%.*s", remaining_bytes, buffer);
		ptr->msg_size = remaining_bytes + 1;
		all_msg_size += remaining_bytes + 1;
		printk(KERN_INFO "%s: Writing %d bytes", DEVICE_NAME, remaining_bytes + 1);
		printk(KERN_INFO "%s: Not enough space left in the buffer, dropping the rest.", DEVICE_NAME);
		
	}


	ptr->next=NULL;

	if (q->top==NULL && q->bottom==NULL)
	{
		q->top = q->bottom = ptr;
	}
	else
	{
		q->bottom->next=ptr;
		q->bottom=ptr;
	}

	// Return success upon writing the message to the device without error, and report it to the kernel.
	printk(KERN_INFO "pa2_in: write stub");
	return len;
}
