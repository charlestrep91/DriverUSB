/*
 * usbCamCmds.h
 *
 *  Created on: Oct 19, 2015
 *      Author: charles
 */

#ifndef USBCAMCMDSCMD_H_
#define USBCAMCMDSCMD_H_

#include </usr/include/linux/ioctl.h>

#define IOCTL_MAGICNUM 's'

#define IOCTL_STREAMON			_IOR(IOCTL_MAGICNUM, 0, int)
#define IOCTL_STREAMOFF			_IOR(IOCTL_MAGICNUM, 1, int)
#define IOCTL_PANTILT			_IOR(IOCTL_MAGICNUM, 2, int)
#define IOCTL_PANTILT_RESET		_IOR(IOCTL_MAGICNUM, 3, int)

#define IOCTL_MAXNR			3

typedef enum {HAUT, BAS, GAUCHE, DROITE} DIRECTION;
//#define HAUT	0
//#define BAS		1
//#define GAUCHE	2
//#define DROITE	3

char tabHaut[4] = {0x00, 0x00, 0x80, 0xFF};
char tabBas[4] = {0x00, 0x00, 0x80, 0x00};
char tabGauche[4] = {0x80, 0x00, 0x00, 0x00};
char tabDroite[4] = {0x80, 0xFF, 0x00, 0x00};

#endif /* USBCAMCMDSCMD_H_ */
