/***********************************************************************
 *
 *	Copyright (c) GeoWorks 1989 -- All Rights Reserved
 *
 * PROJECT:	PC GEOS
 * FILE:	Internal/videoDr.h
 *
 * DESCRIPTION:
 *	C form of Include/Internal/videoDr.def: the functions and the
 *	driver table of video drivers (Port32, ABI.md section 7.5).
 *
 *	The registers of DriverRegs (geosdriver.h) carry what the .def
 *	passes; ds and es were segments of locked blocks and are pointers:
 *	    ds  the GState: DR_dsSi (a GState *), si: DR_si, the offset of
 *	        the CommonAttr in it
 *	    es  the Window: DR_esDi (a Window *)
 *	    ds:si of DR_VID_SETPTR: DR_dsSi (a PointerDef *, or NULL for
 *	        the default pointer)
 *	    a far pointer in dx:si (DR_VID_INFO, the palette, a device
 *	        name): DR_dsSi
 *	    ss:bp (PutBitsArgs), ds:bp of DR_VID_GETBITS: DR_bpPtr
 *
 ***********************************************************************/
#ifndef __INTERNAL_VIDEODR_H
#define __INTERNAL_VIDEODR_H

#include <geos.h>
#include <driver.h>
#include <graphics.h>
#include <win.h>

/* 5.3, as videoDr.def has it (the minor from the major, sic) */
#define VIDEO_PROTO_MAJOR	(DRIVER_EXT_PROTO_MAJOR+2)
#define VIDEO_PROTO_MINOR	(DRIVER_EXT_PROTO_MAJOR+0)

/* VidFunction, after DRE_SET_DEVICE */
#define DR_VID_INFO		12	/* -> dx:si the VideoDriverInfo */
#define DR_VID_GET_EXCLUSIVE	14	/* -> bx gstate */
#define DR_VID_START_EXCLUSIVE	16	/* bx gstate */
#define DR_VID_END_EXCLUSIVE	18	/* bx -> ax..dx aborted, si,di,cx,dx */
#define DR_VID_GETPIXEL		20	/* ax,bx -> ah raw, al r, bl g, bh b */
#define DR_VID_GETBITS		22
#define DR_VID_SETPTR		24	/* ds:si PointerDef */
#define DR_VID_HIDEPTR		26
#define DR_VID_SHOWPTR		28
#define DR_VID_MOVEPTR		30	/* ax,bx -> al save unders hit */
#define DR_VID_SAVE_UNDER	32
#define DR_VID_RESTORE_UNDER	34
#define DR_VID_NUKE_UNDER	36
#define DR_VID_REQUEST_UNDER	38
#define DR_VID_CHECK_UNDER	40
#define DR_VID_INFO_UNDER	42
#define DR_VID_COLLIDE_UNDER	44
#define DR_VID_SET_XOR		46
#define DR_VID_CLEAR_XOR	48
#define DR_VID_RECT		50	/* ax,bx,cx,dx; ds:si attr; es window */
#define DR_VID_PUTSTRING	52
#define DR_VID_MOVBITS		54
#define DR_VID_PUTBITS		56
#define DR_VID_LINE		58
#define DR_VID_REGION		60
#define DR_VID_PUTLINE		62
#define DR_VID_POLYGON		64
#define DR_VID_SCREEN_ON	66
#define DR_VID_SCREEN_OFF	68
#define DR_VID_POLYLINE		70
#define DR_VID_DASH_LINE	72
#define DR_VID_DASH_FILL	74
#define DR_VID_SET_PALETTE	76
#define DR_VID_GET_PALETTE	78

#define DR_VID_LAST_NON_SEMAPHORE	DR_VID_END_EXCLUSIVE
#define DR_VID_LAST_NON_EXCLUSIVE	DR_VID_INFO_UNDER
#define DR_VID_LAST_FUNCTION		DR_VID_GET_PALETTE

/* VidEscCode */
#define VID_ESC_SET_COLOR_TRANSFER		0x8100
#define VID_ESC_ROTATE_SCREEN			0x8101
#define VID_ESC_INVERT_SCREEN			0x8102
#define VID_ESC_CHECK_IF_FORMAT_IS_SUPPORTED	0x8103
#define VID_ESC_CHANGE_VID_MODE			0x8104
#define VID_ESC_SET_CONTRAST			0x8105
#define VID_ESC_UNSET_DEVICE			0x8106
#define VID_ESC_GET_HORIZ_POS_PARAMS		0x8107
#define VID_ESC_GET_VERT_POS_PARAMS		0x8108
#define VID_ESC_SET_HORIZ_POS			0x8109
#define VID_ESC_SET_VERT_POS			0x810a
#define VID_ESC_SET_TV_SUBCARRIER_FREQ		0x810b
#define VID_ESC_SET_BLACK_WHITE			0x810c
#define VID_ESC_UPDATE_DEVICE			0x810d
#define VID_ESC_GREY_TEXT			0x810e

typedef enum {
    DT_RASTER_DISPLAY,
    DT_RASTER_PRINTER,
    DT_RASTER_CAMERA,
    DT_VECTOR_PLOTTER,
    DT_MEMORY
} DeviceTechEnum;
typedef word DeviceTech;

typedef struct {
    DriverExtendedInfoStruct	VDI_common;
    DeviceTech	VDI_tech;
    byte	VDI_verMaj;
    byte	VDI_verMin;
    word	VDI_pageH;	/* pixels */
    word	VDI_pageW;
    word	VDI_vRes;	/* dots per inch */
    word	VDI_hRes;
    word	VDI_bpScan;	/* bytes per scan line */
    byte	VDI_nColors;	/* bits of color */
    byte	VDI_nPlanes;
    byte	VDI_nBits;	/* bits a pixel */
    byte	VDI_wColTab;
    BMType	VDI_bmFormat;
    DisplayType	VDI_displayType;
    word	VDI_device;
} VideoDriverInfo;

typedef WordFlags VisXORFlags;
#define VXF_X_POS_FOLLOWS_MOUSE		0x8000
#define VXF_Y_POS_FOLLOWS_MOUSE		0x4000
#define VXF_AX_PARAM_FOLLOWS_MOUSE	0x2000
#define VXF_BX_PARAM_FOLLOWS_MOUSE	0x1000
#define VXF_CX_PARAM_FOLLOWS_MOUSE	0x0800
#define VXF_DX_PARAM_FOLLOWS_MOUSE	0x0400

typedef struct {
    word	VXP_ax;
    word	VXP_bx;
    word	VXP_cx;
    word	VXP_dx;
    Point	VXP_mousePos;
} VisXORParams;

/* DR_VID_PUTBITS, DR_VID_PUTLINE: ss:bp, here DR_bpPtr */
typedef ByteEnum BMPalType;
#define BMPT_RGB	0	/* 3-byte RGB entries */
#define BMPT_INDEX	1	/* frame buffer values */

typedef WordFlags PutBitsFlags;
#define PBF_ALLOC_PALETTE	0x0004
#define PBF_FILL_MASK		0x0002	/* GrFillBitmap: 1s in the color */
#define PBF_PAL_TYPE		0x0001	/* BMPalType */

typedef struct {
    Bitmap		PBA_bm;		/* this slice, simple */
    PutBitsFlags	PBA_flags;
    const byte		*PBA_data;	/* the first byte */
    word		PBA_size;	/* bytes a scan line */
    const byte		*PBA_pal;	/* the palette, if BMT_PALETTE */
} PutBitsArgs;

/* PointerDef of graphics.def: the mask, then the image follow, a bit a
 * pixel, (width + 7) / 8 bytes a line.  mask 0 image 0: unchanged,
 * 0 1: xor, 1 0: black, 1 1: white. */
#define PDW_ALWAYS_SHOW_PTR	0x80
#define PDW_WIDTH		0x7f
typedef struct {
    byte	PD_width;	/* PointerDefWidth */
    byte	PD_height;
    sbyte	PD_hotX;
    sbyte	PD_hotY;
} PointerDef;

#define STANDARD_CURSOR_IMAGE_SIZE	32

#endif /* __INTERNAL_VIDEODR_H */
