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
    byte	VDS_maskBuf[8];	/* a masked window's: VDS_mask there */
} VidDrawState;

/* vidcomPalette.c */
extern RGBValue vidPalette[256];
void VidSetPalette(DriverRegs *r);
void VidGetPalette(DriverRegs *r);
/* a driver's own palette (the memory driver: the bitmap's); TRUE if it
 * took the call */
extern Boolean (*vidDevPalette)(DriverRegs *r, Boolean set);

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

/* vidcomLine.c: lines.  VidLineRuns steps a line from point 1 to point
 * 2 (both on) in runs, horizontal or vertical, in the line's direction;
 * k is the run's first pixel along the line, from 0 */
typedef void VidRunFn(sword x1, sword y1, sword x2, sword y2, word k,
		      void *ctx);
void VidLineRuns(sword x1, sword y1, sword x2, sword y2, Boolean skipFirst,
		 VidRunFn *fn, void *ctx);
void VidDrawLine(DriverRegs *r);
void VidPolyline(DriverRegs *r);
Boolean VidPolylineBounds(const sword *p, sword bw, sword bh, Rectangle *b);
void VidDashLine(DriverRegs *r);
void VidDashFill(DriverRegs *r);

/* vidcomPolygon.c */
void VidPolygon(DriverRegs *r);
void VidFillPolygon(Window *w, const VidDrawState *ds, const Point *p,
		    word n);
void VidPointBounds(const Point *p, word n, Rectangle *b);

/* vidcomRegion.c */
void VidDrawRegion(DriverRegs *r);
Boolean VidRegionBounds(DriverRegs *r, Rectangle *b);
typedef void VidRectFn(sword l, sword t, sword r, sword b, void *ctx);
/* the bounds and the rectangles of a region with its parameters, moved
 * by ox, oy */
void VidRegionTransBounds(const word *rg, const sword *params, sword ox,
			  sword oy, Rectangle *b);
void VidRegionWalk(const word *rg, const sword *params, sword ox, sword oy,
		   VidRectFn *fn, void *ctx);

/* vidcomXOR.c: the XOR region */
void VidSetXOR(DriverRegs *r);
void VidClearXOR(DriverRegs *r);
void VidHideXOR(void);
void VidShowXOR(void);
void VidCheckXORCollision(sword l, sword t, sword r, sword b);
void VidXORPtrMoved(sword dx, sword dy);
void VidResetXOR(void);

/* vidcomUnder.c: save under areas */
Boolean VidUnder(word function, DriverRegs *r);
WinUnderFlags VidUnderAt(sword x, sword y);
void VidUnderCollision(Window *w, const Rectangle *b);
void VidUnderReset(void);

/* vidcomEntry.c: where the pointer is (DR_VID_MOVEPTR); inverting */
extern sword vidPtrX, vidPtrY;
extern const VidDrawState vidInvertState;

/* vidcomRaster.c: DR_VID_GETBITS, DR_VID_PUTBITS, DR_VID_MOVBITS, and
 * the pixels of a bitmap */
void VidGetBits(DriverRegs *r);
void VidPutBits(DriverRegs *r);
void VidMovBits(DriverRegs *r);
#define VBP_SKIP	0		/* masked out, or a 0 when filling */
#define VBP_COLOR	1		/* *color */
#define VBP_FILL	2		/* in the area color (PBF_FILL_MASK) */
int VidBitmapPixel(const PutBitsArgs *pba, word bx, word by, dword *color);
typedef struct {
    const PutBitsArgs	*VPS_pba;
    MixMode		VPS_mix;
    VidDrawState	VPS_fill;
} VidPutState;
void VidPutSetup(const GState *gs, const Window *w, const PutBitsArgs *pba,
		 VidPutState *c);
/* bitmap pixel bx, by at x, y */
void VidPutBitmapPixel(const VidPutState *c, sword x, sword y, word bx,
		       word by);

/* vidcomPutLine.c: DR_VID_PUTLINE */
void VidPutLine(DriverRegs *r);

/* vidcomChars.c: DR_VID_PUTSTRING */
void VidPutString(DriverRegs *r);

/*
 * The device part.
 */
Boolean VidDevInit(void);			/* DR_INIT; carry */
/* DR_VID_INFO: the driver's information (for the window at es:di) */
VideoDriverInfo *VidDevInfo(DriverRegs *r);
/* before a function with the semaphore: what it draws on (the window
 * at es:di); VidDevUpdate ends it */
void VidDevBegin(DriverRegs *r);
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
