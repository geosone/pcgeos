/*
 * fontPCL4.h -- the soft font manager of the PCL 4 printers (the C form
 * of the structures of pcl4Constant.def): GEOS fonts drawn into bitmaps
 * at 300 dpi and downloaded as soft fonts, a page at a time.
 *
 * Port32: the manager's block holds the font entries as an array (the
 * asm kept them in a chunk array of an LMem block); a character's flags
 * are a byte array by character (the asm indexed its 3-byte
 * SoftCharEntry array by the character too, so it used the flags as
 * bytes as well); the soft font header is built as the bytes the printer
 * reads (big-endian).
 */
#ifndef FONTPCL4_H
#define FONTPCL4_H

#include "printcom.h"

#define HP_MAX_CHARS		256
#define MAX_SFE_NUMBER		64	/* soft fonts of a page */
#define HP_FONT_SIZE		2048	/* a font in the printer, empty */
#define MAX_BITMAP_SIZE		16384	/* the bytes of a character's band */
#define INITIAL_HP_FONT_ID	0
#define HP_FONT_HEADER_SIZE	64
#define HP_CHAR_HEADER_SIZE	16

/* SoftCharFlags */
#define SCF_ON_PAGE	0x02
#define SCF_IN_PRINTER	0x01

/* SoftFontFlags */
#define SFF_ON_PAGE	0x01

/* what makes a soft font: TestFontEntry, the start of a SoftFontEntry */
typedef struct {
    FontID		TFE_fontID;
    PrintTextStyle	TFE_style;
    word		TFE_pointsize;
    OptFontEntry	TFE_optFontEntry;	/* track kerning in dots */
} TestFontEntry;

typedef struct {
    TestFontEntry	SFE_test;
    word		SFE_fontTag;		/* the printer's font ID */
    byte		SFE_orientation;
    word		SFE_usage;
    byte		SFE_flags;
    dword		SFE_size;
    byte		SFE_numChars;
    byte		SFE_chars[HP_MAX_CHARS];	/* SoftCharFlags */
} SoftFontEntry;

/* the block of PS_expansionInfo: HPFontInfo */
typedef struct {
    word		HPFI_numFonts;
    word		HPFI_maxNumFonts;
    word		HPFI_maxPointsize;
    dword		HPFI_fontMemory;
    word		HPFI_heapCount;
    word		HPFI_allocID;
    VMFileHandle	HPFI_bmFileHandle;
    char		HPFI_bmFileName[16];
    byte		HPFI_header[HP_FONT_HEADER_SIZE];
    SoftFontEntry	HPFI_fontEntries[MAX_SFE_NUMBER];
} HPFontInfo;

/* fontUtilsPCL4.c: points (WBFixed as 1/256) to 300 dpi dots, rounded;
 * to quarter dots */
extern sword PointsToPCL(sdword v256);
extern sword PointsToPCL4(sdword v256);
/* the TextStyle of a PrintTextStyle; *hpStyle, *hpWeight */
extern TextStyle ConvertTextStyles(PrintTextStyle style, byte *hpStyle,
				   sbyte *hpWeight);

/* fontInternalPCL4.c: the entry of PS_curFont, NULL if none */
extern SoftFontEntry *IsFontInPrinter(PState *ps, HPFontInfo *fi);
extern SoftFontEntry *AddFontEntry(PState *ps, HPFontInfo *fi, word tag);

/* fontDownloadPCL4.c */
extern void SetTextAttrs(GStateHandle gs, const SoftFontEntry *sfe);
extern void CreateFontHeader(PState *ps, HPFontInfo *fi,
			     const SoftFontEntry *sfe);
extern Boolean SendFontHeader(PState *ps, HPFontInfo *fi,
			      const SoftFontEntry *sfe);
extern Boolean DownloadCharData(PState *ps, HPFontInfo *fi,
				const SoftFontEntry *sfe, byte c);

#endif /* FONTPCL4_H */
