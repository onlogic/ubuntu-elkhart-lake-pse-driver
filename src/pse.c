#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/fs.h> // file operations
#include <linux/uuid.h> // uuid_le, uuid_le_compare
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/intel-ish-client-if.h>
#include <linux/mod_devicetable.h>

#include "pse.h"

MODULE_DESCRIPTION("PSE ISHTP Client Driver");
MODULE_AUTHOR("Jacob Caughfield <jacob.caughfield@onlogic.com>");
MODULE_LICENSE("GPL");

#define WAIT_FOR_SEND_COUNT 10
#define WAIT_FOR_SEND_MS 100
#define WAIT_FOR_READ_MS 1000

/// HECI CLIENT IDENTIFIER
/// SMHI client UUID: bb579a2e-cc54-4450-b1d0-5e7520dcad25
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION( 6, 4, 0 ) )
static const guid_t pse_smhi_guid = UUID_LE_g(0xbb579a2e, 0xcc54, 0x4450, 0xb1, 0xd0, 0x5e,0x75, 0x20, 0xdc, 0xad, 0x25);
#else
static const uuid_le pse_smhi_guid = UUID_LE(
	0xbb579a2e, 0xcc54, 0x4450, 0xb1, 0xd0, 0x5e,
	0x75, 0x20, 0xdc, 0xad, 0x25);
#endif

/// Read buffer and read state tracking
///
/// @lock: Prevent data modification while in use
/// @wait_exception: Set if an error occurs while waiting for a read event
/// @wq_head: The wait queue head; used while waiting for a read interrupt
/// @rb: Actual data buffer for the read
struct pse_read_buffer {
    struct mutex lock;
    bool wait_exception;
    struct work_struct work;
    wait_queue_head_t wq_head;
    struct ishtp_cl_rb *rb;
};

/// Struct that manages the state of the pse device
///
/// @chrdev: Tracks the chardev MAJOR/MINOR
/// @cdev: Holds the actual character device for add/removing
/// @cclass: Tracks the character device class for sysfs and /dev
/// @ishtp_cl: Client transaction state management, allocated during chardev open
/// @ishtp_cl_device: The core ishtp device pointer, captured during probing
/// @pse_rb: The buffer for handling read requests and interrupts
struct pse_device {
    dev_t chrdev;
    struct cdev cdev;
    struct class * cclass;
    struct ishtp_cl *cl;
    struct ishtp_cl_device *cl_device;
    struct pse_read_buffer pse_rb;
};

static struct pse_device pse_dev;

/// Check that the PSE allocations are still safe before performing operations
#define CHECK_ISHTP_ALLOC()                                \
if(!pse_dev.cl || !pse_dev.cl_device) {                    \
    pr_warn("Device has been reset and de-allocated\n");   \
    return -ENODEV;                                        \
}

/// Check that the PSE connection is live and enabled (ready for rw)
#define CHECK_ISHTP_CONNECTION_ALIVE()                                  \
if (pse_dev.cl->dev->dev_state != ISHTP_DEV_ENABLED ||                  \
    pse_dev.cl->state != ISHTP_CL_CONNECTED                             \
) {                                                                     \
    pr_err("Wait failed: The ISHTP device connection is not alive\n");  \
    return -ENODEV;                                                     \
}

static inline int uuid_le_cmp_g(const guid_t u1, const guid_t u2)
{
	return memcmp(&u1, &u2, sizeof(guid_t));
}

/// Manage a userspace request to open the pse chardev
static int ishtp_pse_open(struct inode *inode, struct file *file) {
    int ret;
    
    // Non-blocking fops are not supported
    if (file->f_flags & O_NONBLOCK) {
        pr_warn("Device does not support non-blocking file IO\n");
        return -EINVAL;
    }

    if (!pse_dev.cl_device) {
        pr_warn("ISHTP device does not exist yet (probe failed?)\n");
        return -ENODEV;
    }

    // Allocate and link the cl device
    pse_dev.cl = ishtp_cl_allocate(pse_dev.cl_device);
    if (!pse_dev.cl) {
        pr_err("Failed to allocated the ishtp cl\n");
        return -ENOMEM;
    }

    ret = ishtp_cl_link(pse_dev.cl);
    if (ret) {
        pr_err("Failed to the link the ishtp cl\n");
        kfree(pse_dev.cl);
        return ret;
    }

    return nonseekable_open(inode, file);
}

/// Handle pse chardev read requests
static ssize_t ishtp_pse_read(struct file *file, char __user *ubuf, size_t length, loff_t *offset) {
    struct ishtp_cl_rb *rb;

    // Check that everything is safe and allocated
    
    CHECK_ISHTP_ALLOC();
    CHECK_ISHTP_CONNECTION_ALIVE();

    if (file->f_flags & O_NONBLOCK) {
        pr_warn("Device does not support non-blocking file IO\n");
        return -EINVAL;
    }

    // If no data is ready, wait on the callback
    if (!pse_dev.pse_rb.rb) {

        // No read-buffer is currently present. Wait for some data
        pse_dev.pse_rb.wait_exception = false;
        if (wait_event_interruptible_timeout(
            pse_dev.pse_rb.wq_head, 
            (pse_dev.pse_rb.rb != NULL || pse_dev.pse_rb.wait_exception),
            msecs_to_jiffies(WAIT_FOR_READ_MS)) < 1
        ) {
            pr_warn("Error waiting to receive PSE data\n");
            return -ERESTARTSYS;
        }

        // Re-validate the device state
        CHECK_ISHTP_ALLOC();
        CHECK_ISHTP_CONNECTION_ALIVE();

        if (!pse_dev.pse_rb.rb) {
            return -EIO;
        }
    }

    mutex_lock(&pse_dev.pse_rb.lock);

    rb = pse_dev.pse_rb.rb;

    // Copy the received data out to userspace
    if (!length || !ubuf || *offset > rb->buf_idx) {
        mutex_unlock(&pse_dev.pse_rb.lock);
        return -EMSGSIZE;
    }

    // Truncate to length
    length = min_t(size_t, length, rb->buf_idx - *offset);

    if (copy_to_user(ubuf, rb->buffer.data + *offset, length)) {
        mutex_unlock(&pse_dev.pse_rb.lock);
        return -EFAULT;
    }

    // Check if done reading
    *offset += length;
    if ((unsigned long)*offset < rb->buf_idx) {
        mutex_unlock(&pse_dev.pse_rb.lock);
        return length;
    }

    // Cleanup buffer
    ishtp_cl_io_rb_recycle(rb);
    pse_dev.pse_rb.rb = NULL;
    *offset = 0;

    mutex_unlock(&pse_dev.pse_rb.lock);
    return length;
}

/// Handle pse chardev write requests
static ssize_t ishtp_pse_write(struct file *file, const char __user *ubuf, size_t length, loff_t *offset) {
    int ret;
    void *write_buf;
    
    // Safe-checks
    CHECK_ISHTP_ALLOC();
    CHECK_ISHTP_CONNECTION_ALIVE();

    if (file->f_flags & O_NONBLOCK) {
        pr_warn("Device does not support non-blocking file IO\n");
        return -EINVAL;
    }

    if (length <= 0 || length > pse_dev.cl->device->fw_client->props.max_msg_length) {
        pr_err("Invalid write length specified\n");
        return -EMSGSIZE;
    }

    // Prepare the write buffer
    write_buf = memdup_user(ubuf, length);
    if (IS_ERR(write_buf)) {
        pr_err("Error occured while duplicating the write buffer\n");
        return PTR_ERR(write_buf);
    }

    ret = ishtp_cl_send(pse_dev.cl, write_buf, length);

    kfree(write_buf);

    return ret < 0 ? ret : length;
}

/// Manage a userspace request to close the pse chardev
static int ishtp_pse_release(struct inode *inode, struct file *file) {
    int ret = 0;
    int send_timeout = WAIT_FOR_SEND_COUNT;

    CHECK_ISHTP_ALLOC();

    // Cancel any ongoing read events
    pse_dev.pse_rb.wait_exception = true;
    wake_up_interruptible(&pse_dev.pse_rb.wq_head);

    // Lock the read buffer
    mutex_lock(&pse_dev.pse_rb.lock);

    // Check for connected state and wait for message transmission
    // This can delay for WAIT_FOR_SEND_COUNT * WAIT_FOR_SEND_MS (1s)
    if((pse_dev.cl->dev->dev_state == ISHTP_DEV_ENABLED) &&
       (pse_dev.cl->state == ISHTP_CL_CONNECTED)
    ) {
        // Wait for transmission with a timeout
        do {
            if (!ishtp_cl_tx_empty(pse_dev.cl)) {
                msleep_interruptible(WAIT_FOR_SEND_MS);
            } else {
                break;
            }
        } while (--send_timeout);

        // Set the diconnecting state
        pse_dev.cl->state = ISHTP_CL_DISCONNECTING;
        ret = ishtp_cl_disconnect(pse_dev.cl);
    }

    // Unlink and flush the connection
    ishtp_cl_unlink(pse_dev.cl);
    ishtp_cl_flush_queues(pse_dev.cl);
    ishtp_cl_free(pse_dev.cl);

    pse_dev.cl = NULL;

    // Clean the read buffer
    if(pse_dev.pse_rb.rb) {
        ishtp_cl_io_rb_recycle(pse_dev.pse_rb.rb);
        pse_dev.pse_rb.rb = NULL;
    }

    mutex_unlock(&pse_dev.pse_rb.lock);
    return ret;
}

/// Handle the primary client connect IOCTL
static int ishtp_pse_ioctl_cc(struct file *file, struct ishtp_device *ishtp_dev, struct ishtp_cc_data *data) {
    // TODO: Check if we can just use the cl_device->fw_client directly
    struct ishtp_client *client;
    struct ishtp_fw_client *fw_client;

    CHECK_ISHTP_ALLOC();

    // Confirm that the device is present and enabled
    if (!ishtp_dev) {
        return -ENODEV;
    }

    if (ishtp_dev->dev_state != ISHTP_DEV_ENABLED) {
        pr_err("The ISHTP PSE device is disabled\n");
        return -ENODEV;
    }

    // Check that the device doesn't already have an open connection=
    if (pse_dev.cl->state != ISHTP_CL_INITIALIZING && pse_dev.cl->state != ISHTP_CL_DISCONNECTED) {
        pr_err("The ISHTP PSE already has an open connection\n");
        return -EBUSY;
    }

    fw_client = ishtp_fw_cl_get_client(ishtp_dev, &data->in_client_uuid);
    if (!fw_client) {
        pr_warn("Did not find the client UUID\n");
        return -ENOENT;
    }

    // Prep and connect
    pse_dev.cl->fw_client_id = fw_client->client_id;
    pse_dev.cl->state = ISHTP_CL_CONNECTING;

    // Create the response data
    client = &data->out_client_props;
    client->max_message_length = fw_client->props.max_msg_length;
    client->protocol_version = fw_client->props.protocol_version;

    return ishtp_cl_connect(pse_dev.cl);
}

/// Handle pse connection IOCTLs
static long ishtp_pse_ioctl(struct file * file, unsigned int cmd, unsigned long data) {
    int ret;
    struct ishtp_cc_data *cc_data;
    
    if(!capable(CAP_SYS_ADMIN)) {
        return -EPERM;
    }

    // Check the ISHTP reset didn't free cl allocations
    CHECK_ISHTP_ALLOC();

    // Handle different IOCTLs
    switch (cmd) {
    case IOCTL_ISHTP_CONNECT_CLIENT:
    {
		
		pr_info("IOCTL_ISHTP_CONNECT_CLIENT\n");
        // Duplicate the input data
        cc_data = memdup_user((char __user *)data, sizeof(struct ishtp_cc_data));
        
        if (IS_ERR(cc_data)) {
            pr_err("Error duplicating ioctl data\n");
            return PTR_ERR(cc_data);
        }

        // Actually manage the connect ioctl
        ret = ishtp_pse_ioctl_cc(file, pse_dev.cl->dev, cc_data);
        if (ret) {
            pr_err("PSE ISHTP Connection IOCTL failed (%i)\n", ret);
            return ret;
        }

        break;
    }
    default:
    {
        pr_warn("Invalid IOCTL received\n");
        return -EINVAL;
    }
    };

    return 0;
}

/// PSE operations are exposed to userspace via a character device
static const struct file_operations ishtp_pse_fops = {
    .owner = THIS_MODULE,
    .open = ishtp_pse_open,
    .read = ishtp_pse_read,
    .write = ishtp_pse_write,
    .release = ishtp_pse_release,
    .unlocked_ioctl = ishtp_pse_ioctl,
    .llseek = no_llseek
};

/// Callback handler for ISHTP parent bus events
///
/// This function will be executed when events are received from the
/// ISHFW
static void ishtp_pse_event_cb(struct ishtp_cl_device *cl_device) {
    struct ishtp_cl_rb *rb;

    pr_debug("PSE ISHTP Client Event Callback\n");

    // Wait for lock
    mutex_lock(&pse_dev.pse_rb.lock);

    rb = pse_dev.pse_rb.rb;

    // Only read data if buffer is already empty
    if (!rb) {
        pr_debug("Read data from the PSE CL device\n");
        rb = ishtp_cl_rx_get_rb(pse_dev.cl);

        if (!rb) {
            pr_warn("Failed to read any data from the cl_rx read buffer\n");
            pse_dev.pse_rb.wait_exception = true;
        } else {
            pse_dev.pse_rb.rb = rb;
        }
    }

    // Wait any waiting read
    wake_up_interruptible(&pse_dev.pse_rb.wq_head);

    // Unlock
    mutex_unlock(&pse_dev.pse_rb.lock);
}

/// PSE ISHTP Work Reset Handler
///
/// This function is called when a reset workqueue is scheduled during and ISHFW
/// reset event. This disconnects and reconnects the pse cl
static void ishtp_cl_reset_handler(struct work_struct *work) {
    int ret = 0;
    struct ishtp_fw_client *fw_client;

    pr_info("ISHTP Client WorkQ Reset\n");

    if (!pse_dev.cl_device->ishtp_dev) {
        pr_err("The cl_device is not linked to any ishtp_device\n");
        return;
    }

    // Cancel any ongoing read events
    pse_dev.pse_rb.wait_exception = true;
    wake_up_interruptible(&pse_dev.pse_rb.wq_head);

    // Lock the read buffer
    mutex_lock(&pse_dev.pse_rb.lock);

    // Un-link any existing cl, and reconnect
    if (pse_dev.cl) {
        ishtp_cl_unlink(pse_dev.cl);
        ishtp_cl_flush_queues(pse_dev.cl);
        ishtp_cl_free(pse_dev.cl);
        
        pse_dev.cl = NULL;

        // Re-connect the cl
        pse_dev.cl = ishtp_cl_allocate(pse_dev.cl_device);
        
        if(!pse_dev.cl) {
            pr_err("Allocation of the pse cl failed\n");
            return;
        }

        if (pse_dev.cl->dev->dev_state != ISHTP_DEV_ENABLED) {
            pr_err("The ISHTP device isn't enabled\n");
            ret = -ENODEV;

            goto unlink;
        }

        // Re-establish the cl link and firwmare client
        ret = ishtp_cl_link(pse_dev.cl);
        if (ret) {
            pr_err("Linking the ISHTP firmware client failed\n");
            goto unlink;
        }

        fw_client = ishtp_fw_cl_get_client(pse_dev.cl_device->ishtp_dev, 
            &pse_dev.cl_device->fw_client->props.protocol_name);
        
        if (!fw_client) {
            pr_err("Could not detect the linked firmware client\n");
            ret = -ENOENT;
            goto unlink;
        }

        pse_dev.cl->fw_client_id = fw_client->client_id;
        pse_dev.cl->state = ISHTP_CL_CONNECTING;

        ret = ishtp_cl_connect(pse_dev.cl);
    }

unlink:
    if (ret) {
        ishtp_cl_free(pse_dev.cl);
        pse_dev.cl = NULL;

        pr_err("Reset failed: %i\n", ret);
    } else {
        // No error, re-register the callback
        ishtp_register_event_cb(pse_dev.cl_device, ishtp_pse_event_cb);
    }

    mutex_unlock(&pse_dev.pse_rb.lock);
    return;
}

/// De-register the character device region and cdev
static void ishtp_dealloc_chrdev(void) {
    if (pse_dev.cclass) {
        class_destroy(pse_dev.cclass);
    }

    if (&pse_dev.cdev) {
        cdev_del(&pse_dev.cdev);
    }

    unregister_chrdev_region(pse_dev.chrdev, 1);
}

/// Create the character device driver for the PSE
static int ishtp_pse_setup_chardev(void) {
    int ret;
    struct device *p_cdev;

    // Request dynamic chardev major allocation from the kernel
    ret = alloc_chrdev_region(&pse_dev.chrdev, 0, 1, "pse");

    if (ret) {
        pr_err("Failed to allocate a character device major\n");
        return ret;
    }

    // Create the character device
    cdev_init(&pse_dev.cdev, &ishtp_pse_fops);
    ret = cdev_add(&pse_dev.cdev, pse_dev.chrdev, 1);

    if (ret) {
        pr_err("Failed to add the PSE character device\n");
        return ret;
    }

    // Create the /sys/class and /dev files
#if NEWER_KENREL  == 1
    pse_dev.cclass = class_create("pse");
#else
    pse_dev.cclass = class_create(THIS_MODULE, "pse");
#endif

    if (IS_ERR(pse_dev.cclass)) {
        pr_err("Failed to create the PSE chardev class\n");
        ishtp_dealloc_chrdev();
        return -ENODEV;
    }

    p_cdev = device_create(pse_dev.cclass, NULL, pse_dev.chrdev, NULL, "pse");
    if (IS_ERR(p_cdev)) {
        pr_err("Failed to create the PSE device node\n");
        ishtp_dealloc_chrdev();
        return -ENODEV;
    }

    return 0;
}

/// This function is called whenever a new ISHTP client is probed
///
/// It checks to see if this device is the PSE bus device.
/// If the PSE device is detected, a character device driver for the PSE will
/// be allocated and registered. It will then be available as /dev/pse.
static int ishtp_pse_probe(struct ishtp_cl_device *cl_device) {
    int ret = 0;

    pr_info("ISHTP Client Probe\n");

    // Safe-check for NULL
    if (!cl_device) { 
        pr_warn("Bad device pointer in ishtp probe\n");
        return -ENODEV; 
    }

    // Device kind test
    if (uuid_le_cmp_g(
        pse_smhi_guid,
        cl_device->fw_client->props.protocol_name
    )) {
        pr_info("Unsupported ISHTP client: skipping.\n");
        return -ENODEV;
    }

    pr_info("PSE ISHTP bus detected: setting up\n");

    // Store the connected device
    pse_dev.cl_device = cl_device;

    // Prep the read buffer and mutex
    init_waitqueue_head(&pse_dev.pse_rb.wq_head);
    mutex_init(&pse_dev.pse_rb.lock);

    // Start work
    INIT_WORK(&pse_dev.pse_rb.work, ishtp_cl_reset_handler);

    ret = ishtp_register_event_cb(cl_device, ishtp_pse_event_cb);
    if (ret) {
        pr_err("Failed to register the PSE event callback (%i)\n", ret);
        return ret;
    }

    ret = ishtp_pse_setup_chardev();
    if (ret) {
        pr_err("Failed to create the PSE character device (%i)\n", ret);
    }

    pr_info("PSE ISHTP device sucessfully created\n");

    return 0;
}

/// Handle dropping an ISHTP client on driver unload
static void ishtp_pse_remove(struct ishtp_cl_device *cl_device) {
    pr_info("ISHTP Client Remove\n");

    // Cancel any ongoing read events
    pse_dev.pse_rb.wait_exception = true;
    wake_up_interruptible(&pse_dev.pse_rb.wq_head);

    // Lock the read buffer
    mutex_lock(&pse_dev.pse_rb.lock);

    // Unlink and destroy the ISHTP connection
    if (pse_dev.cl) {
        pse_dev.cl->state = ISHTP_CL_DISCONNECTING;
        ishtp_cl_disconnect(pse_dev.cl);
        ishtp_cl_unlink(pse_dev.cl);
        ishtp_cl_flush_queues(pse_dev.cl);
        ishtp_cl_free(pse_dev.cl);
        pse_dev.cl = NULL;
    }

    mutex_unlock(&pse_dev.pse_rb.lock);

    // Destroy the mutex
    mutex_destroy(&pse_dev.pse_rb.lock);

    // Close the device
    ishtp_put_device(cl_device);

    // De-allocate the character device
    device_destroy(pse_dev.cclass, pse_dev.chrdev);
    ishtp_dealloc_chrdev();

    //return 0;
}

/// Manange ISHTP reset events
///
/// When a device reset occurs on the ISHTP bus, the connection
/// needs to be cleaned and re-opened.
static int ishtp_pse_reset(struct ishtp_cl_device *cl_device) {
    if (!pse_dev.cl_device) {
        pr_err("Client driver was not ready during reset\n");
        return -ENODEV;
    }

    // Perform actual reset ops
    schedule_work(&pse_dev.pse_rb.work);

    return 0;
}

static struct ishtp_device_id device_id={
	.guid = pse_smhi_guid,
};


static struct ishtp_cl_driver pse_client_driver = {
    .name = "pse-client",
    .id = &device_id,
    .probe = ishtp_pse_probe,
    .remove = ishtp_pse_remove,
    .reset = ishtp_pse_reset
};

/// Register this driver with the ISHTP Bus
static int __init pse_client_init(void) {
    return ishtp_cl_driver_register(&pse_client_driver, THIS_MODULE);
}

/// Unregister this driver with the ISHTP Bus
static void __exit pse_client_exit(void) {
    return ishtp_cl_driver_unregister(&pse_client_driver);
}

// Use late_initcall to ensure the ISHTP driver will always be loaded first
late_initcall(pse_client_init);
module_exit(pse_client_exit);
