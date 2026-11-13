/*
 * vidmem.h -- inside the memory video driver (Main/mainMain.c,
 * vidmemUtils.c, with the C vidcom of Driver/Video/VidCom).
 */
#ifndef VIDMEM_H
#define VIDMEM_H

#include "vidcom.h"
#include <hugearr.h>
#include <heap.h>

/* the bitmap of the window being drawn on (VidDevBegin) */
typedef struct {
    VMFileHandle	file;		/* 0: none */
    VMBlockHandle	block;
    void		*dir;		/* the locked EditableBitmap */
    Boolean		ownDir;		/* locked by the driver */
    word		width, height;
    byte		format;		/* BMF_*; BMF_MONO editing the mask */
    word		lineSize;	/* bytes of a scan line (mask too) */
    word		dataOffset;	/* bm_dataOffset: the data or mask */
    const byte		*pal;		/* RGB triples */
    word		nPal;
    byte		*line0;		/* the locked scan lines */
    word		first, count;
    Boolean		dirty;
    Boolean		cluster;	/* BM_CLUSTERED_DITHER, monochrome */
} VMBitmap;

extern VMBitmap vmBM;

/* vidmemUtils.c */
void VMSelect(VMFileHandle file, VMBlockHandle block, void *dir);
void VMRelease(void);
word VMLineSize(word width, byte type);
extern byte vmDefaultPalette[256 * 3];

/* CMYK/cmykDither.c: the clustered dither of BM_CLUSTERED_DITHER */
#define VM_DITHER_HEIGHT	6
#define VM_DITHER_WIDTH		24
#define VM_DITHER_LEVELS	33
extern const byte (*const vidmemDitherBlack[VM_DITHER_LEVELS])[3];

#endif /* VIDMEM_H */
