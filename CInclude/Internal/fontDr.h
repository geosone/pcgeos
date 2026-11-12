/*
 * fontDr.h -- the font manager and the font drivers (after
 * Include/Internal/fontDr.def and the font structures of
 * Library/Kernel/Graphics/graphicsConstant.def).
 *
 * Port32: the structures in memory have the host's natural alignment;
 * the font files keep the DOS16 layout and the kernel converts what it
 * reads from them (Port32/ABI.md, fonts).  SBCS only.
 */
#ifndef __INTERNAL_FONTDR_H
#define __INTERNAL_FONTDR_H

#include <geos.h>
#include <font.h>
#include <fontID.h>
#include <graphics.h>
#include <file.h>
#include <driver.h>

/* characters that are not there (CTE_dataOffset) */
#define CHAR_NOT_EXIST		0	/* doesn't exist at all */
#define CHAR_NOT_LOADED		1	/* exists, but not loaded */
#define CHAR_NOT_BUILT		2	/* exists, but not built */
#define CHAR_MISSING		3	/* <= is one of the above */

#define KERNEL_STYLES		(TS_UNDERLINE | TS_STRIKE_THRU)

#define MAX_FONT_SIZE		(1024 * 10)
#define MAX_FONT_SIZE_GREY	(1024 * 48)

#define FONT_MAN_ID		0x20

/* the picture of a character: rows of (CD_pictureWidth + 7) / 8 bytes */
typedef struct {
    byte	CD_pictureWidth;	/* width of the picture in bits */
    byte	CD_numRows;		/* number of rows */
    sbyte	CD_yoff;		/* offset to the first row */
    sbyte	CD_xoff;		/* offset to the first column */
    byte	CD_data[1];
} CharData;
#define SIZE_CHAR_HEADER	4

/* greyscale: a coverage nibble (0 .. 15) a pixel */
#define GREY_LEVELS		16
#define GREY_PHASES		4
#define GREY_PHASE_ZERO		2

/* a character as a region (FBF_IS_REGION) */
typedef struct {
    sword	RCD_yoff;
    sword	RCD_xoff;
    word	RCD_size;		/* size of the region in bytes */
    Rectangle	RCD_bounds;
    word	RCD_data[1];
} RegionCharData;

typedef ByteFlags CharTableFlags;
#define CTF_NEGATIVE_LSB	0x40
#define CTF_ABOVE_ASCENT	0x20
#define CTF_BELOW_DESCENT	0x10
#define CTF_NO_DATA		0x08
#define CTF_IS_FIRST_KERN	0x04
#define CTF_IS_SECOND_KERN	0x02
#define CTF_NOT_VISIBLE		0x01

typedef struct {
    word		CTE_dataOffset;	/* offset of the CharData in the
					 * FontBuf, or CHAR_NOT_* */
    WBFixed		CTE_width;
    CharTableFlags	CTE_flags;
    word		CTE_usage;	/* LRU count */
} CharTableEntry;

typedef struct {
    byte	KP_char1;		/* the right character */
    byte	KP_char2;		/* the left character */
} KernPair;

typedef ByteFlags FontBufFlags;
#define FBF_DEFAULT_FONT	0x80
#define FBF_MAPPED_FONT		0x40
#define FBF_IS_GREY		0x20
#define FBF_IS_OUTLINE		0x10
#define FBF_IS_REGION		0x08
#define FBF_IS_COMPLEX		0x04
#define FBF_IS_INVALID		0x02
#define FBF_GREY_PHASES		0x01
#define FONT_GS_FLAGS		(FBF_DEFAULT_FONT | FBF_MAPPED_FONT)

typedef struct {
    WWFixed	FM_11;
    WWFixed	FM_12;
    WWFixed	FM_21;
    WWFixed	FM_22;
} FontMatrix;

/* the block of a font: this header, the CharTableEntry of each character
 * from FB_firstChar to FB_lastChar (FB_CHAR_TABLE), then the kerning
 * tables and the character data, all at offsets from the start */
typedef struct {
    word		FB_dataSize;	/* bytes used of the block */
    FontMaker		FB_maker;
    WBFixed		FB_avgwidth;
    WBFixed		FB_maxwidth;
    WBFixed		FB_heightAdjust;
    WBFixed		FB_height;
    WBFixed		FB_accent;
    WBFixed		FB_mean;
    WBFixed		FB_baseAdjust;
    WBFixed		FB_baselinePos;
    WBFixed		FB_descent;
    WBFixed		FB_extLeading;
    word		FB_kernCount;
    word		FB_kernPairPtr;	/* offset of the KernPair table */
    word		FB_kernValuePtr; /* offset of the BBFixed values */
    byte		FB_firstChar;
    byte		FB_lastChar;
    byte		FB_defaultChar;
    WBFixed		FB_underPos;
    WBFixed		FB_underThickness;
    WBFixed		FB_strikePos;
    WBFixed		FB_aboveBox;
    WBFixed		FB_belowBox;
    sword		FB_minLSB;
    sword		FB_minTSB;
    sword		FB_maxBSB;
    sword		FB_maxRSB;
    word		FB_pixHeight;
    FontBufFlags	FB_flags;
    word		FB_heapCount;
} FontBuf;

#define FB_CHAR_TABLE(fb)	((CharTableEntry *) ((FontBuf *) (fb) + 1))

/* the DOS16 sizes, as in the font files */
#define DOS16_FONT_BUF_SIZE	71	/* without FB_charTable */
#define DOS16_CHAR_TABLE_ENTRY	8

typedef struct {
    dword	OE_offset;		/* in the file */
    word	OE_size;
    Handle	OE_handle;		/* if loaded */
} OutlineEntry;

typedef struct {
    TextStyle		ODE_style;
    byte		ODE_weight;	/* AdjustedWeight */
    OutlineEntry	ODE_header;
    OutlineEntry	ODE_first;
    OutlineEntry	ODE_second;
} OutlineDataEntry;
#define DOS16_OUTLINE_DATA_ENTRY 26

typedef word OutlineDataFlag;		/* which part of the outline */
#define ODF_HEADER	0
#define ODF_PART1	1
#define ODF_PART2	2

typedef struct {
    TextStyle	PSE_style;
    WBFixed	PSE_pointSize;
    word	PSE_dataSize;
    word	PSE_filePosLo;
    word	PSE_filePosHi;
} PointSizeEntry;
#define DOS16_POINT_SIZE_ENTRY	10

/* the chunks of the font info block (an LMem block) */
#define FONTS_AVAIL_HANDLE	(sizeof(LMemBlockHeader) + 0)
#define FONTS_IN_USE_HANDLE	(sizeof(LMemBlockHeader) + 2)
#define FONT_DRIVERS_HANDLE	(sizeof(LMemBlockHeader) + 4)
#define FONT_MOD_DATA_HANDLE	(sizeof(LMemBlockHeader) + 6)
#define FONT_FILE_CACHE_HANDLE	FONT_MOD_DATA_HANDLE

#define FONT_FILE_LENGTH	FILE_LONGNAME_BUFFER_SIZE

typedef struct {
    FontID	FAE_fontID;
    char	FAE_fileName[FONT_FILE_LENGTH];
    ChunkHandle	FAE_infoHandle;		/* the FontInfo chunk */
} FontsAvailEntry;

/* a font: this, then its PointSizeEntry and OutlineDataEntry tables at
 * the offsets (from the start of the FontInfo) given */
typedef struct {
    FileHandle	FI_fileHandle;		/* the font file, when open */
    FontID	FI_fontID;
    FontMaker	FI_maker;
    FontAttrs	FI_family;
    char	FI_faceName[FID_NAME_LEN];
    word	FI_pointSizeTab;
    word	FI_pointSizeEnd;
    word	FI_outlineTab;
    word	FI_outlineEnd;
} FontInfo;
/* in a font file, after FontFileInfo: from FI_fontID on, packed */
#define DOS16_FONT_INFO_RESIDENT 33

typedef struct {
    FontCommonAttrs	FIUE_attrs;
    Handle		FIUE_dataHandle;	/* the FontBuf block */
    word		FIUE_refCount;
    FontBufFlags	FIUE_flags;
    FontMatrix		FIUE_matrix;
} FontsInUseEntry;

typedef Boolean FontDriverStrategy(word function, DriverRegs *regs);

typedef struct {
    FontMaker		DAE_makerID;
    GeodeHandle		DAE_driverHandle;
    FontDriverStrategy	*DAE_strategy;
} DriversAvailEntry;

typedef struct {
    FileDateAndTime	FMI_modified;
    dword		FMI_fileSize;
} FontModificationInfo;

/* the font driver functions */
#define DR_FONT_GEN_CHAR	8
#define DR_FONT_GEN_WIDTHS	10
#define DR_FONT_CHAR_METRICS	12
#define DR_FONT_INIT_FONTS	14
#define DR_FONT_GEN_PATH	16
#define DR_FONT_GEN_IN_REGION	18

typedef struct {
    DriverInfoStruct	FDIS_common;
    FontMaker		FDIS_maker;
} FontDriverInfoStruct;

typedef enum {
    FADE_TOO_MANY_FONTS = 0,
    FADE_FONT_ALREADY_EXISTS = 2,
    FADE_FONT_IN_USE = 4,
    FADE_NO_SUCH_FONT = 6,
    FADE_DEFAULT_FONT = 8
} FontAddDeleteError;

/* the kernel's side (graphicsChars.c, graphicsFontDriver.c) */
void *FontDrLockInfoBlock(void);
void FontDrUnlockInfoBlock(void);
/* the FontBuf of a GState, locked; NULL if there is no font */
FontBuf *FontDrLockFont(GStateHandle gstate);
void FontDrUnlockFont(GStateHandle gstate);
/* the FontInfo of a font in the locked info block, or NULL */
FontInfo *FontDrFindFontInfo(void *infoBlock, FontID id);
/* the size of a character's data in a FontBuf */
word FontDrCharDataSize(const FontBuf *fb, word dataOffset);

#endif /* __INTERNAL_FONTDR_H */
