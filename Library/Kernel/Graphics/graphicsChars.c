/*
 * graphicsChars.c -- the font manager: fonts of GStates (after
 * graphicsChars.asm: GrSetFont, GrGetDefFontID, FindFont, IsFontInUse,
 * IsFontAvail, IsFaceAvail, AddInUseEntry, ReallocFont, ReloadFont,
 * EnsureFontFileOpen, the font locks and UpdateFontOpts; LibFindBestFace
 * and the nearest size of graphicsFont.asm).
 *
 * The font info block (grFontBlock, an LMem block, locked with
 * MemThreadGrab) holds four chunks at fixed handles (fontDr.h): the
 * fonts available (FontsAvailEntry, each with a FontInfo chunk), the
 * fonts in use (FontsInUseEntry), the font drivers (DriversAvailEntry)
 * and the open font files (FontIDs, the most recent last).
 *
 * A font in use is a FontBuf block, allocated discarded and filled when
 * it is first locked: a bitmap face from its font file, converted from
 * the DOS16 layout (GrLoadBitmapFont), or the widths from the font's
 * driver (DR_FONT_GEN_WIDTHS) for outlines.  A GState keeps the block
 * (GS_fontHandle) and the entry's offset in the in-use chunk
 * (GS_fontIndex); the entry counts the GStates.  The default font is
 * loaded at the start, fixed, and taken without the block's semaphore.
 * A font that is not there is substituted (LibFindBestFace): its nearest
 * bitmap size, else its outlines, else the default font, and the
 * GState's attributes say so (FBF_MAPPED_FONT).
 *
 * Not yet: greyscale text (FontGreyRequested), deleting fonts
 * (FBF_IS_INVALID), DBCS.
 */
#include "graphicsInt.h"
#include <Internal/fontDr.h>
#include <file.h>
#include <stdlib.h>

#define FID_MAKER_MASK	0xf0		/* of the high byte */
#define FID_INIT_SIZE	16

/* ---------------------------------------------------------------------
 * The info block
 * ------------------------------------------------------------------- */

void *FontDrLockInfoBlock(void)
{
    return grFontBlock ? MemThreadGrab(grFontBlock) : NULL;
}

void FontDrUnlockInfoBlock(void)
{
    if (grFontBlock)
	MemThreadRelease(grFontBlock);
}

void *GrFontChunk(ChunkHandle ch, word *size)
{
    if (size)
	*size = LMemGetChunkSizeHandles(grFontBlock, ch);
    return LMemDerefHandles(grFontBlock, ch);
}

/* IsFontAvail: the entry of exactly this font */
FontsAvailEntry *GrFontAvailEntry(FontID id)
{
    word size, i;
    FontsAvailEntry *fa = GrFontChunk(FONTS_AVAIL_HANDLE, &size);

    for (i = 0; i < size / sizeof(FontsAvailEntry); i++)
	if (fa[i].FAE_fontID == id)
	    return &fa[i];
    return NULL;
}

FontInfo *FontDrFindFontInfo(void *infoBlock, FontID id)
{
    FontsAvailEntry *fa;

    (void) infoBlock;
    fa = GrFontAvailEntry(id);
    return fa ? GrFontChunk(fa->FAE_infoHandle, NULL) : NULL;
}

/* IsFontAvailable: the font, or one of another maker with the same
 * number; *id becomes the one found */
FontInfo *GrFontAvailable(FontID *id)
{
    word size, i;
    FontsAvailEntry *fa = GrFontChunk(FONTS_AVAIL_HANDLE, &size);
    FontID plain = (FontID) (*id & ~(FID_MAKER_MASK << 8));

    for (i = 0; i < size / sizeof(FontsAvailEntry); i++)
	if (fa[i].FAE_fontID == *id ||
	    ((fa[i].FAE_fontID & (FID_MAKER_MASK << 8)) &&
	     (FontID) (fa[i].FAE_fontID & ~(FID_MAKER_MASK << 8)) == plain)) {
	    *id = fa[i].FAE_fontID;
	    return GrFontChunk(fa[i].FAE_infoHandle, NULL);
	}
    return NULL;
}

PointSizeEntry *GrPointSizeTab(FontInfo *fi, word *n)
{
    *n = fi->FI_pointSizeTab ? (word) ((fi->FI_pointSizeEnd -
					fi->FI_pointSizeTab) /
				       sizeof(PointSizeEntry)) : 0;
    return (PointSizeEntry *) ((byte *) fi + fi->FI_pointSizeTab);
}

OutlineDataEntry *GrOutlineTab(FontInfo *fi, word *n)
{
    *n = fi->FI_outlineTab ? (word) ((fi->FI_outlineEnd -
				      fi->FI_outlineTab) /
				     sizeof(OutlineDataEntry)) : 0;
    return (OutlineDataEntry *) ((byte *) fi + fi->FI_outlineTab);
}

/* ---------------------------------------------------------------------
 * Nearest sizes and styles (graphicsFont.asm)
 * ------------------------------------------------------------------- */

/* CheckCloserStyles: a style no richer than asked; the one with the most
 * of what is asked so far in *best.  TRUE if a subset */
static Boolean CloserStyles(TextStyle asked, TextStyle cand, TextStyle *best)
{
    if ((TextStyle) (cand & asked) != cand)
	return FALSE;
    if ((cand & asked) >= (*best & asked))
	*best = cand;
    return TRUE;
}

/* FindNearestBitmap: the closest size (absolute or, if !absolute, the
 * closest not larger) and its best style; FALSE if none.  Sizes in
 * 1/256 */
Boolean GrNearestBitmap(FontInfo *fi, sdword size, TextStyle style,
			Boolean absolute, sdword *found, TextStyle *foundStyle)
{
    word n, i;
    PointSizeEntry *pse = GrPointSizeTab(fi, &n);
    sdword best = -1, bestDist = 0;
    TextStyle bs = 0;

    for (i = 0; i < n; i++) {
	sdword s = ((sdword) pse[i].PSE_pointSize.WBF_int << 8) |
		   pse[i].PSE_pointSize.WBF_frac;
	sdword dist = size - s;
	if (absolute && dist < 0)
	    dist = -dist;
	if (dist < 0)
	    continue;
	if (best < 0 || dist < bestDist) {
	    best = s;
	    bestDist = dist;
	    bs = pse[i].PSE_style;
	} else if (dist == bestDist)
	    (void) CloserStyles(style, pse[i].PSE_style, &bs);
    }
    if (best < 0)
	return FALSE;
    *found = best;
    *foundStyle = bs;
    return TRUE;
}

/* FindNearestOutline: the best style of the outlines */
Boolean GrNearestOutline(FontInfo *fi, TextStyle style,
			 TextStyle *foundStyle)
{
    word n, i;
    OutlineDataEntry *ode = GrOutlineTab(fi, &n);
    TextStyle bs = 0;
    Boolean any = FALSE;

    for (i = 0; i < n; i++)
	if (CloserStyles(style, ode[i].ODE_style, &bs))
	    any = TRUE;
    if (any)
	*foundStyle = bs;
    return any;
}

/* LibFindBestFace: the GState's font as near as there is; TRUE if
 * outlines */
static Boolean FindBestFace(GState *gs)
{
    FontCommonAttrs *fa = &gs->GS_fontAttr;
    FontID id = fa->FCA_fontID;
    TextStyle keep = (TextStyle) (fa->FCA_textStyle & KERNEL_STYLES);
    TextStyle style = (TextStyle) (fa->FCA_textStyle & ~KERNEL_STYLES);
    sdword size = ((sdword) fa->FCA_pointsize.WBF_int << 8) |
		  fa->FCA_pointsize.WBF_frac;
    FontInfo *fi = GrFontAvailable(&id);
    Boolean outline = FALSE;
    sdword found;
    TextStyle fs;

    if (fi && GrNearestBitmap(fi, size, style, TRUE, &found, &fs)) {
	size = found;
	style = fs;
	fa->FCA_weight = FW_NORMAL;
	fa->FCA_width = FWI_MEDIUM;
    } else if (fi && fi->FI_outlineEnd)
	outline = TRUE;
    else {
	id = grDefaultFontID;
	size = (sdword) grDefaultFontSize << 8;
	style = 0;
	fa->FCA_weight = FW_NORMAL;
	fa->FCA_width = FWI_MEDIUM;
    }
    fa->FCA_fontID = id;
    fa->FCA_textStyle = (TextStyle) (style | keep);
    fa->FCA_pointsize.WBF_int = (sword) (size >> 8);
    fa->FCA_pointsize.WBF_frac = (byte) size;
    gs->GS_fontFlags |= FBF_MAPPED_FONT;
    return outline;
}

/* ---------------------------------------------------------------------
 * Fonts in use
 * ------------------------------------------------------------------- */

static Boolean SameAttrs(const FontCommonAttrs *a, const FontCommonAttrs *b)
{
    return (Boolean) (a->FCA_fontID == b->FCA_fontID &&
		      a->FCA_pointsize.WBF_int == b->FCA_pointsize.WBF_int &&
		      a->FCA_pointsize.WBF_frac == b->FCA_pointsize.WBF_frac &&
		      a->FCA_weight == b->FCA_weight &&
		      a->FCA_width == b->FCA_width &&
		      a->FCA_superPos == b->FCA_superPos &&
		      a->FCA_superSize == b->FCA_superSize &&
		      a->FCA_subPos == b->FCA_subPos &&
		      a->FCA_subSize == b->FCA_subSize);
}

static void MatrixOf(const TMatrix *m, FontMatrix *fm)
{
    fm->FM_11 = m->TM_11;
    fm->FM_12 = m->TM_12;
    fm->FM_21 = m->TM_21;
    fm->FM_22 = m->TM_22;
}

/* IsFontInUse: the entry of the GState's font under the matrix */
static FontsInUseEntry *FontInUse(const GState *gs, TextStyle style,
				  const TMatrix *m)
{
    word size, i;
    FontsInUseEntry *e = GrFontChunk(FONTS_IN_USE_HANDLE, &size);
    FontBufFlags flags = (FontBufFlags) ((m->TM_flags & TM_COMPLEX) ?
					 FBF_IS_COMPLEX : 0);
    FontMatrix fm;

    MatrixOf(m, &fm);
    for (i = 0; i < size / sizeof(FontsInUseEntry); i++) {
	if (!e[i].FIUE_dataHandle ||
	    e[i].FIUE_attrs.FCA_textStyle != style ||
	    !SameAttrs(&e[i].FIUE_attrs, &gs->GS_fontAttr) ||
	    e[i].FIUE_flags != flags)
	    continue;
	if ((flags & FBF_IS_COMPLEX) &&
	    memcmp(&e[i].FIUE_matrix, &fm, sizeof(fm)))
	    continue;
	return &e[i];
    }
    return NULL;
}

/* IsFaceAvail: the bitmap of the GState's font, size and style; only for
 * simple matrices and plain attributes */
static PointSizeEntry *FaceAvail(const GState *gs, FontInfo *fi,
				 TextStyle style, const TMatrix *m)
{
    word n, i;
    PointSizeEntry *pse;

    style &= (TextStyle) ~KERNEL_STYLES;
    if ((m->TM_flags & TM_COMPLEX) ||
	(style & (TS_SUPERSCRIPT | TS_SUBSCRIPT)) ||
	gs->GS_fontAttr.FCA_width != FWI_MEDIUM ||
	gs->GS_fontAttr.FCA_weight != FW_NORMAL)
	return NULL;
    pse = GrPointSizeTab(fi, &n);
    for (i = 0; i < n; i++)
	if (pse[i].PSE_style == style &&
	    pse[i].PSE_pointSize.WBF_int ==
	    gs->GS_fontAttr.FCA_pointsize.WBF_int &&
	    pse[i].PSE_pointSize.WBF_frac ==
	    gs->GS_fontAttr.FCA_pointsize.WBF_frac)
	    return &pse[i];
    return NULL;
}

/* AddInUseEntry: a free entry, or a new one; its offset in the chunk */
word GrAddInUseEntry(const FontCommonAttrs *attrs, TextStyle style,
		     const TMatrix *m)
{
    word size, i;
    FontsInUseEntry *e = GrFontChunk(FONTS_IN_USE_HANDLE, &size);

    for (i = 0; i < size / sizeof(FontsInUseEntry); i++)
	if (!e[i].FIUE_dataHandle)
	    break;
    if (i == size / sizeof(FontsInUseEntry)) {
	LMemReAllocHandles(grFontBlock, FONTS_IN_USE_HANDLE,
			   (word) (size + sizeof(FontsInUseEntry)));
	e = GrFontChunk(FONTS_IN_USE_HANDLE, NULL);
    }
    memset(&e[i], 0, sizeof(e[i]));
    e[i].FIUE_attrs = *attrs;
    e[i].FIUE_attrs.FCA_textStyle = style;
    if (m && (m->TM_flags & TM_COMPLEX)) {
	e[i].FIUE_flags = FBF_IS_COMPLEX;
	MatrixOf(m, &e[i].FIUE_matrix);
    }
    return (word) (i * sizeof(FontsInUseEntry));
}

static FontsInUseEntry *InUseAt(word index)
{
    return (FontsInUseEntry *) ((byte *) GrFontChunk(FONTS_IN_USE_HANDLE,
						     NULL) + index);
}

/* FindFont: the font of the GState into GS_fontHandle and GS_fontIndex */
static void FindFont(GState *gs, const TMatrix *m)
{
    TextStyle style;
    FontsInUseEntry *e;
    FontInfo *fi;
    word index;
    Handle h;

    gs->GS_fontFlags = 0;
    FontDrLockInfoBlock();
    for (;;) {
	FontID id = gs->GS_fontAttr.FCA_fontID;
	style = (TextStyle) (gs->GS_fontAttr.FCA_textStyle & ~KERNEL_STYLES);
	e = FontInUse(gs, style, m);
	if (e)
	    break;
	fi = GrFontAvailable(&id);
	if (fi && id == gs->GS_fontAttr.FCA_fontID &&
	    (FaceAvail(gs, fi, style, m) || fi->FI_outlineEnd)) {
	    index = GrAddInUseEntry(&gs->GS_fontAttr, style, m);
	    h = MemAllocSetOwner(0, FID_INIT_SIZE,
				 HF_SHARABLE | HF_DISCARDABLE | HF_DISCARDED,
				 HAF_NO_ERR);
	    e = InUseAt(index);
	    e->FIUE_dataHandle = h;
	    break;
	}
	if (!grDefaultFontHandle && !fi) {	/* no fonts at all */
	    gs->GS_fontHandle = 0;
	    FontDrUnlockInfoBlock();
	    return;
	}
	if (!FindBestFace(gs))
	    m = &grIdentityMatrix;		/* a bitmap: unscaled */
    }
    e->FIUE_refCount++;
    gs->GS_fontHandle = e->FIUE_dataHandle;
    gs->GS_fontIndex = (word) ((byte *) e -
			       (byte *) GrFontChunk(FONTS_IN_USE_HANDLE,
						    NULL));
    if (gs->GS_fontHandle == grDefaultFontHandle)
	gs->GS_fontFlags |= FBF_DEFAULT_FONT;
    FontDrUnlockInfoBlock();
}

/* InvalidateFont: the GState lets its font go */
void GrInvalidateFont(GState *gs)
{
    if (!gs->GS_fontHandle)
	return;
    if (grFontBlock && !(gs->GS_fontFlags & FBF_DEFAULT_FONT)) {
	FontsInUseEntry *e;
	FontDrLockInfoBlock();
	e = InUseAt(gs->GS_fontIndex);
	if (e->FIUE_dataHandle == gs->GS_fontHandle && e->FIUE_refCount)
	    e->FIUE_refCount--;
	FontDrUnlockInfoBlock();
    }
    gs->GS_fontHandle = 0;
}

/* ---------------------------------------------------------------------
 * Loading
 * ------------------------------------------------------------------- */

static word Le16(const byte *p)
{
    return (word) (p[0] | (p[1] << 8));
}

static void WBF(WBFixed *w, const byte *p)
{
    w->WBF_frac = p[0];
    w->WBF_int = (sword) Le16(p + 1);
}

/* a bitmap face in the DOS16 layout (img, size bytes) into the block
 * mh as a FontBuf; FALSE if it is not one */
Boolean GrConvertBitmapFont(const byte *img, word size, MemHandle mh)
{
    word first, last, n, i, h16, hc;
    sdword delta, total;
    FontBuf *fb;
    CharTableEntry *cte;
    const byte *p;

    if (size < DOS16_FONT_BUF_SIZE)
	return FALSE;
    first = img[40];
    last = img[41];
    if (last < first)
	return FALSE;
    n = (word) (last - first + 1);
    h16 = (word) (DOS16_FONT_BUF_SIZE + n * DOS16_CHAR_TABLE_ENTRY);
    hc = (word) (sizeof(FontBuf) + n * sizeof(CharTableEntry));
    if (h16 > size)
	return FALSE;
    delta = (sdword) hc - h16;
    total = (sdword) size + delta;
    if (total > 0xffff)
	return FALSE;
    MemReAlloc(mh, (word) total, HAF_NO_ERR);
    fb = MemLock(mh);
    fb->FB_dataSize = (word) (Le16(img) + delta);
    fb->FB_maker = Le16(img + 2);
    WBF(&fb->FB_avgwidth, img + 4);
    WBF(&fb->FB_maxwidth, img + 7);
    WBF(&fb->FB_heightAdjust, img + 10);
    WBF(&fb->FB_height, img + 13);
    WBF(&fb->FB_accent, img + 16);
    WBF(&fb->FB_mean, img + 19);
    WBF(&fb->FB_baseAdjust, img + 22);
    WBF(&fb->FB_baselinePos, img + 25);
    WBF(&fb->FB_descent, img + 28);
    WBF(&fb->FB_extLeading, img + 31);
    fb->FB_kernCount = Le16(img + 34);
    fb->FB_kernPairPtr = (word) (Le16(img + 36) + delta);
    fb->FB_kernValuePtr = (word) (Le16(img + 38) + delta);
    fb->FB_firstChar = (byte) first;
    fb->FB_lastChar = (byte) last;
    fb->FB_defaultChar = img[42];
    WBF(&fb->FB_underPos, img + 43);
    WBF(&fb->FB_underThickness, img + 46);
    WBF(&fb->FB_strikePos, img + 49);
    WBF(&fb->FB_aboveBox, img + 52);
    WBF(&fb->FB_belowBox, img + 55);
    fb->FB_minLSB = (sword) Le16(img + 58);
    fb->FB_minTSB = (sword) Le16(img + 60);
    fb->FB_maxBSB = (sword) Le16(img + 62);
    fb->FB_maxRSB = (sword) Le16(img + 64);
    fb->FB_pixHeight = Le16(img + 66);
    fb->FB_flags = img[68];
    fb->FB_heapCount = 0;
    cte = FB_CHAR_TABLE(fb);
    for (i = 0, p = img + DOS16_FONT_BUF_SIZE; i < n;
	 i++, p += DOS16_CHAR_TABLE_ENTRY) {
	word off = Le16(p);
	cte[i].CTE_dataOffset = (word) (off > CHAR_MISSING ? off + delta : off);
	cte[i].CTE_width.WBF_frac = p[2];
	cte[i].CTE_width.WBF_int = (sword) Le16(p + 3);
	cte[i].CTE_flags = p[5];
	cte[i].CTE_usage = 0;
    }
    memcpy((byte *) fb + hc, img + h16, (size_t) (size - h16));
    MemUnlock(mh);
    return TRUE;
}

/* EnsureFontFileOpen: the font's file, opened; the open ones kept to
 * [system] maxfontfiles, the least recent closed */
FileHandle GrEnsureFontFileOpen(FontID id)
{
    FontsAvailEntry *fa = GrFontAvailEntry(id);
    FontInfo *fi;
    FileHandle fh;
    word size, n, i;
    FontID *cache;

    if (!fa)
	return 0;
    fi = GrFontChunk(fa->FAE_infoHandle, NULL);
    if (fi->FI_fileHandle || !fa->FAE_fileName[0])
	return fi->FI_fileHandle;
    FilePushDir();
    FileSetStandardPath(SP_FONT);
    fh = FileOpen(fa->FAE_fileName, FILE_ACCESS_R | FILE_DENY_W);
    FilePopDir();
    if (!fh)
	return 0;
    fi = FontDrFindFontInfo(NULL, id);
    fi->FI_fileHandle = fh;
    /* RecordNewFontFile */
    cache = GrFontChunk(FONT_FILE_CACHE_HANDLE, &size);
    n = (word) (size / sizeof(FontID));
    for (i = 0; i < n && cache[i]; i++)
	;
    if (n && i == n) {				/* close the oldest */
	FontInfo *old = FontDrFindFontInfo(NULL, cache[0]);
	if (old && old->FI_fileHandle) {
	    FileClose(old->FI_fileHandle, TRUE);
	    old->FI_fileHandle = 0;
	}
	memmove(cache, cache + 1, (n - 1) * sizeof(FontID));
	i = (word) (n - 1);
    }
    if (n)
	cache[i] = id;
    return fh;
}

/* ReloadFont: a bitmap face read from its file into mh */
Boolean GrLoadBitmapFont(FileHandle fh, const PointSizeEntry *pse,
			 MemHandle mh)
{
    byte *img = malloc(pse->PSE_dataSize);
    Boolean ok = FALSE;

    if (img &&
	FilePos(fh, ((dword) pse->PSE_filePosHi << 16) | pse->PSE_filePosLo,
		FILE_POS_START) ==
	(((dword) pse->PSE_filePosHi << 16) | pse->PSE_filePosLo) &&
	FileRead(fh, img, pse->PSE_dataSize, TRUE) == pse->PSE_dataSize)
	ok = GrConvertBitmapFont(img, pse->PSE_dataSize, mh);
    free(img);
    return ok;
}

/* ReallocFont: the data of a discarded font */
static Boolean ReallocFont(GState *gs, const TMatrix *m, Handle h)
{
    FontID id;
    TextStyle style;
    FontInfo *fi;
    PointSizeEntry *pse;
    Boolean ok = FALSE;

    FontDrLockInfoBlock();
    if (gs->GS_fontFlags & FBF_MAPPED_FONT)
	if (!FindBestFace(gs))
	    m = &grIdentityMatrix;
    id = gs->GS_fontAttr.FCA_fontID;
    style = (TextStyle) (gs->GS_fontAttr.FCA_textStyle & ~KERNEL_STYLES);
    fi = FontDrFindFontInfo(NULL, id);
    if (fi) {
	FileHandle fh = GrEnsureFontFileOpen(id);
	fi = FontDrFindFontInfo(NULL, id);
	pse = FaceAvail(gs, fi, style, m);
	if (pse) {
	    PointSizeEntry copy = *pse;
	    ok = (Boolean) (fh && GrLoadBitmapFont(fh, &copy, h));
	} else {			/* CheckCallDriver: the widths */
	    DriverRegs r;
	    FontMaker maker = fi->FI_maker;
	    memset(&r, 0, sizeof(r));
	    r.DR_bx = h;
	    r.DR_esDi = gs;
	    r.DR_bpPtr = (void *) m;
	    r.DR_cx = id;
	    if (gs->GS_window) {
		Window *w = MemDeref(gs->GS_window);
		r.DR_dsSi = w ? &w->W_TMatrix : NULL;
	    }
	    FontDrUnlockInfoBlock();
	    /* the driver fills h and leaves it locked (DR_bx) */
	    ok = (Boolean) !GrCallFontDriverID(maker, DR_FONT_GEN_WIDTHS, &r);
	    EC_CHECK(!ok || (Handle) r.DR_bx == h,
		     GRAPHICS_GR_INIT_FONTS_ERROR);
	    if (ok)
		MemUnlock(h);
	    return ok;
	}
    }
    FontDrUnlockInfoBlock();
    return ok;
}

/* ---------------------------------------------------------------------
 * Locking
 * ------------------------------------------------------------------- */

static void RecalcKern(GState *gs);

/* UpdateFontOpts */
static void UpdateFontOpts(GState *gs, const FontBuf *fb)
{
    gs->GS_pixelHeightM1 = fb->FB_pixHeight;
    gs->GS_fontFirstChar = fb->FB_firstChar;
    gs->GS_fontLastChar = fb->FB_lastChar;
    gs->GS_complexOpcode = (GOpt) ((fb->FB_flags & FBF_IS_COMPLEX) ?
				   GO_SPECIAL_CASE : GO_FALL_THRU);
    gs->GS_minLSB = fb->FB_minLSB;
    gs->GS_minTSB = fb->FB_minTSB;
    gs->GS_textMode &= (TextMode) ~(TM_TRACK_KERN | TM_PAIR_KERN);
    gs->GS_kernOp = GO_FALL_THRU;
    if (fb->FB_kernCount) {
	gs->GS_textMode |= TM_PAIR_KERN;
	gs->GS_kernOp = GO_SPECIAL_CASE;
    }
    if (gs->GS_trackKernDegree)
	RecalcKern(gs);
    gs->GS_fontFlags = (byte) ((gs->GS_fontFlags & FONT_GS_FLAGS) |
			       fb->FB_flags);
}

/* DoFontLock: the GState's font for the matrix, locked; NULL if there
 * are no fonts */
FontBuf *GrLockFont(GState *gs, const TMatrix *m)
{
    Handle h;
    FontBuf *fb;

    if (!grFontBlock)
	return NULL;
    if (!gs->GS_fontHandle) {
	FindFont(gs, m);
	if (!gs->GS_fontHandle)
	    return NULL;
    }
    h = gs->GS_fontHandle;
    if (!(gs->GS_fontFlags & FBF_DEFAULT_FONT))
	HandleP(h);
    fb = MemLock(h);
    if (!fb) {
	if (!ReallocFont(gs, m, h)) {
	    if (!(gs->GS_fontFlags & FBF_DEFAULT_FONT))
		HandleV(h);
	    return NULL;
	}
	fb = MemLock(h);
    }
    UpdateFontOpts(gs, fb);
    return fb;
}

/* NearUnlockFont */
void GrUnlockFont(GState *gs)
{
    Handle h = gs->GS_fontHandle;

    if (!h)
	return;
    MemUnlock(h);
    if (!(gs->GS_fontFlags & FBF_DEFAULT_FONT))
	HandleV(h);
}

/* the matrix fonts are built for: the window's, or the GState's */
FontBuf *FontDrLockFont(GStateHandle gstate)
{
    GState *gs;
    Window *w;
    TMatrix *m = GrLockTransform(gstate, &gs, &w);
    FontBuf *fb = GrLockFont(gs, m);

    GrUnlockTransform(gstate, gs, w);
    return fb;
}

void FontDrUnlockFont(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);

    GrUnlockFont(gs);
    MemUnlock(gstate);
}

/* ---------------------------------------------------------------------
 * The API
 * ------------------------------------------------------------------- */

/* LibRecalcKernValues: the track kerning for the size */
static void RecalcKern(GState *gs)
{
    sword degree = gs->GS_trackKernDegree;
    sword size = (sword) (gs->GS_fontAttr.FCA_pointsize.WBF_int +
			  (gs->GS_fontAttr.FCA_pointsize.WBF_frac >= 0x80));
    sdword v = 0;

    if (degree) {
	if (degree > MAX_TRACK_KERNING)
	    degree = MAX_TRACK_KERNING;
	if (degree < MIN_TRACK_KERNING)
	    degree = MIN_TRACK_KERNING;
	v = (sdword) degree * size;
	if (v > 32767 || v < -32768 || (v < 0 && gs->GS_trackKernDegree > 0))
	    v = gs->GS_trackKernDegree < 0 ? (sword) MIN_KERN_VALUE :
		MAX_KERN_VALUE;
    }
    gs->GS_trackKernValue.BBF_frac = (byte) v;
    gs->GS_trackKernValue.BBF_int = (sbyte) (v >> 8);
    gs->GS_textMode &= (TextMode) ~TM_TRACK_KERN;
    if (v)
	gs->GS_textMode |= TM_TRACK_KERN;
    gs->GS_kernOp = (GOpt) ((gs->GS_textMode & (TM_TRACK_KERN |
						 TM_PAIR_KERN)) ?
			    GO_SPECIAL_CASE : GO_FALL_THRU);
}

void GrRecalcKernValues(GState *gs)
{
    RecalcKern(gs);
}

void GrSetTrackKern(GStateHandle gstate, word tk)
{
    GState *gs = MemLock(gstate);

    (void) GrGSRecord(gstate, GR_SET_TRACK_KERN, "w", tk);
    if ((sword) tk != gs->GS_trackKernDegree) {
	gs->GS_trackKernDegree = (sword) tk;
	RecalcKern(gs);
    }
    MemUnlock(gstate);
}

word GrGetTrackKern(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    word tk = (word) gs->GS_trackKernDegree;

    MemUnlock(gstate);
    return tk;
}

/* GrSetFont: 0 keeps the font or the size */
void GrSetFont(GStateHandle gstate, FontID id, WWFixedAsDWord pointSize)
{
    GState *gs = MemLock(gstate);
    sword si = (sword) (pointSize >> 16);
    byte sf = (byte) (pointSize >> 8);

    (void) GrGSRecord(gstate, GR_SET_FONT, "fw", pointSize, id);
    if (gs->GS_fontAttr.FCA_fontID != id ||
	gs->GS_fontAttr.FCA_pointsize.WBF_int != si ||
	gs->GS_fontAttr.FCA_pointsize.WBF_frac != sf) {
	GrInvalidateFont(gs);
	if (id)
	    gs->GS_fontAttr.FCA_fontID = id;
	if (si) {
	    gs->GS_fontAttr.FCA_pointsize.WBF_int = si;
	    gs->GS_fontAttr.FCA_pointsize.WBF_frac = sf;
	}
	if (gs->GS_trackKernDegree)
	    RecalcKern(gs);
    }
    MemUnlock(gstate);
}

FontID GrGetDefFontID(dword *sizeSHL16)
{
    if (sizeSHL16)
	*sizeSHL16 = (dword) grDefaultFontSize << 16;
    return grDefaultFontID;
}
