/*
 * File         : usbCamCmds.h
 * Description  : ELE784 Lab2 IOCTL commands
 *
 * Etudiants:  LAPJ05108303(Jonathan Lapointe)
 *             TREC07029107 (Charles Trepanier)
 */

#ifndef USBCAMCMDSCMD_H_
#define USBCAMCMDSCMD_H_

#include </usr/include/linux/ioctl.h>

#define IOCTL_MAGICNUM 's'

//list of available IOCTL commands
#define IOCTL_STREAMON			_IOR(IOCTL_MAGICNUM, 0, int)
#define IOCTL_STREAMOFF			_IOR(IOCTL_MAGICNUM, 1, int)
#define IOCTL_PANTILT			_IOR(IOCTL_MAGICNUM, 2, int)
#define IOCTL_PANTILT_RESET		_IOR(IOCTL_MAGICNUM, 3, int)
#define IOCTL_GRAB				_IOR(IOCTL_MAGICNUM, 4, int)

#define IOCTL_MAXNR			4

typedef enum {HAUT, BAS, GAUCHE, DROITE} DIRECTION;

#endif /* USBCAMCMDSCMD_H_ */
