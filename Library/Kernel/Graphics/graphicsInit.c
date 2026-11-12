/*
 * graphicsInit.c -- the fonts at the start (after graphicsInit.asm:
 * GrInitFonts, GrCreateFontBlock, GrProcessFont, AdjustFontWeight,
 * GrInitDefaultFont, InitAddInUseEntry; the default font of
 * bootInitfile.asm, GetFontId and GetFontSize).
 *
 * GrInitFonts makes the font info block and reads the header of each
 * font file (*.fnt) in SP_FONT: FontFileInfo, then the FontInfo from
 * FI_fontID on and its tables, all packed (DOS16).  Each becomes a
 * FontInfo chunk in the host layout, its table offsets recomputed, and
 * a FontsAvailEntry with the file's name.  GrInitDefaultFont then loads
 * the default font ([system] fontid, a font's name, and fontsize) into a
 * block of its own that stays.
 *
 * Port32: the font drivers are not loaded here yet (GrInitFontDriver);
 * without font files the kernel runs without fonts (the text calls draw
 * nothing) where DOS16 stopped with DEFAULT_FONT_NOT_FOUND.  The font
 * cache file of the asm (the FontInfo of the last start) is not needed.
 */
#include "graphicsInt.h"
#include <Internal/fontDr.h>
#include <fileEnum.h>
#include <initfile.h>
#include <stdlib.h>

MemHandle grFontBlock;
FontID grDefaultFontID = DEFAULT_FONT_ID;
word grDefaultFontSize = DEFAULT_FONT_SIZE;
Handle grDefaultFontHandle;
TMatrix grIdentityMatrix;

#define FONT_FILE_INFO_SIZE	8	/* "BSWF", minor, major, headerSize */
#define MAX_MAJOR_VER		1
#define FID_DEFAULT_FILE_HANDLES 8
#define FID_MIN_FILE_HANDLES	2
#define FID_MAX_FILE_HANDLES	32
#define DOS16_FI_FILE_HANDLE	2	/* the table offsets count it */

/* the attributes of a new GState (GState <>, FontCommonAttrs <>) */
void GrDefaultFontAttrs(FontCommonAttrs *fa)
{
    memset(fa, 0, sizeof(*fa));
    fa->FCA_fontID = grDefaultFontID;
    fa->FCA_pointsize.WBF_int = (sword) grDefaultFontSize;
    fa->FCA_weight = FW_NORMAL;
    fa->FCA_width = FWI_MEDIUM;
    fa->FCA_superPos = SPP_DEFAULT;
    fa->FCA_superSize = SPS_DEFAULT;
    fa->FCA_subPos = SBP_DEFAULT;
    fa->FCA_subSize = SBS_DEFAULT;
}

static word Le16(const byte *p)
{
    return (word) (p[0] | (p[1] << 8));
}

static dword Le32(const byte *p)
{
    return (dword) Le16(p) | ((dword) Le16(p + 2) << 16);
}

/* AdjustFontWeight: the weights of V1.x outlines (FontWeightEnum) */
static byte AdjustWeight(byte w)
{
    static const byte table[] = {80, 85, 90, 95, 100, 105, 110, 115, 120,
				 125};

    return w < sizeof(table) ? table[w] : w;
}

/* GrProcessFont: the header of a font file into the info block */
static void ProcessFont(const char *name)
{
    byte ffi[FONT_FILE_INFO_SIZE], *hdr;
    word size, psTab, psEnd, olTab, olEnd, nps = 0, nol = 0, i, total;
    FileHandle fh;
    ChunkHandle ch;
    FontInfo *fi;
    FontsAvailEntry *fa;
    PointSizeEntry *pse;
    OutlineDataEntry *ode;
    FontID id;

    fh = FileOpen(name, FILE_ACCESS_R | FILE_DENY_W);
    if (!fh)
	return;
    if (FileRead(fh, ffi, sizeof(ffi), TRUE) != sizeof(ffi) ||
	memcmp(ffi, "BSWF", 4) || ffi[5] > MAX_MAJOR_VER) {
	FileClose(fh, TRUE);
	return;
    }
    size = Le16(ffi + 6);
    hdr = size >= DOS16_FONT_INFO_RESIDENT ? malloc(size) : NULL;
    if (!hdr || FileRead(fh, hdr, size, TRUE) != size) {
	free(hdr);
	FileClose(fh, TRUE);
	return;
    }
    FileClose(fh, TRUE);

    /* the tables, at offsets from the DOS16 FontInfo (with its handle) */
    psTab = Le16(hdr + 25);
    psEnd = Le16(hdr + 27);
    olTab = Le16(hdr + 29);
    olEnd = Le16(hdr + 31);
    if (psTab && psEnd >= psTab && psEnd - DOS16_FI_FILE_HANDLE <= size)
	nps = (word) ((psEnd - psTab) / DOS16_POINT_SIZE_ENTRY);
    if (olTab && olEnd >= olTab && olEnd - DOS16_FI_FILE_HANDLE <= size)
	nol = (word) ((olEnd - olTab) / DOS16_OUTLINE_DATA_ENTRY);
    total = (word) (sizeof(FontInfo) + nps * sizeof(PointSizeEntry) +
		    nol * sizeof(OutlineDataEntry));

    ch = LMemAlloc(grFontBlock, total);
    fi = LMemDerefHandles(grFontBlock, ch);
    memset(fi, 0, total);
    fi->FI_fontID = id = Le16(hdr);
    fi->FI_maker = Le16(hdr + 2);
    fi->FI_family = hdr[4];
    memcpy(fi->FI_faceName, hdr + 5, FID_NAME_LEN);
    fi->FI_faceName[FID_NAME_LEN - 1] = 0;
    if (nps) {
	const byte *p = hdr + psTab - DOS16_FI_FILE_HANDLE;
	fi->FI_pointSizeTab = sizeof(FontInfo);
	fi->FI_pointSizeEnd = (word) (sizeof(FontInfo) +
				      nps * sizeof(PointSizeEntry));
	pse = (PointSizeEntry *) ((byte *) fi + fi->FI_pointSizeTab);
	for (i = 0; i < nps; i++, p += DOS16_POINT_SIZE_ENTRY) {
	    pse[i].PSE_style = p[0];
	    pse[i].PSE_pointSize.WBF_frac = p[1];
	    pse[i].PSE_pointSize.WBF_int = (sword) Le16(p + 2);
	    pse[i].PSE_dataSize = Le16(p + 4);
	    pse[i].PSE_filePosLo = Le16(p + 6);
	    pse[i].PSE_filePosHi = Le16(p + 8);
	}
    }
    if (nol) {
	const byte *p = hdr + olTab - DOS16_FI_FILE_HANDLE;
	fi->FI_outlineTab = (word) (sizeof(FontInfo) +
				    nps * sizeof(PointSizeEntry));
	fi->FI_outlineEnd = total;
	ode = (OutlineDataEntry *) ((byte *) fi + fi->FI_outlineTab);
	for (i = 0; i < nol; i++, p += DOS16_OUTLINE_DATA_ENTRY) {
	    OutlineEntry *oe[3];
	    int k;
	    oe[0] = &ode[i].ODE_header;
	    oe[1] = &ode[i].ODE_first;
	    oe[2] = &ode[i].ODE_second;
	    ode[i].ODE_style = p[0];
	    ode[i].ODE_weight = AdjustWeight(p[1]);
	    for (k = 0; k < 3; k++) {
		oe[k]->OE_offset = Le32(p + 2 + 8 * k);
		oe[k]->OE_size = Le16(p + 6 + 8 * k);
		oe[k]->OE_handle = 0;
	    }
	}
    }
    free(hdr);

    /* a font already there (another file of it) keeps the first */
    if (GrFontAvailEntry(id)) {
	LMemFreeHandles(grFontBlock, ch);
	return;
    }
    {
	word n = LMemGetChunkSizeHandles(grFontBlock, FONTS_AVAIL_HANDLE);
	LMemReAllocHandles(grFontBlock, FONTS_AVAIL_HANDLE,
			   (word) (n + sizeof(FontsAvailEntry)));
	fa = (FontsAvailEntry *) ((byte *) LMemDerefHandles(grFontBlock,
						FONTS_AVAIL_HANDLE) + n);
    }
    memset(fa, 0, sizeof(*fa));
    fa->FAE_fontID = id;
    strncpy(fa->FAE_fileName, name, FONT_FILE_LENGTH - 1);
    fa->FAE_infoHandle = ch;
}

/* GrCreateFontBlock: the info block with its four chunks */
static void CreateFontBlock(void)
{
    ChunkHandle ch[4];
    int i;

    grFontBlock = MemAllocLMem(LMEM_TYPE_FONT_BLK, 0);
    MemModifyFlags(grFontBlock, HF_SHARABLE, 0);
    MemLock(grFontBlock);
    for (i = 0; i < 4; i++)
	ch[i] = LMemAlloc(grFontBlock, 0);
    EC_CHECK(ch[0] == FONTS_AVAIL_HANDLE && ch[1] == FONTS_IN_USE_HANDLE &&
	     ch[2] == FONT_DRIVERS_HANDLE && ch[3] == FONT_MOD_DATA_HANDLE,
	     GRAPHICS_GR_INIT_FONTS_ERROR);
    (void) ch;
}

void GrInitFonts(void)
{
    FileEnumParams p;
    MemHandle names = 0;
    word n, i, files;

    GrMatIdentity(&grIdentityMatrix);
    CreateFontBlock();

    FilePushDir();
    FileSetStandardPath(SP_FONT);
    {
	memset(&p, 0, sizeof(p));
	p.FEP_searchFlags = FESF_NON_GEOS | FESF_GEOS_NON_EXECS |
			    FESF_CALLBACK;
	p.FEP_returnAttrs = (FileExtAttrDesc *) FESRT_NAME;
	p.FEP_returnSize = sizeof(FileLongName);
	p.FEP_bufSize = FE_BUFSIZE_UNLIMITED;
	p.FEP_callback = (void *) FESC_WILDCARD;
	p.FEP_cbData1 = (uintptr_t) "*.fnt";
	p.FEP_cbData2 = 1;			/* without case */
	n = FileEnum(&p, &names, NULL);
	if (names) {
	    char *name = MemLock(names);
	    for (i = 0; i < n; i++)
		ProcessFont(name + i * sizeof(FileLongName));
	    MemFree(names);
	}
    }
    FilePopDir();

    /* the mod data was for a font cache file; now the open files */
    if (InitFileReadInteger("system", "maxfontfiles", &files))
	files = FID_DEFAULT_FILE_HANDLES;
    if (files < FID_MIN_FILE_HANDLES)
	files = FID_MIN_FILE_HANDLES;
    if (files > FID_MAX_FILE_HANDLES)
	files = FID_MAX_FILE_HANDLES;
    LMemReAllocHandles(grFontBlock, FONT_FILE_CACHE_HANDLE,
		       (word) (files * sizeof(FontID)));
    memset(LMemDerefHandles(grFontBlock, FONT_FILE_CACHE_HANDLE), 0,
	   files * sizeof(FontID));
    MemUnlock(grFontBlock);
}

/* InitAddInUseEntry: the default font in use, by the kernel */
static void AddDefaultInUse(TextStyle style)
{
    FontCommonAttrs fa;
    FontsInUseEntry *e;
    word index;

    GrDefaultFontAttrs(&fa);
    index = GrAddInUseEntry(&fa, style, NULL);
    e = (FontsInUseEntry *) ((byte *) GrFontChunk(FONTS_IN_USE_HANDLE,
						  NULL) + index);
    e->FIUE_dataHandle = grDefaultFontHandle;
    e->FIUE_refCount = 1;
}

/* GrInitDefaultFont: the font of [system] fontid and fontsize (or the
 * first one with bitmaps), at its nearest size, loaded to stay */
void GrInitDefaultFont(void)
{
    char name[FID_NAME_LEN + 1];
    word size, n;
    FontID id = FID_INVALID;
    sdword found;
    TextStyle style;
    FontInfo *fi;
    PointSizeEntry *pse, copy;
    FileHandle fh;
    Handle h;
    word i;

    if (!InitFileReadStringBuffer("system", "fontid", name, sizeof(name),
				  &n))
	id = GrCheckFontAvailName(FEF_BITMAPS | FEF_STRING | FEF_DOWNCASE,
				  0, name);
    if (id == FID_INVALID)
	id = GrCheckFontAvailID(FEF_BITMAPS, 0, grDefaultFontID);
    if (id == FID_INVALID) {
	FontEnumStruct fes;
	if (!GrEnumFonts(&fes, 1, FEF_BITMAPS, 0))
	    return;				/* no fonts */
	id = fes.FES_ID;
    }
    if (InitFileReadInteger("system", "fontsize", &size))
	size = grDefaultFontSize;

    FontDrLockInfoBlock();
    fi = FontDrFindFontInfo(NULL, id);
    if (!GrNearestBitmap(fi, (sdword) size << 8, 0, TRUE, &found, &style)) {
	FontDrUnlockInfoBlock();
	return;
    }
    grDefaultFontID = id;
    grDefaultFontSize = (word) (found >> 8);
    fh = GrEnsureFontFileOpen(id);
    fi = FontDrFindFontInfo(NULL, id);
    /* InitFindFace: the entry of that size and style */
    pse = GrPointSizeTab(fi, &n);
    for (i = 0; i < n; i++)
	if (pse[i].PSE_style == style &&
	    pse[i].PSE_pointSize.WBF_int == (sword) (found >> 8) &&
	    pse[i].PSE_pointSize.WBF_frac == (byte) found)
	    break;
    if (i == n || !fh) {
	FontDrUnlockInfoBlock();
	return;
    }
    copy = pse[i];
    h = MemAllocSetOwner(0, 16, HF_SHARABLE | HF_SWAPABLE,
			 HAF_NO_ERR);
    if (!GrLoadBitmapFont(fh, &copy, h)) {
	MemFree(h);
	FontDrUnlockInfoBlock();
	return;
    }
    grDefaultFontHandle = h;
    AddDefaultInUse(style);
    FontDrUnlockInfoBlock();
}
