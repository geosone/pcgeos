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

/* vidcomOutput.c: DR_VID_RECT; the clipping the others share */
void VidDrawRect(DriverRegs *r);
/* the draw state of the CommonAttr at si in the GState, for window w */
void VidSetupDraw(const GState *gs, const CommonAttr *ca, const Window *w,
		  VidDrawState *ds);
/* fill a rectangle where the window's mask is */
void VidFillClipped(Window *w, sword left, sword top, sword right,
		    sword bottom, const VidDrawState *ds);
/* the visible spans of a rectangle, line by line, top down or bottom
 * up */
typedef void VidSpanFn(sword y, sword left, sword right, void *ctx);
void VidForSpans(Window *w, sword left, sword top, sword right,
		 sword bottom, Boolean bottomUp, VidSpanFn *fn, void *ctx);

/* vidcomLine.c: DR_VID_LINE */
void VidDrawLine(DriverRegs *r);
/* vidcomRaster.c: DR_VID_GETBITS, DR_VID_PUTBITS, DR_VID_MOVBITS */
void VidGetBits(DriverRegs *r);
void VidPutBits(DriverRegs *r);
void VidMovBits(DriverRegs *r);

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
/* a pixel with a mix mode, within the screen or not */
void VidDevPutPixel(sword x, sword y, dword color, MixMode mix);
/* n pixels of line sy from sx to line dy at dx; overlapping is fine */
void VidDevCopySpan(sword sx, sword sy, sword dx, sword dy, sword n);
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
