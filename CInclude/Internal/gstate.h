/***********************************************************************
 *
 *	Copyright (c) GeoWorks 1989 -- All Rights Reserved
 *
 * PROJECT:	PC GEOS
 * FILE:	Internal/gstate.h
 *
 * DESCRIPTION:
 *	C form of Include/Internal/gstate.def: the GState, as the
 *	graphics system keeps it and the video drivers read it (Port32:
 *	handles are 32 bits).  The GOpt fields were opcodes the asm
 *	patched into its code; here they are flags (GO_*).
 *
 ***********************************************************************/
#ifndef __INTERNAL_GSTATE_H
#define __INTERNAL_GSTATE_H

#include <geos.h>
#include <lmem.h>
#include <graphics.h>
#include <color.h>
#include <Internal/tmatrix.h>

typedef ByteFlags AttrOpts;
#define AO_MASK_1		0x80	/* the draw mask is all ones */
#define AO_MASK_1_COPY		0x40	/* ... and the mix mode MM_COPY */
#define AO_MASK_1_INVERT	0x20	/* ... and the mix mode MM_INVERT */
#define AO_USE_INDEX		0x10	/* driver: use the color index */

typedef byte DrawMask[8];

/* the attributes of lines, areas and text */
typedef struct {
    ColorMapMode	CA_mapMode;
    Color		CA_colorIndex;
    RGBValue		CA_colorRGB;
    SystemDrawMask	CA_maskType;
    DrawMask		CA_mask;
    AttrOpts		CA_flags;
} CommonAttr;

#define MAX_DASH_ARRAY_PAIRS	5
typedef word DashPairArray[MAX_DASH_ARRAY_PAIRS * 2];

typedef struct {
    byte		DS_nPairs;
    byte		DS_offset;
    DashPairArray	DS_pairs;
} DashStruct;

typedef struct {
    byte		GCP_pattern;	/* PatternType */
    byte		GCP_system;	/* SystemHatch or SystemBitmap */
    ChunkHandle		GCP_custom;	/* in the GState block */
} GrCommonPattern;

typedef ByteEnum GOpt;
#define GO_FALL_THRU		0
#define GO_SPECIAL_CASE		1
#define GO_JNE			2

typedef struct {
    word	GPD_ax;
    word	GPD_bx;
    word	GPD_cx;
    word	GPD_dx;
} GrPrivateData;

typedef ByteFlags PathFlags;
#define PF_DEFINING_PATH	0x80
#define PF_DEF_PATH_WIN_LOCKED	0x40
#define PF_FILL			0x02
#define PF_STROKE		0x01

typedef struct {
    word		FCA_fontID;
    WBFixed		FCA_pointsize;
    byte		FCA_weight;
    byte		FCA_width;
    byte		FCA_superPos;
    byte		FCA_superSize;
    byte		FCA_subPos;
    byte		FCA_subSize;
    TextStyle		FCA_textStyle;
} FontCommonAttrs;

typedef struct {
    LMemBlockHeader	GS_header;
    Handle		GS_window;	/* its window, or 0 */
    Handle		GS_gstring;
    Handle		GS_saveStateLink;
    TMatrix		GS_TMatrix;
    ChunkHandle		GS_defTMatrix;
    ChunkHandle		GS_savedMatrix;
    ChunkHandle		GS_currentPath;
    ChunkHandle		GS_clipPath;
    ChunkHandle		GS_winClipPath;
    GrPrivateData	GS_privData;
    PointDWFixed	GS_penPos;
    byte		GS_xShift;	/* masks rotated by, in x */
    byte		GS_yShift;
    CommonAttr		GS_lineAttr;
    LineStyle		GS_lineStyle;
    LineJoin		GS_lineJoin;
    LineEnd		GS_lineEnd;
    WWFixed		GS_lineWidth;
    word		GS_scaledLineWid;
    word		GS_inverseMiterLimit;
    MixMode		GS_mixMode;
    byte		GS_numOfDashPairs;
    byte		GS_dashSkipDistance;
    DashPairArray	GS_dashPairArray;
    CommonAttr		GS_areaAttr;
    CommonAttr		GS_textAttr;
    sword		GS_trackKernDegree;
    BBFixed		GS_trackKernValue;
    word		GS_pixelHeightM1;
    byte		GS_fontFirstChar;
    byte		GS_fontLastChar;
    GOpt		GS_complexOpcode;
    GOpt		GS_kernOp;
    GOpt		GS_optSpacePad;
    WBFixed		GS_textSpacePad;
    Handle		GS_fontHandle;
    sword		GS_minLSB;
    sword		GS_minTSB;
    byte		GS_optDrawMask;
    GOpt		GS_hyphenOpcode;
    byte		GS_fontFlags;
    word		GS_fontIndex;
    word		GS_textDrawOffset;
    GOpt		GS_drawOffsetOpcode;
    TextMode		GS_textMode;
    FontCommonAttrs	GS_fontAttr;
    GOpt		GS_drawCtrlOpcode;
    PathFlags		GS_pathFlags;
    Handle		GS_pathData;
    GrCommonPattern	GS_areaPattern;
    GrCommonPattern	GS_textPattern;
} GState;

#endif /* __INTERNAL_GSTATE_H */
