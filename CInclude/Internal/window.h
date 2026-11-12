/***********************************************************************
 *
 *	Copyright (c) GeoWorks 1989 -- All Rights Reserved
 *
 * PROJECT:	PC GEOS
 * FILE:	Internal/window.h
 *
 * DESCRIPTION:
 *	C form of Include/Internal/window.def: the Window structure, as
 *	the window system keeps it and the video drivers read it
 *	(Port32: handles are 32 bits, ABI.md section 2).
 *
 *	A Window is the start of an LMem block of its own; its regions
 *	are chunks of that block (WinRegionOf).  A region is lines of
 *	words: the bottom y of a band, then pairs of on/off x positions,
 *	then EOREGREC; EOREGREC ends the region.  A band reaches from the
 *	line after the previous band's bottom.
 *
 ***********************************************************************/
#ifndef __INTERNAL_WINDOW_H
#define __INTERNAL_WINDOW_H

#include <geos.h>
#include <lmem.h>
#include <graphics.h>
#include <win.h>
#include <Internal/tmatrix.h>

/* W_saveUnder, W_savedUnderMask, W_savedUnderFlags: a bit a save under
 * area */
typedef ByteFlags WinUnderFlags;

typedef ByteFlags WinGrRegFlags;
#define WGRF_WIN_PATH_VALID	0x02
#define WGRF_PATH_VALID		0x01

typedef ByteFlags WinGrFlags;
#define WGF_MASK_SIMPLE		0x40	/* the mask is a rectangle */
#define WGF_XFORM_VALID		0x20	/* W_curTMatrix is valid */
#define WGF_BUFFER_VALID	0x10	/* the driver's clip line buffer */
#define WGF_CLIP_SIMPLE		0x08	/* the clip line has no holes */
#define WGF_CLIP_NULL		0x04	/* nothing visible on the clip line */
#define WGF_MASK_NULL		0x02	/* nothing visible */
#define WGF_MASK_VALID		0x01	/* regions and clip fields valid */

#ifndef WCF_MASKED
#define WCF_MASKED		0x10	/* the window is masked (internal) */
#endif

typedef struct {
    word	WPD_ax;
    word	WPD_bx;
    word	WPD_cx;
    word	WPD_dx;
} WinPrivateData;

typedef ByteFlags WinPriorityData;	/* WPD_LAYER:4, WPD_WIN:4 */
typedef ByteFlags WinRegFlags;
typedef ByteFlags WinPtrFlags;

typedef struct {
    LMemBlockHeader	W_header;
    optr		W_exposureObj;	/* gets the exposures */
    optr		W_inputObj;	/* gets enter/leave */
    Handle		W_curState;	/* GState the clip fields are for */
    Handle		W_updateState;	/* GState doing an update, or 0 */
    WinPrivateData	W_privData;
    void		*W_driverStrategy; /* of the window's video driver */
    dword		W_bitmap;	/* HugeArray of a bitmap's window */
    void		*W_bmSegment;	/* its locked block (a pointer) */
    ChunkHandle		W_palette;	/* custom palette, or 0 */
    WinPriorityData	W_priority;
    Handle		W_layerID;
    WinColorFlags	W_color;	/* background color and flags */
    RGBValue		W_colorRGB;
    WinRegFlags		W_regFlags;
    WinPtrFlags		W_ptrFlags;
    byte		W_refCount;
    word		W_suspendCount;
    optr		W_gadgetPtrImage;
    optr		W_windowPtrImage;
    Handle		W_parent;
    Handle		W_prevSibling;
    Handle		W_nextSibling;
    Handle		W_firstChild;
    Handle		W_lastChild;
    ChunkHandle		W_winReg;	/* the window's region */
    ChunkHandle		W_univReg;	/* its universe */
    ChunkHandle		W_childReg;	/* the sum of its children */
    ChunkHandle		W_visReg;	/* the visible region */
    ChunkHandle		W_invalReg;
    ChunkHandle		W_updateReg;
    ChunkHandle		W_maskReg;	/* what the video driver draws in */
    ChunkHandle		W_temp1Reg;
    ChunkHandle		W_temp2Reg;
    ChunkHandle		W_winPathReg;
    ChunkHandle		W_pathReg;
    Rectangle		W_winRect;	/* bounds of the window */
    word		W_pattPos;	/* a byte each for x and y */
    word		W_ditherX;
    word		W_ditherY;
    TMatrix		W_TMatrix;	/* the window's transformation */
    TMatrix		W_curTMatrix;	/* the final one (with the GState) */
    WinUnderFlags	W_saveUnder;
    WinUnderFlags	W_savedUnderMask;
    WinUnderFlags	W_savedUnderFlags;
    Rectangle		W_maskRect;	/* bounds of W_maskReg */
    /* the clip line: the band of W_maskReg that held the last line
     * drawn (WinValClipLine); top EOREGREC when not valid */
    Rectangle		W_clipRect;
    word		W_clipPtr;	/* its on/off pairs: offset in the
					 * W_maskReg chunk */
    WinGrFlags		W_grFlags;
    WinGrRegFlags	W_grRegFlags;
    ChunkHandle		W_temp3Reg;
} Window;

/* a region chunk of the window, as words */
#define WinRegionOf(w, ch) \
    ((sword *) ((byte *) (w) + *(word *) ((byte *) (w) + (ch))))

/* the clip line for line y: W_clipRect, W_clipPtr, WGF_CLIP_SIMPLE and
 * WGF_CLIP_NULL (winIndividual.c) */
extern void WinValClipLine(Window *w, sword y);

#endif /* __INTERNAL_WINDOW_H */
