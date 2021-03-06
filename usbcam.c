/*
 * File         : usbcam.c
 * Description  : ELE784 Lab2 source
 *
 * Etudiants:  LAPJ05108303(Jonathan Lapointe)
 *             TREC07029107 (Charles Trepanier)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <uapi/asm-generic/errno-base.h>
#include <linux/fcntl.h>
#include <uapi/asm-generic/fcntl.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include <uapi/asm-generic/ioctl.h>
#include <linux/usb.h>
#include <linux/completion.h>
#include "usbvideo.h"
#include "usbCamCmds.h"

#define NB_URBS 5



// Module Information
MODULE_AUTHOR("Jonathan Lapointe, Charles Trepanier");
MODULE_LICENSE("Dual BSD/GPL");

// Prototypes
static int __init usbcam_init(void);
static void __exit usbcam_exit(void);
static int usbcam_probe (struct usb_interface *intf, const struct usb_device_id *devid);
static void usbcam_disconnect(struct usb_interface *intf);
static int usbcam_open (struct inode *inode, struct file *filp);
static int usbcam_release (struct inode *inode, struct file *filp) ;
static ssize_t usbcam_read (struct file *filp, char __user *ubuf, size_t count, loff_t *f_ops);
static ssize_t usbcam_write (struct file *filp, const char __user *ubuf, size_t count, loff_t *f_ops);
static long usbcam_ioctl (struct file *filp, unsigned int cmd, unsigned long arg);

module_init(usbcam_init);
module_exit(usbcam_exit);

// Private function prototypes
static int  urbInit(struct urb *urb, struct usb_interface *intf);
static void urbCompletionCallback(struct urb *urb);

//global variables
static unsigned int myStatus;
static unsigned int myLength;
static unsigned int myLengthUsed;
static char * myData;
static int nbUser=0;
static struct urb *myUrb[5];
atomic_t  myUrbCount;

//usb device structure
struct usbcam_dev
{
	struct usb_device *usbdev;
};

struct class * my_class;

//contains the device IDs for the two version of the camera
static struct usb_device_id usbcam_table[] = {
// { USB_DEVICE(VENDOR_ID, PRODUCT_ID) },
{ USB_DEVICE(0x046d, 0x08cc) },
{ USB_DEVICE(0x046d, 0x0994) },
{}
};
MODULE_DEVICE_TABLE(usb, usbcam_table);

// USB Driver structure
static struct usb_driver usbcam_driver = {
	.name       = "usbcam",
	.id_table   = usbcam_table,
	.probe      = usbcam_probe,
	.disconnect = usbcam_disconnect,
};

// File operation structure
struct file_operations usbcam_fops = {
	.owner          = THIS_MODULE,
	.open           = usbcam_open,
	.release        = usbcam_release,
	.read           = usbcam_read,
	.write          = usbcam_write,
	.unlocked_ioctl = usbcam_ioctl,
};

#define USBCAM_MINOR 0
static struct usb_class_driver usbcam_class =
{
	.name      	 	= "usb/usbcam%d",
	.fops       	= &usbcam_fops,
	.minor_base 	= USBCAM_MINOR,
};

struct completion read_complete;

/*
 * Function: usbcam_init
 * Description:
 * Initializes the required global variables and registers the driver to the USB core.
 */
static int __init usbcam_init(void) {
	int error,i;
	printk(KERN_ALERT   "ELE784 -> Init...\n");
	nbUser=0;
	atomic_set(&myUrbCount,0);
	error = usb_register(&usbcam_driver);		//registers the driver to the usb core
	init_completion(&read_complete);
	myLength = 42666;
	myData = kmalloc((myLength * 2)* sizeof(unsigned char), GFP_KERNEL);

	for(i=0;i<NB_URBS;i++)
		myUrb[i]=NULL;

	if(error)
		printk(KERN_ALERT   "ELE784 -> Initialization failed, error: %d\n",error);
    return error;
}

/*
 * Function: usbcam_exit
 * Description:
 * Deregisters the driver from the USB core and frees the dynamically allocated memory
 */
static void __exit usbcam_exit(void)
{
	printk(KERN_ALERT   "ELE784 -> Exiting...\n");
	usb_deregister(&usbcam_driver);				//deregisters the driver to the usb core
	kfree(myData);
}

/*
 * Function: usbcam_probe
 * Description:
 * This function is called when a new usb device is available. If the device's interface matches,
 * it is registered to /dev and returns 0 indicating the usb core that the driver will handle this usb device.
 */
static int usbcam_probe (struct usb_interface *intf, const struct usb_device_id *devid)
{
	struct usbcam_dev *camdev = NULL;
	printk(KERN_ALERT   "ELE784 -> Probe...\n");

	if(intf->cur_altsetting->desc.bInterfaceClass == CC_VIDEO)
	{
		printk(KERN_ALERT   "ELE784 -> CC_VIDEO device detected\n");
		if(intf->cur_altsetting->desc.bInterfaceSubClass == SC_VIDEOSTREAMING)		//if the interface matches, the driver will handle the device
		{
			camdev = kmalloc (sizeof(struct usbcam_dev), GFP_KERNEL);				//allocates memory for the structure
			if(camdev<0)
			{
				printk(KERN_ALERT   "ELE784 -> KMALLOC FAIL...\n");
				return -ENODEV;
			}

			camdev->usbdev = usb_get_dev (interface_to_usbdev(intf));				//retrieves the usb driver structure
			usb_set_intfdata (intf, camdev);										//ties the local structure to the interface
			usb_register_dev (intf, &usbcam_class);									//registers the usb device in /dev
			usb_set_interface(interface_to_usbdev(intf), 1, 4);						//sets interface parameters
			printk(KERN_ALERT   "ELE784 -> SC_VIDEOSTREAMING device detected\n");
			return 0;																//tells the usb core that this driver will handle the usb device
		}
		else if(intf->cur_altsetting->desc.bInterfaceSubClass == SC_VIDEOCONTROL)
		{
			printk(KERN_ALERT   "ELE784 -> SC_VIDEOCONTROL device detected\n");
			return 0;
		}
		else
			return -ENODEV;
	}

	return -ENODEV;																	//interface is not handled by this driver
}

/*
 * Function: usbcam_disconnect
 * Description:
 * Deregisters the device from the usb core and frees it's structure memory.
 */
void usbcam_disconnect(struct usb_interface *intf)
{
	struct usbcam_dev *camdev = NULL;
	printk(KERN_ALERT   "ELE784 -> Disconnect...\n");


	if(intf->cur_altsetting->desc.bInterfaceSubClass == SC_VIDEOSTREAMING)
	{
		camdev = (struct usbcam_dev *)usb_get_intfdata(intf);
		kfree(camdev);
		usb_set_intfdata (intf, NULL);						//unties the local structure from the interface
		usb_deregister_dev (intf, &usbcam_class);			//deregisters the usb device from the usb core
		printk(KERN_ALERT   "ELE784 -> Disconnect VIDEOSTREAMING...\n");
	}
}

/*
 * Function: usbcam_open
 * Description:
 * Passes the usb interface to the file pointer of the user application.
 */
int usbcam_open (struct inode *inode, struct file *filp)
{
	struct usb_interface *intf;
	int subminor;
	printk(KERN_ALERT "ELE784 -> Open... \n\r");
	if(nbUser!=0)																	//error if there's already a user
	{
		printk(KERN_ALERT "ELE784 -> Open: le peripherique est deja ouvert (1 user max) \n");
		return -EAGAIN;
	}
	subminor = iminor(inode);
	intf = usb_find_interface(&usbcam_driver, subminor);							//sets the usb interface to the driver
	if (!intf)
	{
		printk(KERN_ALERT "ELE784 -> Open: Ne peux ouvrir le peripherique \n");
		return -ENODEV;
	}
	filp->private_data = intf;														//sets the file pointer to the usb interface
	printk(KERN_ALERT "ELE784 -> intf est assigne \n");
	nbUser++;																		//increments the number of users
	return 0;
}

/*
 * Function: usbcam_release
 * Description:
 * Decreases the number of users.
 */
int usbcam_release (struct inode *inode, struct file *filp)
{
	printk(KERN_ALERT "ELE784 -> Release... \n\r");
	if(nbUser!=0)
		nbUser--;		//decrements the number of users
    return 0;
}

/*
 * Function: usbcam_read
 * Description:
 * Waits for a complete signal from the callback function, then copies the content received to the
 * user application. The usb urbs are then destroyed and the function returns the size of the copied
 * content.
 */
ssize_t usbcam_read (struct file *filp, char __user *ubuf, size_t count, loff_t *f_ops)
{
	int i;
	struct usb_interface *intf = filp->private_data;							//sets the interface pointer
	struct usbcam_dev *camdev = (struct usbcam_dev *)usb_get_intfdata(intf);	//sets the structure pointer
	struct usb_device *dev = camdev->usbdev;

	printk(KERN_ALERT "ELE784 -> waiting for completion...\n");
	wait_for_completion(&read_complete);										//waits for all urbs to be received

	if(atomic_read(&myUrbCount )!= NB_URBS)
	{
		printk(KERN_ALERT "ELE784 -> pas le bon nombre de URBS \n");
		return -1;
	}

	printk(KERN_ALERT "ELE784 -> waiting for completion done...\n");
	copy_to_user(ubuf, myData, myLengthUsed);									//copies the received data to the user application
	printk(KERN_ALERT "ELE784 -> copy to user done\n");

    for (i = 0; i < NB_URBS; i++)												//destroy the urbs
    {
    	if(myUrb[i]!=NULL)
    	{
			usb_kill_urb(myUrb[i]);
			usb_free_coherent(dev, myUrb[i]->transfer_buffer_length, myUrb[i]->transfer_buffer, myUrb[i]->transfer_dma);
			usb_free_urb(myUrb[i]);
			myUrb[i]=NULL;
    	}
    }
    return myLengthUsed;
}

/*
 * Function: usbcam_write
 * Description:
 * Does nothing.
 */
ssize_t usbcam_write (struct file *filp, const char __user *ubuf, size_t count, loff_t *f_ops)
{
    return 0;
}

/*
 * Function: usbcam_ioctl
 * Description:
 * Used by the user application to send various commands to the driver.
 */
long usbcam_ioctl (struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned int argument = (unsigned int)arg;
	struct usb_interface *intf = filp->private_data;
	struct usbcam_dev *camdev = (struct usbcam_dev *)usb_get_intfdata(intf);
	struct usb_device *dev = camdev->usbdev;
	int err = 0;
	char tempData;
	char tabHaut[4]   = {0x00, 0x00, 0x80, 0xFF};
	char tabBas[4]    = {0x00, 0x00, 0x80, 0x00};
	char tabGauche[4] = {0x80, 0x00, 0x00, 0x00};
	char tabDroite[4] = {0x80, 0xFF, 0x00, 0x00};

	if (_IOC_TYPE(cmd) != IOCTL_MAGICNUM)
		return -ENOTTY;

	if (_IOC_NR(cmd) > IOCTL_MAXNR)
		return -ENOTTY;

	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));

	if (err)
		return -EFAULT;

	printk(KERN_ALERT   "ELE784 -> IOCTL...\n");

	switch(cmd)
	{
		case IOCTL_STREAMON:
			printk(KERN_ALERT "ELE784 -> STREAM ON... \n\r");
			usb_control_msg(dev, usb_sndctrlpipe(dev, 0), 0x0B, USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE, 0x0004, 0x0001, NULL, 0, 0);
		break;

		case IOCTL_STREAMOFF:
			printk(KERN_ALERT "ELE784 -> STREAM OFF... \n\r");
			usb_control_msg(dev, usb_sndctrlpipe(dev, 0), 0x0B, USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE, 0x0000, 0x0001, NULL, 0, 0);
		break;

		case IOCTL_PANTILT:
			printk(KERN_ALERT "ELE784 -> PAN TILT... \n\r");
			if(argument < 0 || argument >3)
				printk(KERN_ALERT   "ELE784 -> IOCTL_PANTILT: Received invalid argument\n");
			else
			{
				switch(argument)
				{
					case HAUT:
						usb_control_msg(dev, usb_sndctrlpipe(dev, 0), 0x01, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0x0100, 0x0900, tabHaut, 4, 0);
						printk(KERN_ALERT   "ELE784 -> IOCTL_PANTILT HAUT\n");
					break;

					case BAS:
						usb_control_msg(dev, usb_sndctrlpipe(dev, 0), 0x01, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0x0100, 0x0900, tabBas, 4, 0);
					break;

					case GAUCHE:
						usb_control_msg(dev, usb_sndctrlpipe(dev, 0), 0x01, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0x0100, 0x0900, tabGauche, 4, 0);
					break;

					case DROITE:
						usb_control_msg(dev, usb_sndctrlpipe(dev, 0), 0x01, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0x0100, 0x0900, tabDroite, 4, 0);
					break;
				}
			}


		break;

		case IOCTL_PANTILT_RESET:
			printk(KERN_ALERT "ELE784 -> PAN TILT RESET... \n\r");
			tempData = 0x03;
			usb_control_msg(dev, usb_sndctrlpipe(dev, 0), 0x01, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0x0200, 0x0900, &tempData, 1, 0);
		break;

		case IOCTL_GRAB:
			printk(KERN_ALERT "ELE784 -> GRAB... \n\r");
			urbInit(NULL, intf);							//sends required urbs to the usb core
			break;

		default:
			printk(KERN_ALERT "ELE784 -> Undefined command\n\r");
		break;

	}

    return 0;
}


// *************************** //
// **** Private functions **** //
// *************************** //

/*
 * Function: urbInit
 * Description:
 * Initializes the urbs and submits them to the camera.
 */
int urbInit(struct urb *urb, struct usb_interface *intf) {
    int i, j, ret, nbPackets, myPacketSize, size, nbUrbs;
    struct usb_host_interface *cur_altsetting = intf->cur_altsetting;
    struct usb_endpoint_descriptor endpointDesc = cur_altsetting->endpoint[0].desc;
    struct usbcam_dev *camdev = (struct usbcam_dev *)usb_get_intfdata(intf);
	struct usb_device *dev = camdev->usbdev;

    nbPackets = 40;  // The number of isochronous packets this urb should contain
    myPacketSize = le16_to_cpu(endpointDesc.wMaxPacketSize);
    size = myPacketSize * nbPackets;
    nbUrbs = 5;
    reinit_completion(&read_complete);					//disable the read complete flag
    myLengthUsed = 0;
    atomic_set(&myUrbCount,0);							//reinitialize the urb count
    myStatus = 0;

    for (i = 0; i < nbUrbs; i++)
    {
        myUrb[i] = usb_alloc_urb(nbPackets, GFP_KERNEL);	//creates the urb
        if (myUrb[i] == NULL)
        {
            printk(KERN_ALERT "ELE784 -> One or more urb could not be allocated \n");
            return -ENOMEM;
        }

        myUrb[i]->transfer_buffer = usb_alloc_coherent(dev, size, GFP_DMA, &myUrb[i]->transfer_dma);	//allocates memory to the urb

        if (myUrb[i]->transfer_buffer == NULL)
        {
        	printk(KERN_ALERT "ELE784 -> One or more urb  transfer buffers could not be allocated \n");
        	usb_free_urb(myUrb[i]);
            return -ENOMEM;
        }

        //sets urb parameters
        myUrb[i]->dev = dev;
        myUrb[i]->context = dev;
        myUrb[i]->pipe = usb_rcvisocpipe(dev, endpointDesc.bEndpointAddress);
        myUrb[i]->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
        myUrb[i]->interval = endpointDesc.bInterval;
        myUrb[i]->complete = urbCompletionCallback;
        myUrb[i]->number_of_packets = nbPackets;
        myUrb[i]->transfer_buffer_length = size;

        for (j = 0; j < nbPackets; j++)
        {
            myUrb[i]->iso_frame_desc[j].offset = j * myPacketSize;
            myUrb[i]->iso_frame_desc[j].length = myPacketSize;
        }
    }

    for(i = 0; i < nbUrbs; i++)
    {
        if ((ret = usb_submit_urb(myUrb[i], GFP_KERNEL)) < 0)			//submits the urb to the usb core
        {
            printk(KERN_WARNING "ELE784 -> Urb submit to USB core failed: %d\n", ret);
            return ret;
        }
    }
    return 0;
}

/*
 * Function: urbCompletionCallback
 * Description:
 * This function is called by the usb core when an urb has returned from the usb device.
 * The urb's transfer buffer data is copied to a global variable.
 */
static void urbCompletionCallback(struct urb *urb)
{
    int ret;
    int i;
    unsigned char * data;
    unsigned int len;
    unsigned int maxlen;
    unsigned int nbytes;
    void * mem;

    if(urb->status == 0)
    {
        for (i = 0; i < urb->number_of_packets; ++i)
        {

            if(myStatus == 1)
            {
                continue;
            }
            if (urb->iso_frame_desc[i].status < 0)
            {
                continue;
            }
            data = urb->transfer_buffer + urb->iso_frame_desc[i].offset;
            if(data[1] & (1 << 6))
            {
                continue;
            }
            len = urb->iso_frame_desc[i].actual_length;
            if (len < 2 || data[0] < 2 || data[0] > len)
            {
                continue;
            }

            len -= data[0];
            maxlen = myLength - myLengthUsed ;
            mem = myData + myLengthUsed;
            nbytes = min(len, maxlen);
            memcpy(mem, data + data[0], nbytes);
            myLengthUsed += nbytes;

            if (len > maxlen)
            {
                myStatus = 1; // DONE
            }

            // Mark the buffer as done if the EOF marker is set.
            if ((data[1] & (1 << 1)) && (myLengthUsed != 0))
            {
                myStatus = 1; // DONE
            }
        }

        if (!(myStatus == 1))
        {

            if ((ret = usb_submit_urb(urb, GFP_ATOMIC)) < 0)
            {
                 printk(KERN_ALERT "ELE784 -> error submitting urb\n");
            }
        }
        else
        {
        	atomic_inc(&myUrbCount);						//updates the received urb count
        	if(atomic_read(&myUrbCount ) == NB_URBS)		//if all urbs have been received, sends a read complete signal
        	{
        		complete(&read_complete);
        		printk(KERN_ALERT "ELE784 -> complete signal sent\n");
        	}
        }
    }
    else
    {
    	printk(KERN_ALERT "ELE784 -> urb status invalid\n");
    }
}
