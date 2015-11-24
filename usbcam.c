/*
 * File         : usbcam.c
 * Description  : ELE784 Lab2 source
 *
 * Etudiants:  XXXX00000000 (prenom nom #1)
 *             XXXX00000000 (prenom nom #2)
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
#include "dht_data.h"
#include "usbCamCmds.h"

// Module Information
MODULE_AUTHOR("prenom nom #1, prenom nom #2");
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
static int urbInit(struct urb *urb, struct usb_interface *intf);
static void urbCompletionCallback(struct urb *urb);


static unsigned int myStatus;
static unsigned int myLength;
static unsigned int myLengthUsed;
static char * myData;
static struct urb *myUrb[5];

struct usbcam_dev {
	struct usb_device *usbdev;
};

struct class * my_class;

static struct usb_device_id usbcam_table[] = {
// { USB_DEVICE(VENDOR_ID, PRODUCT_ID) },
//{ USB_DEVICE(0x046d, 0x08cc) },s
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
static struct usb_class_driver usbcam_class = {
	.name       = "usb/usbcam%d",
	.fops       = &usbcam_fops,
	.minor_base = USBCAM_MINOR,
};

//char tabHaut[4] = 	{0x00, 0x00, 0x80, 0xFF};
//char tabBas[4] = 	{0x00, 0x00, 0x80, 0x00};
//char tabGauche[4] = {0x80, 0x00, 0x00, 0x00};
//char tabDroite[4] = {0x80, 0xFF, 0x00, 0x00};

static int __init usbcam_init(void) {
	int error;
	printk(KERN_ALERT   "ELE784 -> Init...\n");
	error = usb_register(&usbcam_driver);
	if(error)
		printk(KERN_ALERT   "ELE784 -> Initialization failed, error: %d\n",error);
    return error;
}

static void __exit usbcam_exit(void) {
	printk(KERN_ALERT   "ELE784 -> Exiting...\n");
	usb_deregister(&usbcam_driver);
}

static int usbcam_probe (struct usb_interface *intf, const struct usb_device_id *devid) {
//	const struct usb_host_interface *interface;
//	const struct usb_endpoint_descriptor *endpoint;
//	struct usb_device *dev = interface_to_usbdev(intf);
	struct usbcam_dev *camdev = NULL;
	printk(KERN_ALERT   "ELE784 -> Probe...\n");

	if(intf->cur_altsetting->desc.bInterfaceClass == CC_VIDEO)
	{
		printk(KERN_ALERT   "ELE784 -> CC_VIDEO device detected\n");
		if(intf->cur_altsetting->desc.bInterfaceSubClass == SC_VIDEOSTREAMING)
		{
			camdev = kmalloc (sizeof(struct usbcam_dev), GFP_KERNEL);
			if(camdev<0)
			{
				printk(KERN_ALERT   "ELE784 -> KMALLOC FAIL...\n");
				return -1;
			}

			camdev->usbdev = usb_get_dev (interface_to_usbdev(intf));
			usb_set_intfdata (intf, camdev);
			usb_register_dev (intf, &usbcam_class);			//enregistre le driver dans /dev
			usb_set_interface(interface_to_usbdev(intf), 1, 4);
			printk(KERN_ALERT   "ELE784 -> SC_VIDEOSTREAMING device detected\n");
			return 0;
		}
		else if(intf->cur_altsetting->desc.bInterfaceSubClass == SC_VIDEOCONTROL)
		{
			printk(KERN_ALERT   "ELE784 -> SC_VIDEOCONTROL device detected\n");
			return 0;
		}
		else
			return -1;
	}
	else
		return -1;

	return -1;
}

void usbcam_disconnect(struct usb_interface *intf) {
	struct usbcam_dev *camdev = NULL;
	printk(KERN_ALERT   "ELE784 -> Disconnect...\n");


	if(intf->cur_altsetting->desc.bInterfaceSubClass == SC_VIDEOSTREAMING)
	{
		camdev = (struct usbcam_dev *)usb_get_intfdata(intf);
		kfree(camdev);
		usb_set_intfdata (intf, NULL);
		usb_deregister_dev (intf, &usbcam_class);
	}
}

int usbcam_open (struct inode *inode, struct file *filp) {
	struct usb_interface *intf;
	int subminor;
	printk(KERN_ALERT "ELE784 -> Open... \n\r");
	subminor = iminor(inode);
	intf = usb_find_interface(&usbcam_driver, subminor);
	if (!intf)
	{
		printk(KERN_ALERT "ELE784 -> Open: Ne peux ouvrir le peripherique");
		return -ENODEV;
	}
	filp->private_data = intf;
	return 0;
}

int usbcam_release (struct inode *inode, struct file *filp) {
	printk(KERN_ALERT "ELE784 -> Release... \n\r");


    return 0;
}

ssize_t usbcam_read (struct file *filp, char __user *ubuf, size_t count, loff_t *f_ops) {
    return 0;
}

ssize_t usbcam_write (struct file *filp, const char __user *ubuf, size_t count, loff_t *f_ops) {
    return 0;
}

long usbcam_ioctl (struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned int argument = (unsigned int)arg;
	struct usb_interface *intf = filp->private_data;
	struct usbcam_dev *camdev = (struct usbcam_dev *)usb_get_intfdata(intf);
	struct usb_device *dev = camdev->usbdev;
	int err = 0;
	char tempData;
	char tabHaut[4] = 	{0x00, 0x00, 0x80, 0xFF};
	char tabBas[4] = 	{0x00, 0x00, 0x80, 0x00};
	char tabGauche[4] = {0x80, 0x00, 0x00, 0x00};
	char tabDroite[4] = {0x80, 0xFF, 0x00, 0x00};
//	DIRECTION camDir;

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
				printk(KERN_ALERT   "ELE784 -> IOCTL_PANTILT: Received invalid argument: %d\n");
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

		default:
			printk(KERN_ALERT "ELE784 -> Undefined command\n\r");
		break;

	}

    return 0;
}


// *************************** //
// **** Private functions **** //
// *************************** //

/* FIXME: REMOVE THIS LINE

int urbInit(struct urb *urb, struct usb_interface *intf) {
    int i, j, ret, nbPackets, myPacketSize, size, nbUrbs;
    struct usb_host_interface *cur_altsetting = intf->cur_altsetting;
    struct usb_endpoint_descriptor endpointDesc = cur_altsetting->endpoint[0].desc;

    nbPackets = 40;  // The number of isochronous packets this urb should contain
    myPacketSize = le16_to_cpu(endpointDesc.wMaxPacketSize);
    size = myPacketSize * nbPackets;
    nbUrbs = 5;

    for (i = 0; i < nbUrbs; ++i) {
        // TODO: usb_free_urb(...);
        // TODO: myUrb[i] = usb_alloc_urb(...);
        if (myUrb[i] == NULL) {
            // TODO: printk(KERN_WARNING "");
            return -ENOMEM;
        }

        // TODO: myUrb[i]->transfer_buffer = usb_buffer_alloc(...);

        if (myUrb[i]->transfer_buffer == NULL) {
            // printk(KERN_WARNING "");
            usb_free_urb(myUrb[i]);
            return -ENOMEM;
        }

        // TODO: myUrb[i]->dev = ...
        // TODO: myUrb[i]->context = *dev*;
        // TODO: myUrb[i]->pipe = usb_rcvisocpipe(*dev*, endpointDesc.bEndpointAddress);
        myUrb[i]->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
        myUrb[i]->interval = endpointDesc.bInterval;
        // TODO: myUrb[i]->complete = ...
        // TODO: myUrb[i]->number_of_packets = ...
        // TODO: myUrb[i]->transfer_buffer_length = ...

        for (j = 0; j < nbPackets; ++j) {
            myUrb[i]->iso_frame_desc[j].offset = j * myPacketSize;
            myUrb[i]->iso_frame_desc[j].length = myPacketSize;
        }
    }

    for(i = 0; i < nbUrbs; i++){
        // TODO: if ((ret = usb_submit_urb(...)) < 0) {
            // TODO: printk(KERN_WARNING "");
            return ret;
        }
    }
    return 0;
}


static void urbCompletionCallback(struct urb *urb) {
    int ret;
    int i;
    unsigned char * data;
    unsigned int len;
    unsigned int maxlen;
    unsigned int nbytes;
    void * mem;

    if(urb->status == 0){

        for (i = 0; i < urb->number_of_packets; ++i) {
            if(myStatus == 1){
                continue;
            }
            if (urb->iso_frame_desc[i].status < 0) {
                continue;
            }

            data = urb->transfer_buffer + urb->iso_frame_desc[i].offset;
            if(data[1] & (1 << 6)){
                continue;
            }
            len = urb->iso_frame_desc[i].actual_length;
            if (len < 2 || data[0] < 2 || data[0] > len){
                continue;
            }

            len -= data[0];
            maxlen = myLength - myLengthUsed ;
            mem = myData + myLengthUsed;
            nbytes = min(len, maxlen);
            memcpy(mem, data + data[0], nbytes);
            myLengthUsed += nbytes;

            if (len > maxlen) {
                myStatus = 1; // DONE
            }

            // Mark the buffer as done if the EOF marker is set.
            if ((data[1] & (1 << 1)) && (myLengthUsed != 0)) {
                myStatus = 1; // DONE
            }
        }

        if (!(myStatus == 1)){
            if ((ret = usb_submit_urb(urb, GFP_ATOMIC)) < 0) {
                // TODO: printk(KERN_WARNING "");
            }
        } else {
            ///////////////////////////////////////////////////////////////////////
            //  Synchronisation
            ///////////////////////////////////////////////////////////////////////
            //TODO
        }
    } else {
        // TODO: printk(KERN_WARNING "");
    }
}

FIXME: REMOVE THIS LINE*/
