/***********************************************************************
 *
 *	Copyright (c) GeoWorks 1989 -- All Rights Reserved
 *
 * PROJECT:	PC GEOS
 * FILE:	Internal/mouseDr.h
 *
 * DESCRIPTION:
 *	C form of Include/Internal/mouseDr.def: what mouse drivers and
 *	their users share beyond the public mouseDr.h (Port32).
 *
 ***********************************************************************/
#ifndef __INTERNAL_MOUSEDR_H
#define __INTERNAL_MOUSEDR_H

#include <mouseDr.h>

/* mouseDr.def has minor 2 (the protocol of the DOS16 drivers) */
#define MOUSE_DRIVER_PROTO_MAJOR	(DRIVER_PROTO_MAJOR+1)
#define MOUSE_DRIVER_PROTO_MINOR	(DRIVER_PROTO_MINOR+2)

/* MouseDriverInfoStruct of mouseDr.def, with its MDIS_flags */
typedef struct {
    DriverExtendedInfoStruct	MDI_common;
    word			MDI_numButtons;
    word			MDI_xRes;	/* digitizers; 0 for a mouse */
    word			MDI_yRes;
    word			MDI_flags;	/* MouseDriverInfoFlags */
} MouseDriverInfo;

#define MDIF_KEYBOARD_ONLY	0x8000

/* MouseFunction, after DRE_SET_DEVICE */
#define DR_MOUSE_SET_RATE		12	/* cl rate -> cl actual */
#define DR_MOUSE_GET_RATES		14	/* -> es:di table, cx length */
#define DR_MOUSE_SET_ACCELERATION	16	/* cx threshold, dx multiplier */
#define DR_MOUSE_GET_ACCELERATION	18
#define DR_MOUSE_SET_COMBINE_MODE	20	/* cl MouseCombineMode */
#define DR_MOUSE_GET_COMBINE_MODE	22
#define DR_MOUSE_GET_CALIBRATION_POINTS	24
#define DR_MOUSE_SET_CALIBRATION_POINTS	26
#define DR_MOUSE_GET_RAW_COORDINATE	28
#define DR_MOUSE_CHANGE_OUTPUT		30	/* bx new -> bx old */
#define DR_MOUSE_START_CALIBRATION	32
#define DR_MOUSE_STOP_CALIBRATION	34

typedef ByteEnum MouseCombineMode;
#define MCM_COMBINE			0
#define MCM_NO_COMBINE			1
#define MCM_COMBINE_COLINEAR_ONLY	2

/* MouseExtendedInfo (the device table) */
#define MEI_SERIAL	0x8000
#define MEI_GENERIC	0x4000
#define MEI_IRQ		0x3c00
#define MEI_CALIBRATE	0x0200
#define MEI_WHEEL	0x0100

/* clock ticks within which motion is accelerated together */
#define MOUSE_ACCEL_THRESH	2

#endif /* __INTERNAL_MOUSEDR_H */
