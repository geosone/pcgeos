/*
 * vidcom.h -- the common part of the C video drivers (the vidcom*.c
 * beside the vidcom*.asm) and what the device part of a driver
 * provides.
 *
 * As in DOS16, a video driver is the common code plus a device part.
 * The common part takes the driver functions (videoDr.h): the
 * semaphore, the exclusive, the clipping to the window's mask region;
 * the device part draws into its frame buffer and owns the pointer
 * (as vga24Pointer.asm did).
 */
#ifndef VIDCOM_H
#define VIDCOM_H

#include <geos.h>
#include <string.h>		/* memcpy and friends: kernel exports */
#include <driver.h>
#include <geosdriver.h>
#include <Internal/videoDr.h>
#include <Internal/window.h>
#include <Internal/gstate.h>

#define VID_NO_DEVICE	0xffff		/* VDI_device before DRE_SET_DEVICE */

Boolean VidStrategy(word function, DriverRegs *r);

/* the driver table of a video driver: the device part fills in the
 * size when the device is set */
#define VIDEO_DRIVER_TABLE(nColors, nBits, bmFormat, displayType) \
    VideoDriverInfo DriverTable = { \
	{ { DRIVER_STRATEGY(VidStrategy), DA_HAS_EXTENDED_INFO, \
	    DRIVER_TYPE_VIDEO }, 0 }, \
	DT_RASTER_DISPLAY, 1, 0, 0, 0, 80, 80, 0, \
	(nColors), 1, (nBits), 0, (bmFormat), (displayType), VID_NO_DEVICE }
extern VideoDriverInfo DriverTable;

/* how a span is drawn: the color, the mix mode, the draw mask (NULL:
 * all ones) */
typedef struct {
    dword	VDS_color;	/* 0x00RRGGBB */
    MixMode	VDS_mix;
    const byte	*VDS_mask;	/* 8 bytes, a byte a line (y & 7) */
} VidDrawState;

/* vidcomPalette.c */
extern RGBValue vidPalette[256];
void VidSetPalette(DriverRegs *r);
void VidGetPalette(DriverRegs *r);

/* vidcomOutput.c: DR_VID_RECT */
void VidDrawRect(DriverRegs *r);

/*
 * The device part.
 */
Boolean VidDevInit(void);			/* DR_INIT; carry */
void VidDevExit(void);				/* DR_EXIT */
word VidDevTest(const char *name);		/* DevicePresent */
Boolean VidDevSet(const char *name);		/* opens; carry */
void VidDevScreen(Boolean on);			/* SCREEN_ON/OFF */
/* rectangle, inclusive, within the screen or not */
void VidDevFillRect(sword left, sword top, sword right, sword bottom,
		    const VidDrawState *ds);
dword VidDevGetPixel(sword x, sword y);		/* 0x00RRGGBB */
/* show what changed, at the end of a driver function */
void VidDevUpdate(void);
/* the pointer (DR_VID_SETPTR ... MOVEPTR); NULL: the default one */
void VidDevSetPtr(const PointerDef *pd);
void VidDevHidePtr(void);
void VidDevShowPtr(void);
void VidDevMovePtr(sword x, sword y);
/* around drawing: hide the pointer if it is in the rectangle, show it
 * again after */
void VidDevCondHidePtr(sword left, sword top, sword right, sword bottom);
void VidDevCondShowPtr(void);

#endif /* VIDCOM_H */
