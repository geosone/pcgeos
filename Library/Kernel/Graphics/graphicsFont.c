/*
 * graphicsFont.c -- what fonts there are and the font attributes of a
 * GState (after graphicsFont.asm: GrEnumFonts, CheckFontType, AddFont,
 * GrCheckFontAvail, GrFindNearestPointsize, GrGetFontName, GrCharWidth,
 * the weight, width, superscript and subscript attributes; GrGetFont of
 * graphicsGetState.asm).
 *
 * Port32: names compare as bytes, FEF_DOWNCASE and FEF_ALPHABETIZE on
 * ASCII only (the asm used LocalCmpStrings, the collation of the
 * localization); SBCS.
 */
#include "graphicsInt.h"
#include <ctype.h>
#include <stdlib.h>

/* CheckFontType */
static Boolean FontType(const FontInfo *fi, FontEnumFlags flags,
			word family)
{
    FontAttrs a = fi->FI_family;

    if (!fi->FI_faceName[0])
	return FALSE;
    if (!((flags & FEF_BITMAPS) && fi->FI_pointSizeTab) &&
	!((flags & FEF_OUTLINES) && fi->FI_outlineTab))
	return FALSE;
    if ((flags & FEF_USEFUL) && !(a & FA_USEFUL))
	return FALSE;
    if ((flags & FEF_FIXED_WIDTH) && !(a & FA_FIXED_WIDTH))
	return FALSE;
    if ((flags & FEF_FAMILY) && (a & FA_FAMILY) != (byte) family)
	return FALSE;
    return TRUE;
}

/* AddFont */
static void AddFont(FontEnumStruct *e, const FontInfo *fi,
		    FontEnumFlags flags)
{
    word i;

    e->FES_ID = fi->FI_fontID;
    memcpy(e->FES_name, fi->FI_faceName, FID_NAME_LEN);
    e->FES_name[FID_NAME_LEN - 1] = 0;
    if (flags & FEF_DOWNCASE)
	for (i = 0; e->FES_name[i]; i++)
	    e->FES_name[i] = (char) tolower((byte) e->FES_name[i]);
}

static int CompareFonts(const void *a, const void *b)
{
    return strcmp(((const FontEnumStruct *) a)->FES_name,
		  ((const FontEnumStruct *) b)->FES_name);
}

/* the fonts of the type into buffer (at most size of them; all are
 * counted without a buffer); the number found */
word GrEnumFonts(FontEnumStruct *buffer, word size, FontEnumFlags flags,
		 word family)
{
    word n, i, found = 0;
    FontsAvailEntry *fa;

    if (!grFontBlock)
	return 0;
    FontDrLockInfoBlock();
    fa = GrFontChunk(FONTS_AVAIL_HANDLE, &n);
    n = (word) (n / sizeof(FontsAvailEntry));
    for (i = 0; i < n; i++) {
	FontInfo *fi = GrFontChunk(fa[i].FAE_infoHandle, NULL);
	if (buffer && found == size)
	    break;
	if (!FontType(fi, flags, family))
	    continue;
	if (buffer)
	    AddFont(&buffer[found], fi, flags);
	found++;
    }
    FontDrUnlockInfoBlock();
    if (buffer && (flags & FEF_ALPHABETIZE) && found > 1)
	qsort(buffer, found, sizeof(FontEnumStruct), CompareFonts);
    return found;
}

/* GrCheckFontAvailReal: the font by ID or (FEF_STRING) name, of the
 * type; FID_INVALID if not there */
static FontID CheckFontAvail(FontEnumFlags flags, word family, FontID id,
			     const char *name)
{
    word n, i;
    FontsAvailEntry *fa;
    FontID r = FID_INVALID;

    if (!grFontBlock)
	return FID_INVALID;
    FontDrLockInfoBlock();
    fa = GrFontChunk(FONTS_AVAIL_HANDLE, &n);
    n = (word) (n / sizeof(FontsAvailEntry));
    for (i = 0; i < n && r == FID_INVALID; i++) {
	FontInfo *fi = GrFontChunk(fa[i].FAE_infoHandle, NULL);
	FontEnumStruct e;
	const char *a, *b;
	if (!FontType(fi, flags, family))
	    continue;
	if (!(flags & FEF_STRING)) {
	    if (fi->FI_fontID == id)
		r = id;
	    continue;
	}
	AddFont(&e, fi, 0);
	for (a = e.FES_name, b = name; *a && *b; a++, b++)
	    if ((flags & FEF_DOWNCASE) ?
		tolower((byte) *a) != tolower((byte) *b) : *a != *b)
		break;
	if (!*a && !*b)
	    r = fi->FI_fontID;
    }
    FontDrUnlockInfoBlock();
    return r;
}

FontID GrCheckFontAvailID(FontEnumFlags flags, word family, FontID id)
{
    return CheckFontAvail((FontEnumFlags) (flags & ~FEF_STRING), family, id,
			  NULL);
}

FontID GrCheckFontAvailName(FontEnumFlags flags, word family,
			    const char *name)
{
    return CheckFontAvail((FontEnumFlags) (flags | FEF_STRING), family, 0,
			  name);
}

/* the nearest bitmap size of the font (or its outlines, any size) and
 * the style; FALSE if the font is not there (the C stub of the asm's
 * FID_INVALID) */
Boolean GrFindNearestPointsize(FontID id, dword sizeSHL16, word styles,
			      word *styleFound, dword *sizeFoundSHL16)
{
    FontInfo *fi;
    sdword found = (sdword) (sizeSHL16 >> 8);
    TextStyle fs = 0;

    if (!grFontBlock)
	return FID_INVALID;
    FontDrLockInfoBlock();
    fi = GrFontAvailable(&id);
    if (!fi || !(GrNearestBitmap(fi, (sdword) (sizeSHL16 >> 8),
				 (TextStyle) styles, TRUE, &found, &fs) ||
		 GrNearestOutline(fi, (TextStyle) styles, &fs)))
	id = FID_INVALID;
    FontDrUnlockInfoBlock();
    if (id != FID_INVALID) {
	if (styleFound)
	    *styleFound = fs;
	if (sizeFoundSHL16)
	    *sizeFoundSHL16 = (dword) found << 8;
    }
    return (Boolean) (id != FID_INVALID);
}

/* the name of a font into name (FID_NAME_LEN); its length, 0 if the
 * font is not there */
word GrGetFontName(FontID id, char *name)
{
    FontInfo *fi;
    word len = 0;

    if (!grFontBlock)
	return 0;
    FontDrLockInfoBlock();
    fi = FontDrFindFontInfo(NULL, id);
    if (fi) {
	memcpy(name, fi->FI_faceName, FID_NAME_LEN);
	name[FID_NAME_LEN - 1] = 0;
	len = (word) strlen(name);
    }
    FontDrUnlockInfoBlock();
    return len;
}

FontID GrGetFont(GStateHandle gstate, WWFixedAsDWord *pointSize)
{
    GState *gs = MemLock(gstate);
    FontID id = gs->GS_fontAttr.FCA_fontID;

    if (pointSize)
	*pointSize = ((dword) (word) gs->GS_fontAttr.FCA_pointsize.WBF_int
		      << 16) |
		     ((dword) gs->GS_fontAttr.FCA_pointsize.WBF_frac << 8);
    MemUnlock(gstate);
    return id;
}

/* SetFontAttrCommonByte/Word: a change drops the font */
static void SetAttr(GStateHandle gstate, size_t offset, const byte *v,
		    word n)
{
    GState *gs = MemLock(gstate);
    byte *p = (byte *) &gs->GS_fontAttr + offset;

    if (memcmp(p, v, n)) {
	memcpy(p, v, n);
	GrInvalidateFont(gs);
    }
    MemUnlock(gstate);
}

static void GetAttr(GStateHandle gstate, size_t offset, byte *v, word n)
{
    GState *gs = MemLock(gstate);

    memcpy(v, (byte *) &gs->GS_fontAttr + offset, n);
    MemUnlock(gstate);
}

void GrSetFontWeight(GStateHandle gstate, FontWeight weight)
{
    SetAttr(gstate, offsetof(FontCommonAttrs, FCA_weight), &weight, 1);
}

void GrSetFontWidth(GStateHandle gstate, FontWidth width)
{
    SetAttr(gstate, offsetof(FontCommonAttrs, FCA_width), &width, 1);
}

void GrSetSuperscriptAttr(GStateHandle gstate, SuperscriptAttr attrs)
{
    byte v[2];

    v[0] = attrs.position;
    v[1] = attrs.size;
    SetAttr(gstate, offsetof(FontCommonAttrs, FCA_superPos), v, 2);
}

void GrSetSubscriptAttr(GStateHandle gstate, SubscriptAttr attrs)
{
    byte v[2];

    v[0] = attrs.position;
    v[1] = attrs.size;
    SetAttr(gstate, offsetof(FontCommonAttrs, FCA_subPos), v, 2);
}

FontWeight GrGetFontWeight(GStateHandle gstate)
{
    FontWeight w;

    GetAttr(gstate, offsetof(FontCommonAttrs, FCA_weight), &w, 1);
    return w;
}

FontWidth GrGetFontWidth(GStateHandle gstate)
{
    FontWidth w;

    GetAttr(gstate, offsetof(FontCommonAttrs, FCA_width), &w, 1);
    return w;
}

SuperscriptAttr GrGetSuperscriptAttr(GStateHandle gstate)
{
    byte v[2];
    SuperscriptAttr a;

    GetAttr(gstate, offsetof(FontCommonAttrs, FCA_superPos), v, 2);
    a.position = v[0];
    a.size = v[1];
    return a;
}

SubscriptAttr GrGetSubscriptAttr(GStateHandle gstate)
{
    byte v[2];
    SubscriptAttr a;

    GetAttr(gstate, offsetof(FontCommonAttrs, FCA_subPos), v, 2);
    a.position = v[0];
    a.size = v[1];
    return a;
}

/* the entry of a character in a locked font, the default character for
 * one that is not there */
CharTableEntry *GrCharEntry(const FontBuf *fb, word ch)
{
    CharTableEntry *cte = FB_CHAR_TABLE(fb);

    if (ch < fb->FB_firstChar || ch > fb->FB_lastChar ||
	cte[ch - fb->FB_firstChar].CTE_dataOffset == CHAR_NOT_EXIST)
	ch = fb->FB_defaultChar;
    return &cte[ch - fb->FB_firstChar];
}

/* the width of a character, << 16 (WBFixed in the upper 24 bits) */
dword GrCharWidth(GStateHandle gstate, word ch)
{
    FontBuf *fb = FontDrLockFont(gstate);
    CharTableEntry *cte;
    dword w;

    if (!fb)
	return 0;
    cte = GrCharEntry(fb, ch);
    w = ((dword) (word) cte->CTE_width.WBF_int << 16) |
	((dword) cte->CTE_width.WBF_frac << 8);
    FontDrUnlockFont(gstate);
    return w;
}
