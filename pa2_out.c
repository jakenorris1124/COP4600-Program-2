/**
 * File:	pa2_out.c
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
#include "pa2_in.c"

#define DEVICE_NAME "pa2_out" // Device name.
#define CLASS_NAME "char"	  ///< The device class -- this is a character device driver
#define SUCCESS 0
#define BUF_LEN 1024          // Max length of a message

MODULE_LICENSE("GPL");						 ///< The license type -- this affects available functionality
MODULE_AUTHOR("John Aedo");					 ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("pa2_out Kernel Module"); ///< The description -- see modinfo
MODULE_VERSION("0.1");						 ///< A version number to inform users

/**
 * Important variables that store data and keep track of relevant information.
 */
static int major_number; // Stores the major number of the device driver
static int device_open = 0; // Boolean to track whether the device is open

static struct class *pa2_outClass = NULL;	///< The device-driver class struct pointer
static struct device *pa2_outDevice = NULL; ///< The device-driver device struct pointer

extern struct queue *q;
extern static int all_msg_size; // Size of all the messages written to the device
extern struct mutex pa2_mutex;
extern wait_queue_head_t wq;
/**
 * Prototype functions for file operations.
 */
static int open(struct inode *, struct file *);
static int close(struct inode *, struct file *);
static ssize_t read(struct file *, char *, size_t, loff_t *);

/**
 * File operations structure and the functions it points to.
 */
static struct file_operations fops =
	{
		.owner = THIS_MODULE,
		.open = open,
		.release = close,
		.read = read,
		.write = write,
};

/**
 * Initializes module at installation
 */
int init_module(void)
{
	printk(KERN_INFO "pa2_out: installing module.\n");

	// Allocate a major number for the device.
	major_number = register_chrdev(0, DEVICE_NAME, &fops);
	if (major_number < 0)
	{
		printk(KERN_ALERT "pa2_out could not register number.\n");
		return major_number;
	}
	printk(KERN_INFO "pa2_out: registered correctly with major number %d\n", major_number);

	// Register the device class
	pa2_outClass = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(pa2_outClass))
	{ // Check for error and clean up if there is
		unregister_chrdev(major_number, DEVICE_NAME);
		printk(KERN_ALERT "Failed to register device class\n");
		return PTR_ERR(pa2_outClass); // Correct way to return an error on a pointer
	}
	printk(KERN_INFO "pa2_out: device class registered correctly\n");

	// Register the device driver
	pa2_outDevice = device_create(pa2_outClass, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
	if (IS_ERR(pa2_outDevice))
	{								 // Clean up if there is an error
		class_destroy(pa2_outClass); // Repeated code but the alternative is goto statements
		unregister_chrdev(major_number, DEVICE_NAME);
		printk(KERN_ALERT "Failed to create the device\n");
		return PTR_ERR(pa2_outDevice);
	}
	printk(KERN_INFO "pa2_out: device class created correctly\n"); // Made it! device was initialized

	return SUCCESS;
}

/*
 * Removes module, sends appropriate message to kernel
 */
void cleanup_module(void)
{
	printk(KERN_INFO "pa2_out: removing module.\n");
	device_destroy(pa2_outClass, MKDEV(major_number, 0)); // remove the device
	class_unregister(pa2_outClass);						  // unregister the device class
	class_destroy(pa2_outClass);						  // remove the device class
	unregister_chrdev(major_number, DEVICE_NAME);		  // unregister the major number
	printk(KERN_INFO "pa2_out: Goodbye from the LKM!\n");
	unregister_chrdev(major_number, DEVICE_NAME);
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
		printk(KERN_INFO "pa2_out: device is busy.\n");
		return -EBUSY;
	}

	/*---------- Critical Section Start ----------*/
	if (q == NULL)
	{
		printk(KERN_INFO "pa2_out: input device has not been intialized.")
		return -ESRCH
	}
	/*---------- Critical Section End ----------*/
	// Increment to indicate we have now opened the device
	device_open++;

	// Return success upon opening the device without error, and report to the kernel.
	printk(KERN_INFO "pa2_out: device opened.\n");
	return SUCCESS;
}

/*
 * Closes device module, sends appropriate message to kernel
 */
static int close(struct inode *inodep, struct file *filep)
{
	// Decrement to indicate the device is now closed
	device_open--;

	// Return success upon opening the device without error, and report it to the kernel.
	printk(KERN_INFO "pa2_out: device closed.\n");

	/*---------- Critical Section Start ----------*/
	kfree(q);
	/*---------- Critical Section End ----------*/
	return SUCCESS;
}

/*
 * Reads from device, displays in userspace, and deletes the read data
 */
static ssize_t read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
	// Send the message to user space, and store the number of bytes that could not be copied
	// On success, this should be zero.
	/*---------- Critical Section Start ----------*/
	int uncopied_bytes = copy_to_user(buffer, q->top->msg, q->top->msg_size);
	/*---------- Critical Section End ----------*/
	struct msgs *ptr = kmalloc(sizeof(struct msgs), GFP_KERNEL);

	// If the message was successfully sent to user space, report this
	// to the kernel and return success.
	if (uncopied_bytes == 0)
	{
		/*---------- Critical Section Start ----------*/
		if (q->top != NULL)
		{
			ptr = q->top;
			q->top = q->top->next;
			if (q->top == NULL)
			{
				q->bottom = NULL;
			}
			ptr->next = NULL;
      			all_msg_size -= ptr->msg_size;
			kfree(ptr);
		}
		/*---------- Critical Section End ----------*/		
		printk(KERN_INFO "pa2_out: read stub");
		return SUCCESS;
	}

	// Return with an error indicating bad address if we cannot copy the message to user space.
	printk(KERN_INFO "pa2_out: failed to read stub");
	return -EFAULT;
}