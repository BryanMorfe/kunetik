/**
 * Sample Kunetik Device Linux Module Driver
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/random.h>

#include <stdbool.h>

#include "ukunetik.h"

MODULE_DESCRIPTION("Kunetik Device Driver");
MODULE_AUTHOR("Bryan Morfe");
MODULE_LICENSE("GPL");
MODULE_VERSION("v0.1");

#define KUNETIK_DEV_CLASS "kunetik_class"
#define KUNETIK_DEV_NAME  "kunetik"

/**
 * Kunetik device structure
 */
struct kunetik_dev
{
    /* IOCTL & Functionality */
    struct kunetik_temp_type temp_type;

    /* For simulation purposes */
    __u8 max_temp;
    __u8 min_temp;

    /* Character Device */
    dev_t          devt;       /* Device Major and Minor numbers */
    struct class*  dev_class;  /* Device class (/sys/class/)     */
    struct device* device;     /* Basic device structure         */
    struct cdev    cdev;       /* Struct of Character DEVice     */

    /* Device Management */
    wait_queue_head_t waitqueue;
    bool              is_ready;
    bool              in_use;
    __u8*             device_data;
    __u8              data_size;
};

/**
 * Make sure to make your globals static to not conflict with other kernel globals
 */
static struct kunetik_dev* kdevp;

static int kdev_open(struct inode* node, struct file* file)
{
    struct kunetik_dev* kdev = container_of(node->i_cdev, struct kunetik_dev, cdev);
    
    if (kdev->in_use)
        return -EBUSY;

    file->private_data = kdev;
    kdev->in_use = true;

    return 0;
}

static int kdev_release(struct inode* node, struct file* file)
{
    struct kunetik_dev* kdev = file->private_data;

    if (!kdev->in_use)
        return -EINVAL;

    kdev->in_use   = false;

    return 0;
}

/**
 *  The followin constants & functions are for simulation purposes
 */
#define KTK_MIN_TEMP_CELCIUS    0x00
#define KTK_MAX_TEMP_CELCIUS    0x28
#define KTK_MIN_TEMP_FAHRENHEIT 0x20
#define KTK_MAX_TEMP_FAHRENHEIT 0x78

#define __normalize(_x, _max, _min) ((_x) % (1 + (_max) - (_min)) + (_min))

/**
 * This function is called when a user makes a "read"
 * system call.
 * Note the __user macro used in the buffer. This is used
 * to help the identification of the virtual address (whether
 * kernel space (__kernel), user space (__user) or it is IO
 * memory (__iomem), like a mapped FPGA's address space). If
 * you're unsure when to use them, then don't use them at all.
 */
static ssize_t kdev_read(struct file* file, char __user* ubuf, size_t len, loff_t* offset)
{
    struct kunetik_dev* kdev = file->private_data;
    ssize_t             ret  = 0;

    /*
     * Everytime you read, you ask the device to capture
     * data and send it.
     * 
     * Here, the driver would write to the device (maybe to
     * registers, etc.) and wait for the device to fill data
     */
    if (!kdev->is_ready)
    {
        if ((file->f_flags & O_NONBLOCK) > 0)
            return -EAGAIN;
    
        // If the user didn't set the O_NONBLOCK flag, then this will block
        // until the device is ready (and we have read data from it)
        // Here, normally an interrupt handler would set the is_ready flag and
        // wake up the wait queue (check ktk_capture_data to see the simulation).
        if (wait_event_interruptible(kdev->waitqueue, kdev->is_ready))
            return -ERESTARTSYS;
    }

    if (len > kdev->data_size)
        len = kdev->data_size;

    if (copy_to_user(ubuf, kdev->device_data, len))
        return -EFAULT;

    ret = len;

    return ret;
}

/**
 * This function is called when a user makes a "write"
 * system call.
 * Note the __user macro used in the buffer. This is used
 * to help the identification of the virtual address (whether
 * kernel space (__kernel), user space (__user) or it is IO
 * memory (__iomem), like a mapped FPGA's address space). If
 * you're unsure when to use them, then don't use them at all.
 */
static ssize_t kdev_write(struct file* file, const char __user* ubuf, size_t len, loff_t* offset)
{
    return -EFAULT;  // The Kunetik device does not support write
}

static int ktk_set_temp_type(struct kunetik_dev* kdev, struct kunetik_temp_type* ltt)
{
    int rv = 0;
    switch (ltt->type)
    {
    case KTK_TEMP_TYPE_CELCIUS:
        kdev->max_temp = KTK_MAX_TEMP_CELCIUS;
        kdev->min_temp = KTK_MIN_TEMP_CELCIUS;
        kdev->temp_type.type = ltt->type;
        break;
    case KTK_TEMP_TYPE_FAHRENHEIT:
        kdev->max_temp = KTK_MAX_TEMP_FAHRENHEIT;
        kdev->min_temp = KTK_MIN_TEMP_FAHRENHEIT;
        kdev->temp_type.type = ltt->type;
        break;
    default:
        rv = -EINVAL;
        break;
    }

    return rv;
}

static int ktk_capture_data(struct kunetik_dev* kdev)
{
    kdev->is_ready = false; // with this implementation, you can always read the last
                            // captured data until the user requests new data, so is_ready
                            // is always true unless new data is requested or it was never
                            // requested to begin with.

    /*
     * This is a simulation. Normally, what you'd really do here is communicate
     * with the Kunetik device and ask it to capture data, then it'd send
     * an interrupt or you'd pull a bit until ready. When the interrupt arrives
     * (or you pull the bit until set), then you set the kdev->is_ready to true
     */
    kdev->device_data[KTK_TEMPTYPE_OFFSET] = kdev->temp_type.type;
    get_random_bytes(kdev->device_data + KTK_TEMP_OFFSET, 2);  // generate temperature and humidity
    kdev->device_data[KTK_TEMP_OFFSET] = 
        __normalize(kdev->device_data[KTK_TEMP_OFFSET], kdev->max_temp, kdev->min_temp);

    kdev->is_ready = true;
    wake_up_interruptible(&kdev->waitqueue);  // this wakes up the queue if it sleeping
    return 0;
}

/**
 * This function is called when a user makes an "ioctl"
 * system call. Note that arg is an unsigned long. It is
 * simply an address in the form of an integer. You should
 * be able to cast it to a pointer like it's done in these
 * functions.
 */
static long kdev_ioctl(struct file* file, unsigned int cmd, unsigned long arg)
{
    struct kunetik_dev*      kdev = file->private_data;
    int                      rv   = 0;
    struct kunetik_temp_type temp_type;

    switch (cmd)
    {
    case KTK_SET_TEMP_TYPE:
        if (copy_from_user(&temp_type, (struct kunetik_temp_type*)arg, sizeof(temp_type)))
            rv = -EFAULT;

        rv = ktk_set_temp_type(kdev, &temp_type);
        break;
    case KTK_GET_TEMP_TYPE:
        if (copy_to_user((struct kunetik_temp_type*)arg, &kdev->temp_type, sizeof(kdev->temp_type)))
            rv = -EFAULT;
        break;
    case KTK_CAPTURE_DATA:
        rv = ktk_capture_data(kdev);
        break;
    default:
        rv = -EINVAL;
        break;
    }

    return rv;
}

/**
 * This file_operations structure is what allows you
 * to define the POSIX-complaint functionality of your
 * character device file. (open/close/read/write/ioctl, etc.)
 * Note that the arguments these function take aren't the same
 * that your read/write/open/close/ioctl functions take, and
 * that's because these are the kernel versions of those. The
 * kernel will be in charge of translating those system calls
 * into these functions. Also, note that these defined function
 * pointers aren't all that are available. Check the
 * file_operations structure for more information.
 */
static struct file_operations fops = {
    .owner          = THIS_MODULE,
    .open           = kdev_open,
    .release        = kdev_release,
    .read           = kdev_read,
    .write          = kdev_write,
    .unlocked_ioctl = kdev_ioctl
};

static int kdev_init(struct kunetik_dev* kdev)
{
    int ret;

    /* 
     * Creates a class in /sys/class/ for the device.
     */
    kdev->dev_class = class_create(THIS_MODULE, KUNETIK_DEV_CLASS);

    if (IS_ERR_OR_NULL(kdev->dev_class))
    {
        ret = -ENOMEM;
        pr_alert("kunetik: failed to create class for kunetik device (%d)\n", ret);
        goto CLASS_CREATE_FAILED;
    }

    /**
     * Asks the kernel for a major and minor numbers for the device
     * Counter. Replacing "alloc" with "register" _tells_ the kernel
     * the major and minor numbers you want to use (not recommended).
     * unregister_* tells the kernel you are done with those numbers.
     */
    ret = alloc_chrdev_region(&kdev->devt, 0, 1, KUNETIK_DEV_NAME);

    if (ret)
    {
        pr_alert("kunetik: failed to allocate character device regions (%d)\n", ret);
        goto ALLOC_CHRDEV_REGION_FAILED;
    }

    /* 
     * Initializes your character device structure
     */
    cdev_init(&kdev->cdev, &fops);

    /* 
     * Adds (registers) your character device (not the "physical" node in /dev/) to the system
     */
    ret = cdev_add(&kdev->cdev, MKDEV(MAJOR(kdev->devt), 0), 1);

    if (ret)
    {
        pr_alert("kunetik: failed to add device to system (%d)\n", ret);
        goto CDEV_ADD_FAILED;
    }
    
    /* 
     * Initializes your basic device structure and creates the device node (in /dev/)
     */
    kdev->device = device_create(kdev->dev_class, NULL, MKDEV(MAJOR(kdev->devt), 0), NULL, KUNETIK_DEV_NAME);

    if (IS_ERR_OR_NULL(kdev->device))
    {
        ret = -ENOMEM;
        pr_alert("kunetik: failed to create device node for kunetik device (%d)\n", ret);
        goto DEVICE_CREATE_FAILED;
    }

    kdev->temp_type.type = KTK_TEMP_TYPE_CELCIUS;  // start with celcius, here you'd tell the device too
    kdev->is_ready       = false;
    kdev->in_use         = false;
    init_waitqueue_head(&kdev->waitqueue);

    /* Simulation */
    kdev->max_temp    = KTK_MAX_TEMP_CELCIUS;
    kdev->min_temp    = KTK_MIN_TEMP_CELCIUS;
    kdev->data_size   = KTK_DATA_SIZE;
    kdev->device_data = kzalloc(kdev->data_size, GFP_KERNEL);

    if (kdev->device_data == NULL)
    {
        ret = -ENOMEM;
        printk(KERN_ALERT "kunetik: failed to allocate device data for simulation\n");
        goto ALLOC_DEVICE_DATA_FAILED;
    }

    return 0;

ALLOC_DEVICE_DATA_FAILED:
    device_destroy(kdev->dev_class, kdev->devt);  // Counterpart to device_create
DEVICE_CREATE_FAILED:
    cdev_del(&kdev->cdev);                        // Counterpart to cdev_add
CDEV_ADD_FAILED:
    unregister_chrdev_region(kdev->devt, 1);      // Counterpart to (alloc|register_chrdev_region)
ALLOC_CHRDEV_REGION_FAILED:
    class_destroy(kdev->dev_class);               // Counterpart to class_create
CLASS_CREATE_FAILED:
    return ret;
}

static void kdev_deinit(struct kunetik_dev* kdev)
{
    kfree(kdev->device_data);

    cdev_del(&kdev->cdev);                        // Counterpart to cdev_add
    device_destroy(kdev->dev_class, kdev->devt);  // Counterpart to device_create
    unregister_chrdev_region(kdev->devt, 1);      // Counterpart to (alloc|register_chrdev_region)
    class_destroy(kdev->dev_class);               // Counterpart to class_create
}

/**
 * The init function is the _first_ function that gets called
 * when your module is inserted. Think of it of your "main" function,
 * though your module instance will outlive its execution, unlike a user program.
 * Use this function to initialize everything your module requires to perform its
 * functions.
 * This function returns an integer that corresponds to an error code (look at the
 * unix/linux error codes. You'll want to return the negative counter parts of the
 * error codes to easily indicate it is an error status, and return 0 only on
 * success.
 * Note: The init function is usually marked with the __init macro. These tell the
 * compiler where to allocate this function. This should only be used for your
 * module's entry point. If you're unsure when to use it, it's best not to use it
 * at all.
 */
static int __init kunetik_init(void)
{
    int rv;

    /*
     * kzalloc is the kernel version of calloc (allocates and sets to 0x00)
     * kmalloc is the kernel version of malloc (only allocates)
     * kfree is the kernel version of free (deallocates)
     * The gfp_flags are special flags that tell the kernel "how to" allocate
     * your memory. For the most part you'll be using GFP_KERNEL, but here's
     * a list of flags with their descriptions:
     *   - https://www.kernel.org/doc/html/latest/core-api/memory-allocation.html
     */
    kdevp = kzalloc(sizeof(struct kunetik_dev), GFP_KERNEL);

    /*
     * printk is the kernel version of printf, however, unlike printf, the buffer
     * in printk doesn't go to stdout, rather to the kernel buffer. To view the
     * kernel buffer, type "dmesg" on the terminal. Notice that printk functions
     * have a MACRO right before the format of the message, which is one of these:
     *  - KERN_EMERG
     *  - KERN_ALERT
     *  - KERN_CRIT
     *  - KERN_ERR
     *  - KERN_WARNING
     *  - KERN_NOTICE
     *  - KERN_INFO
     *  - KERN_DEBUG   (requires DEBUG to be defined (look at the Makefile to see that it is defining it))
     *  - KERN_DEFAULT
     *  - KERN_CONT
     * These are the log levels of the kernel. For a description, visit
     * https://www.kernel.org/doc/html/latesupervisor read access in kernel modest/core-api/printk-basics.html and look
     * at the aliases (pr_emerg(), pr_alert(), etc. I opted not to use them for
     * the benefit of the reader).
     * Finally, you may notice that the fmt argument always starts with "kunetik: ".
     * The reason is because `dmesg` will use these as a "messenger" and color it
     * so you can easily identify the messages specifically from this module.
     */

    if (kdevp == NULL)
    {
        rv = -ENOMEM;
        printk(KERN_ALERT "kunetik: failed to allocate kunetik device due to lack of memory\n");
        goto ALLOC_FAILED;
    }

    rv = kdev_init(kdevp);

    if (rv)
    {
        printk(KERN_ALERT "kunetik: failed to init kdev\n");
        goto KDEV_INIT_FAILED;
    }

    printk(KERN_INFO "kunetik: module loaded\n");

    return 0;
    
KDEV_INIT_FAILED:
    kfree(kdevp);
ALLOC_FAILED:
    return rv;
}

/**
 * The exit function is the counterpart of the init function, and it is called
 * when the driver is to be removed. It should be used to undo what _requires_
 * undoing before the driver is completely removed (such as removing created
 * devices, releasing and uninitializing hardware, etc).
 * Note: The init function is usually marked with the __exit macro. It is very
 * similar to __init in that it helps the compiler with allocation of the
 * functions and should only be used for your module's exit point. Again, if
 * you aren't sure where to use it, and then it's best to not use it at all.
 */
static void __exit kunetik_exit(void)
{
    kdev_deinit(kdevp);
    kfree(kdevp);
    printk(KERN_INFO "kunetik: module removed\n");
}

/**
 * These macros register your entry and exit points
 */
module_init(kunetik_init);
module_exit(kunetik_exit);
