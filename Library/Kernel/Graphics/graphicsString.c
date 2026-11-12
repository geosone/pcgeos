/*
 * graphicsString.c -- playing graphics strings (after graphicsString.asm:
 * GrDrawGString, GrDrawGStringAtCP, GrParseGString, GrCopyGString,
 * GrGetGStringElement, GrSetGStringPos, GrDeleteGStringElement,
 * GStringSetup, GStringCleanup, ReadElement, PlayElement,
 * CheckEndCondition, GetElementInfo, GSElemInfoTab and the PE* play
 * routines).
 *
 * Reading copies an element into GSS_elem (the C heap) and marks it
 * GSF_ELEMENT_READY; playing it moves GSS_curPos past it (bytes; VM:
 * elements) and calls the Gr* routine of its opcode with the values of
 * the DOS16 element.  The variable parts (points, text, bitmaps) are
 * copied to an aligned buffer first.  An element that stops a partial
 * play (GSControl) stays ready and is played first the next time.  The
 * end of a chunk, a huge array or a file reads as GR_END_GSTRING.
 *
 * Drawing (not GrCopyGString, GrParseGString) moves the transformation
 * to the pen and makes it the default (GStringSetup) until the string
 * is complete (GStringCleanup).  GrCopyGString to a graphics string
 * copies the elements byte for byte; to a GState it plays them.
 *
 * Not yet: GR_DRAW_TEXT_FIELD (style runs: the element is a fault),
 * the *_PTR and *_OPTR elements, bitmap slices, palettes,
 * GrGetGStringBounds and the
 * bounds of GSF_FIGURE_BOUNDS.
 */
#include "graphicsInt.h"
#include <gstring.h>
#include <file.h>
#include <hugearr.h>
#include <font.h>
#include <stdlib.h>

const GSElemInfo grGSElemInfo[GS_NUM_ELEMENTS] = {
    {  1, 0, 0},	/* 0x00 GR_END_GSTRING */
    {  3, GSV_BYTES, 1},	/* 0x01 GR_COMMENT */
    {  1, 0, 0},	/* 0x02 GR_NULL_OP */
    {  9, 0, 0},	/* 0x03 GR_SET_GSTRING_BOUNDS */
    {  1, 0, 0},	/* 0x04 GR_MISC_4 */
    {  1, 0, 0},	/* 0x05 GR_MISC_5 */
    {  1, 0, 0},	/* 0x06 GR_MISC_6 */
    {  1, 0, 0},	/* 0x07 GR_MISC_7 */
    {  1, 0, 0},	/* 0x08 GR_MISC_8 */
    {  1, 0, 0},	/* 0x09 GR_MISC_9 */
    {  1, 0, 0},	/* 0x0a GR_MISC_A */
    {  1, 0, 0},	/* 0x0b GR_MISC_B */
    {  1, 0, 0},	/* 0x0c GR_MISC_C */
    {  3, 0, 0},	/* 0x0d GR_LABEL */
    {  5, GSV_BYTES, 3},	/* 0x0e GR_ESCAPE */
    {  2, 0, 0},	/* 0x0f GR_NEW_PAGE */
    {  5, 0, 0},	/* 0x10 GR_APPLY_ROTATION */
    {  9, 0, 0},	/* 0x11 GR_APPLY_SCALE */
    {  9, 0, 0},	/* 0x12 GR_APPLY_TRANSLATION */
    { 29, 0, 0},	/* 0x13 GR_APPLY_TRANSFORM */
    {  9, 0, 0},	/* 0x14 GR_APPLY_TRANSLATION_DWORD */
    { 29, 0, 0},	/* 0x15 GR_SET_TRANSFORM */
    {  1, 0, 0},	/* 0x16 GR_SET_NULL_TRANSFORM */
    {  1, 0, 0},	/* 0x17 GR_SET_DEFAULT_TRANSFORM */
    {  1, 0, 0},	/* 0x18 GR_INIT_DEFAULT_TRANSFORM */
    {  1, 0, 0},	/* 0x19 GR_SAVE_TRANSFORM */
    {  1, 0, 0},	/* 0x1a GR_RESTORE_TRANSFORM */
    {  1, 0, 0},	/* 0x1b GR_XFORM_1B */
    {  1, 0, 0},	/* 0x1c GR_XFORM_1C */
    {  1, 0, 0},	/* 0x1d GR_XFORM_1D */
    {  1, 0, 0},	/* 0x1e GR_XFORM_1E */
    {  1, 0, 0},	/* 0x1f GR_XFORM_1F */
    {  9, 0, 0},	/* 0x20 GR_DRAW_LINE */
    {  5, 0, 0},	/* 0x21 GR_DRAW_LINE_TO */
    {  9, 0, 0},	/* 0x22 GR_DRAW_REL_LINE_TO */
    {  7, 0, 0},	/* 0x23 GR_DRAW_HLINE */
    {  3, 0, 0},	/* 0x24 GR_DRAW_HLINE_TO */
    {  7, 0, 0},	/* 0x25 GR_DRAW_VLINE */
    {  3, 0, 0},	/* 0x26 GR_DRAW_VLINE_TO */
    {  3, GSV_COORDS, 1},	/* 0x27 GR_DRAW_POLYLINE */
    { 15, 0, 0},	/* 0x28 GR_DRAW_ARC */
    { 27, 0, 0},	/* 0x29 GR_DRAW_ARC_3POINT */
    { 19, 0, 0},	/* 0x2a GR_DRAW_ARC_3POINT_TO */
    { 19, 0, 0},	/* 0x2b GR_DRAW_REL_ARC_3POINT_TO */
    {  9, 0, 0},	/* 0x2c GR_DRAW_RECT */
    {  5, 0, 0},	/* 0x2d GR_DRAW_RECT_TO */
    { 11, 0, 0},	/* 0x2e GR_DRAW_ROUND_RECT */
    {  7, 0, 0},	/* 0x2f GR_DRAW_ROUND_RECT_TO */
    {  3, GSV_COORDS, 1},	/* 0x30 GR_DRAW_SPLINE */
    {  3, GSV_COORDS, 1},	/* 0x31 GR_DRAW_SPLINE_TO */
    { 17, 0, 0},	/* 0x32 GR_DRAW_CURVE */
    { 13, 0, 0},	/* 0x33 GR_DRAW_CURVE_TO */
    { 13, 0, 0},	/* 0x34 GR_DRAW_REL_CURVE_TO */
    {  9, 0, 0},	/* 0x35 GR_DRAW_ELLIPSE */
    {  3, GSV_COORDS, 1},	/* 0x36 GR_DRAW_POLYGON */
    {  5, 0, 0},	/* 0x37 GR_DRAW_POINT */
    {  1, 0, 0},	/* 0x38 GR_DRAW_POINT_CP */
    {  5, GSV_COORDS, 1},	/* 0x39 GR_BRUSH_POLYLINE */
    {  6, 0, 0},	/* 0x3a GR_DRAW_CHAR */
    {  2, 0, 0},	/* 0x3b GR_DRAW_CHAR_CP */
    {  7, GSV_BYTES, 5},	/* 0x3c GR_DRAW_TEXT */
    {  3, GSV_BYTES, 1},	/* 0x3d GR_DRAW_TEXT_CP */
    { 15, 0, 0},	/* 0x3e GR_DRAW_TEXT_FIELD */
    {  7, 0, 0},	/* 0x3f GR_DRAW_TEXT_PTR */
    {  9, 0, 0},	/* 0x40 GR_DRAW_TEXT_OPTR */
    {  1, 0, 0},	/* 0x41 GR_DRAW_PATH */
    {  9, 0, 0},	/* 0x42 GR_FILL_RECT */
    {  5, 0, 0},	/* 0x43 GR_FILL_RECT_TO */
    { 11, 0, 0},	/* 0x44 GR_FILL_ROUND_RECT */
    {  7, 0, 0},	/* 0x45 GR_FILL_ROUND_RECT_TO */
    { 15, 0, 0},	/* 0x46 GR_FILL_ARC */
    {  4, GSV_COORDS, 1},	/* 0x47 GR_FILL_POLYGON */
    {  9, 0, 0},	/* 0x48 GR_FILL_ELLIPSE */
    {  2, 0, 0},	/* 0x49 GR_FILL_PATH */
    { 27, 0, 0},	/* 0x4a GR_FILL_ARC_3POINT */
    { 19, 0, 0},	/* 0x4b GR_FILL_ARC_3POINT_TO */
    {  7, GSV_BYTES, 5},	/* 0x4c GR_FILL_BITMAP */
    {  3, GSV_BYTES, 1},	/* 0x4d GR_FILL_BITMAP_CP */
    {  9, 0, 0},	/* 0x4e GR_FILL_BITMAP_OPTR */
    {  7, 0, 0},	/* 0x4f GR_FILL_BITMAP_PTR */
    {  7, GSV_BYTES, 5},	/* 0x50 GR_DRAW_BITMAP */
    {  3, GSV_BYTES, 1},	/* 0x51 GR_DRAW_BITMAP_CP */
    {  9, 0, 0},	/* 0x52 GR_DRAW_BITMAP_OPTR */
    {  7, 0, 0},	/* 0x53 GR_DRAW_BITMAP_PTR */
    {  3, GSV_BYTES, 1},	/* 0x54 GSE_BITMAP_SLICE */
    {  1, 0, 0},	/* 0x55 GR_OUTPUT_55 */
    {  1, 0, 0},	/* 0x56 GR_OUTPUT_56 */
    {  1, 0, 0},	/* 0x57 GR_OUTPUT_57 */
    {  1, 0, 0},	/* 0x58 GR_OUTPUT_58 */
    {  1, 0, 0},	/* 0x59 GR_OUTPUT_59 */
    {  1, 0, 0},	/* 0x5a GR_OUTPUT_5A */
    {  1, 0, 0},	/* 0x5b GR_OUTPUT_5B */
    {  1, 0, 0},	/* 0x5c GR_OUTPUT_5C */
    {  1, 0, 0},	/* 0x5d GR_OUTPUT_5D */
    {  1, 0, 0},	/* 0x5e GR_OUTPUT_5E */
    {  1, 0, 0},	/* 0x5f GR_OUTPUT_5F */
    {  1, 0, 0},	/* 0x60 GR_SAVE_STATE */
    {  1, 0, 0},	/* 0x61 GR_RESTORE_STATE */
    {  2, 0, 0},	/* 0x62 GR_SET_MIX_MODE */
    {  5, 0, 0},	/* 0x63 GR_MOVE_TO */
    {  9, 0, 0},	/* 0x64 GR_REL_MOVE_TO */
    {  1, 0, 0},	/* 0x65 GR_CREATE_PALETTE */
    {  1, 0, 0},	/* 0x66 GR_DESTROY_PALETTE */
    {  6, 0, 0},	/* 0x67 GR_SET_PALETTE_ENTRY */
    {  4, GSV_BYTES, 2},	/* 0x68 GR_SET_PALETTE */
    {  4, 0, 0},	/* 0x69 GR_SET_LINE_COLOR */
    {  2, 0, 0},	/* 0x6a GR_SET_LINE_MASK */
    {  2, 0, 0},	/* 0x6b GR_SET_LINE_COLOR_MAP */
    {  5, 0, 0},	/* 0x6c GR_SET_LINE_WIDTH */
    {  2, 0, 0},	/* 0x6d GR_SET_LINE_JOIN */
    {  2, 0, 0},	/* 0x6e GR_SET_LINE_END */
    { 14, 0, 0},	/* 0x6f GR_SET_LINE_ATTR */
    {  5, 0, 0},	/* 0x70 GR_SET_MITER_LIMIT */
    {  3, 0, 0},	/* 0x71 GR_SET_LINE_STYLE */
    {  2, 0, 0},	/* 0x72 GR_SET_LINE_COLOR_INDEX */
    {  9, 0, 0},	/* 0x73 GR_SET_CUSTOM_LINE_MASK */
    {  5, GSV_WORDS, 3},	/* 0x74 GR_SET_CUSTOM_LINE_STYLE */
    {  4, 0, 0},	/* 0x75 GR_SET_AREA_COLOR */
    {  2, 0, 0},	/* 0x76 GR_SET_AREA_MASK */
    {  2, 0, 0},	/* 0x77 GR_SET_AREA_COLOR_MAP */
    {  7, 0, 0},	/* 0x78 GR_SET_AREA_ATTR */
    {  2, 0, 0},	/* 0x79 GR_SET_AREA_COLOR_INDEX */
    {  9, 0, 0},	/* 0x7a GR_SET_CUSTOM_AREA_MASK */
    {  3, 0, 0},	/* 0x7b GR_SET_AREA_PATTERN */
    {  5, GSV_BYTES, 3},	/* 0x7c GR_SET_CUSTOM_AREA_PATTERN */
    {  4, 0, 0},	/* 0x7d GR_SET_TEXT_COLOR */
    {  2, 0, 0},	/* 0x7e GR_SET_TEXT_MASK */
    {  2, 0, 0},	/* 0x7f GR_SET_TEXT_COLOR_MAP */
    {  3, 0, 0},	/* 0x80 GR_SET_TEXT_STYLE */
    {  3, 0, 0},	/* 0x81 GR_SET_TEXT_MODE */
    {  4, 0, 0},	/* 0x82 GR_SET_TEXT_SPACE_PAD */
    { 25, 0, 0},	/* 0x83 GR_SET_TEXT_ATTR */
    {  6, 0, 0},	/* 0x84 GR_SET_FONT */
    {  2, 0, 0},	/* 0x85 GR_SET_TEXT_COLOR_INDEX */
    {  9, 0, 0},	/* 0x86 GR_SET_CUSTOM_TEXT_MASK */
    {  3, 0, 0},	/* 0x87 GR_SET_TRACK_KERN */
    {  2, 0, 0},	/* 0x88 GR_SET_FONT_WEIGHT */
    {  2, 0, 0},	/* 0x89 GR_SET_FONT_WIDTH */
    {  3, 0, 0},	/* 0x8a GR_SET_SUPERSCRIPT_ATTR */
    {  3, 0, 0},	/* 0x8b GR_SET_SUBSCRIPT_ATTR */
    {  3, 0, 0},	/* 0x8c GR_SET_TEXT_PATTERN */
    {  5, GSV_BYTES, 3},	/* 0x8d GR_SET_CUSTOM_TEXT_PATTERN */
    {  9, 0, 0},	/* 0x8e GR_MOVE_TO_WWFIXED */
    {  2, 0, 0},	/* 0x8f GR_SET_TEXT_DIRECTION */
    {  1, 0, 0},	/* 0x90 GR_ATTR_90 */
    {  1, 0, 0},	/* 0x91 GR_ATTR_91 */
    {  1, 0, 0},	/* 0x92 GR_ATTR_92 */
    {  1, 0, 0},	/* 0x93 GR_ATTR_93 */
    {  1, 0, 0},	/* 0x94 GR_ATTR_94 */
    {  1, 0, 0},	/* 0x95 GR_ATTR_95 */
    {  1, 0, 0},	/* 0x96 GR_ATTR_96 */
    {  1, 0, 0},	/* 0x97 GR_ATTR_97 */
    {  1, 0, 0},	/* 0x98 GR_ATTR_98 */
    {  1, 0, 0},	/* 0x99 GR_ATTR_99 */
    {  1, 0, 0},	/* 0x9a GR_ATTR_9A */
    {  1, 0, 0},	/* 0x9b GR_ATTR_9B */
    {  1, 0, 0},	/* 0x9c GR_ATTR_9C */
    {  1, 0, 0},	/* 0x9d GR_ATTR_9D */
    {  1, 0, 0},	/* 0x9e GR_ATTR_9E */
    {  1, 0, 0},	/* 0x9f GR_ATTR_9F */
    {  5, 0, 0},	/* 0xa0 GR_BEGIN_PATH */
    {  1, 0, 0},	/* 0xa1 GR_END_PATH */
    { 11, 0, 0},	/* 0xa2 GR_SET_CLIP_RECT */
    { 11, 0, 0},	/* 0xa3 GR_SET_WIN_CLIP_RECT */
    {  1, 0, 0},	/* 0xa4 GR_CLOSE_SUB_PATH */
    {  4, 0, 0},	/* 0xa5 GR_SET_CLIP_PATH */
    {  4, 0, 0},	/* 0xa6 GR_SET_WIN_CLIP_PATH */
    {  1, 0, 0},	/* 0xa7 GR_SET_STROKE_PATH */
    {  1, 0, 0},	/* 0xa8 GR_PATH_A8 */
    {  1, 0, 0},	/* 0xa9 GR_PATH_A9 */
    {  1, 0, 0},	/* 0xaa GR_PATH_AA */
    {  1, 0, 0},	/* 0xab GR_PATH_AB */
    {  1, 0, 0},	/* 0xac GR_PATH_AC */
    {  1, 0, 0},	/* 0xad GR_PATH_AD */
    {  1, 0, 0},	/* 0xae GR_PATH_AE */
    {  1, 0, 0},	/* 0xaf GR_PATH_AF */
};

static word U(const byte *p)
{
    return (word) (p[0] | (p[1] << 8));
}

static sword S(const byte *p)
{
    return (sword) U(p);
}

static dword D(const byte *p)
{
    return (dword) U(p) | ((dword) U(p + 2) << 16);
}

/* a WBFixed as a WWFixedAsDWord */
static void SetW(WWFixed *w, dword v)
{
    w->WWF_frac = (word) v;
    w->WWF_int = (sword) (v >> 16);
}

static WWFixedAsDWord WBF(const byte *p)
{
    return ((dword) U(p + 1) << 16) | ((dword) p[0] << 8);
}

static void DWF(const byte *p, DWFixed *d)
{
    d->DWF_frac = U(p);
    d->DWF_int = (sdword) D(p + 2);
}

word GrGSElemSize(const byte *e, dword avail)
{
    const GSElemInfo *i;
    dword size;

    if (!avail || *e >= GS_NUM_ELEMENTS || *e == GR_DRAW_TEXT_FIELD)
	return 0;
    i = &grGSElemInfo[*e];
    if (avail < i->GSEI_size)
	return 0;
    size = i->GSEI_size;
    switch (i->GSEI_var) {
    case GSV_BYTES: size += U(e + i->GSEI_varOff); break;
    case GSV_WORDS: size += (dword) U(e + i->GSEI_varOff) * 2; break;
    case GSV_COORDS: size += (dword) U(e + i->GSEI_varOff) * 4; break;
    }
    return (word) (size <= avail && size <= 0xffff ? size : 0);
}

/* the size of the fixed part, if the element at e begins there */
static word FixedSize(byte op)
{
    return (word) (op < GS_NUM_ELEMENTS ? grGSElemInfo[op].GSEI_size : 0);
}

static Boolean Keep(GStringBlock *g, const void *src, word size)
{
    if (size > g->GSS_elemCap) {
	byte *n = realloc(g->GSS_elem, size);
	if (!n)
	    return FALSE;
	g->GSS_elem = n;
	g->GSS_elemCap = size;
    }
    memcpy(g->GSS_elem, src, size);
    g->GSS_lastSize = size;
    return TRUE;
}

static const byte endElement = GR_END_GSTRING;

/* ReadElement: the element at GSS_curPos into GSS_elem; FALSE: fault */
static Boolean ReadElement(GStringBlock *g)
{
    Boolean ok = FALSE;

    if (g->GSS_flags & GSF_ELEMENT_READY)
	return TRUE;
    switch (g->GSS_flags & GSF_HANDLE_TYPE) {
    case GST_CHUNK: {
	MemHandle mh = (MemHandle) g->GSS_hString;
	ChunkHandle ch = (ChunkHandle) g->GSS_firstBlock;
	word size, n;
	const byte *p;

	MemLock(mh);
	size = LMemGetChunkSizeHandles(mh, ch);
	p = LMemDerefHandles(mh, ch);
	if (g->GSS_curPos >= size)
	    ok = Keep(g, &endElement, 1);
	else if ((n = GrGSElemSize(p + g->GSS_curPos,
				   size - g->GSS_curPos)) != 0)
	    ok = Keep(g, p + g->GSS_curPos, n);
	MemUnlock(mh);
	break;
    }
    case GST_PTR: {
	const byte *p = g->GSS_ptr + g->GSS_curPos;
	word n = GrGSElemSize(p, 0xffff);
	ok = (Boolean) (n && Keep(g, p, n));
	break;
    }
    case GST_VMEM: {
	VMFileHandle f = (VMFileHandle) g->GSS_hString;
	void *p;
	word size, n;

	if (g->GSS_curPos >= HugeArrayGetCount(f, g->GSS_firstBlock)) {
	    ok = Keep(g, &endElement, 1);
	    break;
	}
	HugeArrayLock(f, g->GSS_firstBlock, g->GSS_curPos, &p, &size);
	if (p) {
	    if ((n = GrGSElemSize(p, size)) != 0)
		ok = Keep(g, p, n);
	    HugeArrayUnlock(p);
	}
	break;
    }
    case GST_STREAM: {
	FileHandle fh = (FileHandle) g->GSS_hString;
	byte head[32];
	word fixed, n;

	FilePos(fh, g->GSS_curPos, FILE_POS_START);
	if (FileRead(fh, head, 1, TRUE) != 1) {
	    ok = Keep(g, &endElement, 1);
	    break;
	}
	fixed = FixedSize(head[0]);
	if (!fixed || fixed > sizeof(head) ||
	    (fixed > 1 && FileRead(fh, head + 1, (word) (fixed - 1), TRUE) !=
	     (word) (fixed - 1)))
	    break;
	if (grGSElemInfo[head[0]].GSEI_var)
	    n = GrGSElemSize(head, 0xffff);
	else
	    n = GrGSElemSize(head, fixed);
	if (!n || !Keep(g, head, fixed))
	    break;
	if (n > fixed) {
	    byte *e = realloc(g->GSS_elem, n);
	    if (!e)
		break;
	    g->GSS_elem = e;
	    g->GSS_elemCap = n;
	    if (FileRead(fh, e + fixed, (word) (n - fixed), TRUE) !=
		(word) (n - fixed))
		break;
	    g->GSS_lastSize = n;
	}
	ok = TRUE;
	break;
    }
    default:
	break;
    }
    if (ok)
	g->GSS_flags |= GSF_ELEMENT_READY;
    return ok;
}

/* the element is done: past it */
static void NextElement(GStringBlock *g)
{
    g->GSS_flags &= ~GSF_ELEMENT_READY;
    if ((g->GSS_flags & GSF_HANDLE_TYPE) == GST_VMEM)
	g->GSS_curPos++;
    else
	g->GSS_curPos += g->GSS_lastSize;
}

/* an aligned copy of n bytes at p (free it) */
static void *Copy(const byte *p, dword n)
{
    void *c = malloc(n ? n : 1);

    if (c && n)
	memcpy(c, p, n);
    return c;
}

/* PE*: play e into gstate */
void GrPlayElement(GStateHandle gstate, const byte *e)
{
    void *v = NULL;

    switch (*e) {
    /* misc: nothing to draw */
    case GR_END_GSTRING: case GR_COMMENT: case GR_NULL_OP:
    case GR_SET_GSTRING_BOUNDS: case GR_LABEL: case GR_ESCAPE:
    case GR_NEW_PAGE:
	break;
    /* transformation */
    case GR_APPLY_ROTATION:
	GrApplyRotation(gstate, D(e + 1));
	break;
    case GR_APPLY_SCALE:
	GrApplyScale(gstate, D(e + 1), D(e + 5));
	break;
    case GR_APPLY_TRANSLATION:
	GrApplyTranslation(gstate, D(e + 1), D(e + 5));
	break;
    case GR_APPLY_TRANSLATION_DWORD:
	GrApplyTranslationDWord(gstate, (sdword) D(e + 1), (sdword) D(e + 5));
	break;
    case GR_APPLY_TRANSFORM:
    case GR_SET_TRANSFORM: {
	TransMatrix tm;
	tm.TM_e11.WWF_frac = U(e + 1); tm.TM_e11.WWF_int = S(e + 3);
	tm.TM_e12.WWF_frac = U(e + 5); tm.TM_e12.WWF_int = S(e + 7);
	tm.TM_e21.WWF_frac = U(e + 9); tm.TM_e21.WWF_int = S(e + 11);
	tm.TM_e22.WWF_frac = U(e + 13); tm.TM_e22.WWF_int = S(e + 15);
	DWF(e + 17, &tm.TM_e31);
	DWF(e + 23, &tm.TM_e32);
	if (*e == GR_SET_TRANSFORM)
	    GrSetTransform(gstate, &tm);
	else
	    GrApplyTransform(gstate, &tm);
	break;
    }
    case GR_SET_NULL_TRANSFORM:
	GrSetNullTransform(gstate);
	break;
    case GR_SET_DEFAULT_TRANSFORM:
	GrSetDefaultTransform(gstate);
	break;
    case GR_INIT_DEFAULT_TRANSFORM:
	GrInitDefaultTransform(gstate);
	break;
    case GR_SAVE_TRANSFORM:
	GrSaveTransform(gstate);
	break;
    case GR_RESTORE_TRANSFORM:
	GrRestoreTransform(gstate);
	break;
    /* output */
    case GR_DRAW_LINE:
	GrDrawLine(gstate, S(e + 1), S(e + 3), S(e + 5), S(e + 7));
	break;
    case GR_DRAW_LINE_TO:
	GrDrawLineTo(gstate, S(e + 1), S(e + 3));
	break;
    case GR_DRAW_REL_LINE_TO:
	GrDrawRelLineTo(gstate, D(e + 1), D(e + 5));
	break;
    case GR_DRAW_HLINE:
	GrDrawHLine(gstate, S(e + 1), S(e + 3), S(e + 5));
	break;
    case GR_DRAW_HLINE_TO:
	GrDrawHLineTo(gstate, S(e + 1));
	break;
    case GR_DRAW_VLINE:
	GrDrawVLine(gstate, S(e + 1), S(e + 3), S(e + 5));
	break;
    case GR_DRAW_VLINE_TO:
	GrDrawVLineTo(gstate, S(e + 1));
	break;
    case GR_DRAW_POLYLINE:
    case GR_DRAW_POLYGON:
    case GR_DRAW_SPLINE:
    case GR_DRAW_SPLINE_TO:
	if ((v = Copy(e + 3, (dword) U(e + 1) * 4)) != NULL) {
	    if (*e == GR_DRAW_POLYLINE)
		GrDrawPolyline(gstate, v, U(e + 1));
	    else if (*e == GR_DRAW_POLYGON)
		GrDrawPolygon(gstate, v, U(e + 1));
	    else if (*e == GR_DRAW_SPLINE)
		GrDrawSpline(gstate, v, U(e + 1));
	    else
		GrDrawSplineTo(gstate, v, U(e + 1));
	}
	break;
    case GR_BRUSH_POLYLINE:
	if ((v = Copy(e + 5, (dword) U(e + 1) * 4)) != NULL)
	    GrBrushPolyline(gstate, v, U(e + 1), e[4], e[3]);
	break;
    case GR_FILL_POLYGON:
	if ((v = Copy(e + 4, (dword) U(e + 1) * 4)) != NULL)
	    GrFillPolygon(gstate, e[3], v, U(e + 1));
	break;
    case GR_DRAW_ARC:
	GrDrawArc(gstate, S(e + 3), S(e + 5), S(e + 7), S(e + 9), U(e + 11),
		  U(e + 13), (ArcCloseType) U(e + 1));
	break;
    case GR_FILL_ARC:
	GrFillArc(gstate, S(e + 3), S(e + 5), S(e + 7), S(e + 9), U(e + 11),
		  U(e + 13), (ArcCloseType) U(e + 1));
	break;
    case GR_DRAW_RECT:
	GrDrawRect(gstate, S(e + 1), S(e + 3), S(e + 5), S(e + 7));
	break;
    case GR_DRAW_RECT_TO:
	GrDrawRectTo(gstate, S(e + 1), S(e + 3));
	break;
    case GR_FILL_RECT:
	GrFillRect(gstate, S(e + 1), S(e + 3), S(e + 5), S(e + 7));
	break;
    case GR_FILL_RECT_TO:
	GrFillRectTo(gstate, S(e + 1), S(e + 3));
	break;
    case GR_DRAW_ARC_3POINT:
    case GR_FILL_ARC_3POINT: {
	ThreePointArcParams p;
	p.TPAP_close = (ArcCloseType) U(e + 1);
	SetW(&p.TPAP_point1.PF_x, D(e + 3));
	SetW(&p.TPAP_point1.PF_y, D(e + 7));
	SetW(&p.TPAP_point2.PF_x, D(e + 11));
	SetW(&p.TPAP_point2.PF_y, D(e + 15));
	SetW(&p.TPAP_point3.PF_x, D(e + 19));
	SetW(&p.TPAP_point3.PF_y, D(e + 23));
	(*e == GR_DRAW_ARC_3POINT ? GrDrawArc3Point : GrFillArc3Point)
	    (gstate, &p);
	break;
    }
    case GR_DRAW_ARC_3POINT_TO:
    case GR_FILL_ARC_3POINT_TO: {
	ThreePointArcToParams p;
	p.TPATP_close = (ArcCloseType) U(e + 1);
	SetW(&p.TPATP_point2.PF_x, D(e + 3));
	SetW(&p.TPATP_point2.PF_y, D(e + 7));
	SetW(&p.TPATP_point3.PF_x, D(e + 11));
	SetW(&p.TPATP_point3.PF_y, D(e + 15));
	(*e == GR_DRAW_ARC_3POINT_TO ? GrDrawArc3PointTo : GrFillArc3PointTo)
	    (gstate, &p);
	break;
    }
    case GR_DRAW_REL_ARC_3POINT_TO: {
	ThreePointRelArcToParams p;
	p.TPRATP_close = (ArcCloseType) U(e + 1);
	SetW(&p.TPRATP_delta2.PF_x, D(e + 3));
	SetW(&p.TPRATP_delta2.PF_y, D(e + 7));
	SetW(&p.TPRATP_delta3.PF_x, D(e + 11));
	SetW(&p.TPRATP_delta3.PF_y, D(e + 15));
	GrDrawRelArc3PointTo(gstate, &p);
	break;
    }
    case GR_DRAW_ROUND_RECT:
	GrDrawRoundRect(gstate, S(e + 3), S(e + 5), S(e + 7), S(e + 9),
			U(e + 1));
	break;
    case GR_DRAW_ROUND_RECT_TO:
	GrDrawRoundRectTo(gstate, S(e + 3), S(e + 5), U(e + 1));
	break;
    case GR_FILL_ROUND_RECT:
	GrFillRoundRect(gstate, S(e + 3), S(e + 5), S(e + 7), S(e + 9),
			U(e + 1));
	break;
    case GR_FILL_ROUND_RECT_TO:
	GrFillRoundRectTo(gstate, S(e + 3), S(e + 5), U(e + 1));
	break;
    case GR_DRAW_CURVE:
    case GR_DRAW_CURVE_TO: {
	Point p[4];
	int i, n = *e == GR_DRAW_CURVE ? 4 : 3;
	for (i = 0; i < n; i++) {
	    p[i].P_x = S(e + 1 + 4 * i);
	    p[i].P_y = S(e + 3 + 4 * i);
	}
	if (n == 4)
	    GrDrawCurve(gstate, p);
	else
	    GrDrawCurveTo(gstate, p);
	break;
    }
    case GR_DRAW_ELLIPSE:
	GrDrawEllipse(gstate, S(e + 1), S(e + 3), S(e + 5), S(e + 7));
	break;
    case GR_FILL_ELLIPSE:
	GrFillEllipse(gstate, S(e + 1), S(e + 3), S(e + 5), S(e + 7));
	break;
    case GR_DRAW_POINT:
	GrDrawPoint(gstate, S(e + 1), S(e + 3));
	break;
    case GR_DRAW_POINT_CP:
	GrDrawPointAtCP(gstate);
	break;
    case GR_DRAW_CHAR:
	GrDrawChar(gstate, S(e + 2), S(e + 4), e[1]);
	break;
    case GR_DRAW_CHAR_CP:
	GrDrawCharAtCP(gstate, e[1]);
	break;
    case GR_DRAW_TEXT:
	if (U(e + 5) && (v = Copy(e + 7, U(e + 5))) != NULL)
	    GrDrawText(gstate, S(e + 1), S(e + 3), v, U(e + 5));
	break;
    case GR_DRAW_TEXT_CP:
	if (U(e + 1) && (v = Copy(e + 3, U(e + 1))) != NULL)
	    GrDrawTextAtCP(gstate, v, U(e + 1));
	break;
    case GR_DRAW_BITMAP:
    case GR_FILL_BITMAP:
	if ((v = Copy(e + 7, U(e + 5))) != NULL) {
	    if (*e == GR_DRAW_BITMAP)
		GrDrawBitmap(gstate, S(e + 1), S(e + 3), v, NULL);
	    else
		GrFillBitmap(gstate, S(e + 1), S(e + 3), v, NULL);
	}
	break;
    case GR_DRAW_BITMAP_CP:
    case GR_FILL_BITMAP_CP:
	if ((v = Copy(e + 3, U(e + 1))) != NULL) {
	    if (*e == GR_DRAW_BITMAP_CP)
		GrDrawBitmapAtCP(gstate, v, NULL);
	    else
		GrFillBitmapAtCP(gstate, v, NULL);
	}
	break;
    /* attributes */
    case GR_SAVE_STATE:
	GrSaveState(gstate);
	break;
    case GR_RESTORE_STATE:
	GrRestoreState(gstate);
	break;
    case GR_SET_MIX_MODE:
	GrSetMixMode(gstate, e[1]);
	break;
    case GR_MOVE_TO:
	GrMoveTo(gstate, S(e + 1), S(e + 3));
	break;
    case GR_REL_MOVE_TO:
	GrRelMoveTo(gstate, D(e + 1), D(e + 5));
	break;
    case GR_MOVE_TO_WWFIXED:
	GrMoveToWWFixed(gstate, D(e + 1), D(e + 5));
	break;
    case GR_SET_LINE_COLOR:
	GrSetLineColor(gstate, CF_RGB, e[1], e[2], e[3]);
	break;
    case GR_SET_LINE_COLOR_INDEX:
	GrSetLineColor(gstate, CF_INDEX, e[1], 0, 0);
	break;
    case GR_SET_AREA_COLOR:
	GrSetAreaColor(gstate, CF_RGB, e[1], e[2], e[3]);
	break;
    case GR_SET_AREA_COLOR_INDEX:
	GrSetAreaColor(gstate, CF_INDEX, e[1], 0, 0);
	break;
    case GR_SET_TEXT_COLOR:
	GrSetTextColor(gstate, CF_RGB, e[1], e[2], e[3]);
	break;
    case GR_SET_TEXT_COLOR_INDEX:
	GrSetTextColor(gstate, CF_INDEX, e[1], 0, 0);
	break;
    case GR_SET_LINE_MASK:
	GrSetLineMaskSys(gstate, e[1]);
	break;
    case GR_SET_AREA_MASK:
	GrSetAreaMaskSys(gstate, e[1]);
	break;
    case GR_SET_TEXT_MASK:
	GrSetTextMaskSys(gstate, e[1]);
	break;
    case GR_SET_CUSTOM_LINE_MASK:
	GrSetLineMaskCustom(gstate, (const DrawMask *) (e + 1));
	break;
    case GR_SET_CUSTOM_AREA_MASK:
	GrSetAreaMaskCustom(gstate, (const DrawMask *) (e + 1));
	break;
    case GR_SET_AREA_PATTERN:
    case GR_SET_TEXT_PATTERN: {
	GraphicPattern gp;
	gp.HP_type = e[1];
	gp.HP_data = e[2];
	(*e == GR_SET_AREA_PATTERN ? GrSetAreaPattern : GrSetTextPattern)
	    (gstate, gp);
	break;
    }
    case GR_SET_CUSTOM_AREA_PATTERN:
    case GR_SET_CUSTOM_TEXT_PATTERN: {
	GraphicPattern gp;
	gp.HP_type = e[1];
	gp.HP_data = e[2];
	if ((v = malloc(U(e + 3) ? U(e + 3) : 1)) == NULL)	/* aligned */
	    break;
	memcpy(v, e + 5, U(e + 3));
	if (*e == GR_SET_CUSTOM_AREA_PATTERN)
	    GrSetCustomAreaPattern(gstate, gp, v, U(e + 3));
	else
	    GrSetCustomTextPattern(gstate, gp, v);
	break;
    }
    case GR_SET_CUSTOM_TEXT_MASK:
	GrSetTextMaskCustom(gstate, (const DrawMask *) (e + 1));
	break;
    case GR_SET_LINE_COLOR_MAP:
	GrSetLineColorMap(gstate, e[1]);
	break;
    case GR_SET_AREA_COLOR_MAP:
	GrSetAreaColorMap(gstate, e[1]);
	break;
    case GR_SET_TEXT_COLOR_MAP:
	GrSetTextColorMap(gstate, e[1]);
	break;
    case GR_SET_LINE_WIDTH:
	GrSetLineWidth(gstate, D(e + 1));
	break;
    case GR_SET_LINE_JOIN:
	GrSetLineJoin(gstate, e[1]);
	break;
    case GR_SET_LINE_END:
	GrSetLineEnd(gstate, e[1]);
	break;
    case GR_SET_MITER_LIMIT:
	GrSetMiterLimit(gstate, D(e + 1));
	break;
    case GR_SET_LINE_STYLE:
	GrSetLineStyle(gstate, e[1], e[2], NULL, 0);
	break;
    case GR_SET_CUSTOM_LINE_STYLE:
	GrSetLineStyle(gstate, LS_CUSTOM, U(e + 1),
		       (const DashPairArray *) (e + 5), U(e + 3));
	break;
    case GR_SET_LINE_ATTR: {
	LineAttr la;
	la.LA_colorFlag = e[1];
	la.LA_color.RGB_red = e[2];
	la.LA_color.RGB_green = e[3];
	la.LA_color.RGB_blue = e[4];
	la.LA_mask = e[5];
	la.LA_mapMode = e[6];
	la.LA_end = e[7];
	la.LA_join = e[8];
	la.LA_style = e[9];
	la.LA_width.WWF_frac = U(e + 10);
	la.LA_width.WWF_int = S(e + 12);
	GrSetLineAttr(gstate, &la);
	break;
    }
    case GR_SET_AREA_ATTR: {
	AreaAttr aa;
	aa.AA_colorFlag = e[1];
	aa.AA_color.RGB_red = e[2];
	aa.AA_color.RGB_green = e[3];
	aa.AA_color.RGB_blue = e[4];
	aa.AA_mask = e[5];
	aa.AA_mapMode = e[6];
	GrSetAreaAttr(gstate, &aa);
	break;
    }
    case GR_SET_TEXT_ATTR: {
	TextAttr ta;
	const byte *a = e + 1;
	ta.TA_color.CQ_redOrIndex = a[0];
	ta.TA_color.CQ_info = a[1];
	ta.TA_color.CQ_green = a[2];
	ta.TA_color.CQ_blue = a[3];
	ta.TA_mask = a[4];
	ta.TA_pattern.HP_type = a[5];
	ta.TA_pattern.HP_data = a[6];
	ta.TA_styleSet = a[7];
	ta.TA_styleClear = a[8];
	ta.TA_modeSet = a[9];
	ta.TA_modeClear = a[10];
	ta.TA_spacePad.WBF_frac = a[11];
	ta.TA_spacePad.WBF_int = S(a + 12);
	ta.TA_font = U(a + 14);
	ta.TA_size.WBF_frac = a[16];
	ta.TA_size.WBF_int = S(a + 17);
	ta.TA_trackKern = S(a + 19);
	ta.TA_fontWeight = a[21];
	ta.TA_fontWidth = a[22];
	GrSetTextAttr(gstate, &ta);
	break;
    }
    case GR_SET_TEXT_STYLE:
	GrSetTextStyle(gstate, e[1], e[2]);
	break;
    case GR_SET_TEXT_MODE:
	GrSetTextMode(gstate, e[1], e[2]);
	break;
    case GR_SET_TEXT_SPACE_PAD:
	GrSetTextSpacePad(gstate, WBF(e + 1));
	break;
    case GR_SET_FONT:
	GrSetFont(gstate, U(e + 4), WBF(e + 1));
	break;
    case GR_SET_TRACK_KERN:
	GrSetTrackKern(gstate, U(e + 1));
	break;
    case GR_SET_FONT_WEIGHT:
	GrSetFontWeight(gstate, e[1]);
	break;
    case GR_SET_FONT_WIDTH:
	GrSetFontWidth(gstate, e[1]);
	break;
    case GR_SET_SUPERSCRIPT_ATTR:
    case GR_SET_SUBSCRIPT_ATTR: {
	SuperscriptAttr sa;
	sa.position = e[1];
	sa.size = e[2];
	if (*e == GR_SET_SUPERSCRIPT_ATTR)
	    GrSetSuperscriptAttr(gstate, sa);
	else
	    GrSetSubscriptAttr(gstate, *(SubscriptAttr *) &sa);
	break;
    }
    /* paths */
    case GR_BEGIN_PATH:
	GrBeginPath(gstate, (PathCombineType) U(e + 1));
	break;
    case GR_END_PATH:
	GrEndPath(gstate);
	break;
    case GR_CLOSE_SUB_PATH:
	GrCloseSubPath(gstate);
	break;
    case GR_FILL_PATH:
	GrFillPath(gstate, e[1]);
	break;
    case GR_DRAW_PATH:
	GrDrawPath(gstate);
	break;
    case GR_SET_CLIP_RECT:
    case GR_SET_WIN_CLIP_RECT:
	(*e == GR_SET_CLIP_RECT ? GrSetClipRect : GrSetWinClipRect)
	    (gstate, (PathCombineType) U(e + 1), S(e + 3), S(e + 5), S(e + 7),
	     S(e + 9));
	break;
    case GR_SET_CLIP_PATH:
	GrSetClipPath(gstate, (PathCombineType) U(e + 2), e[1]);
	break;
    case GR_SET_WIN_CLIP_PATH:
	GrSetWinClipPath(gstate, (PathCombineType) U(e + 2), e[1]);
	break;
    default:			/* not yet: see the top */
	break;
    }
    free(v);
}

/* CheckEndCondition: TRUE to stop before the element */
static Boolean EndCondition(const byte *e, GSControl flags, GSRetType *rt,
			    word *val)
{
    byte op = *e;

    *val = op;
    if (op == GR_END_GSTRING) {
	*rt = GSRT_COMPLETE;
	return TRUE;
    }
    if (!flags)
	return FALSE;
    if (flags & GSC_ONE) {
	*rt = GSRT_ONE;
	return TRUE;
    }
    if ((flags & GSC_OUTPUT) && op >= GR_DRAW_LINE && op <= 0x5f) {
	*rt = GSRT_OUTPUT;
	return TRUE;
    }
    if ((flags & GSC_NEW_PAGE) && op == GR_NEW_PAGE) {
	*rt = GSRT_NEW_PAGE;
	*val = e[1];
	return TRUE;
    }
    if ((flags & GSC_ESCAPE) && op == GR_ESCAPE) {
	*rt = GSRT_ESCAPE;
	*val = U(e + 1);
	return TRUE;
    }
    if ((flags & GSC_LABEL) && op == GR_LABEL) {
	*rt = GSRT_LABEL;
	*val = U(e + 1);
	return TRUE;
    }
    if ((flags & GSC_MISC) && op <= 0x0c) {
	*rt = GSRT_MISC;
	return TRUE;
    }
    if ((flags & GSC_XFORM) && op >= GR_APPLY_ROTATION && op <= 0x1f) {
	*rt = GSRT_XFORM;
	return TRUE;
    }
    if ((flags & GSC_ATTR) && op >= GR_SAVE_STATE && op <= 0x9f) {
	*rt = GSRT_ATTR;
	return TRUE;
    }
    if ((flags & GSC_PATH) && op >= GR_BEGIN_PATH && op <= 0xaf) {
	*rt = GSRT_PATH;
	return TRUE;
    }
    if (op >= GS_NUM_ELEMENTS) {
	*rt = GSRT_FAULT;
	return TRUE;
    }
    return FALSE;
}

typedef struct {
    GStateHandle	PS_gstring;	/* the GState of the string */
    MemHandle		PS_mh;		/* its GStringBlock */
    GStringBlock		*PS_g;
    GStateHandle	PS_dest;
} PlayState;

/* GStringSetup: draw moves the transformation to the pen */
static Boolean Setup(PlayState *s, GStateHandle gstring, GStateHandle dest,
		     Boolean draw)
{
    s->PS_gstring = gstring;
    s->PS_dest = dest;
    s->PS_mh = GrGStringOf(gstring);
    if (!s->PS_mh)
	return FALSE;
    s->PS_g = MemLock(s->PS_mh);
    if (draw && dest && !(s->PS_g->GSS_flags & GSF_CONTINUING)) {
	WWFixedAsDWord x, y;
	PointWWFixed pen;
	GrSaveTransform(dest);
	GrGetCurPosWWFixed(dest, &pen);
	x = ((dword) (word) pen.PF_x.WWF_int << 16) | pen.PF_x.WWF_frac;
	y = ((dword) (word) pen.PF_y.WWF_int << 16) | pen.PF_y.WWF_frac;
	GrApplyTranslation(dest, x, y);
	GrInitDefaultTransform(dest);
	s->PS_g->GSS_flags |= GSF_CONTINUING;
    }
    return TRUE;
}

/* GStringCleanup */
static void Cleanup(PlayState *s, GSRetType rt, Boolean draw)
{
    GStringBlock *g = s->PS_g;

    if (draw && (rt == GSRT_COMPLETE || rt == GSRT_FAULT ||
		 rt == GSRT_NEW_PAGE) && (g->GSS_flags & GSF_CONTINUING)) {
	g->GSS_flags &= ~GSF_CONTINUING;
	if (s->PS_dest)
	    GrRestoreTransform(s->PS_dest);
    }
    if (rt == GSRT_COMPLETE) {
	g->GSS_flags &= ~GSF_ELEMENT_READY;
	g->GSS_curPos = (g->GSS_flags & GSF_HANDLE_TYPE) == GST_STREAM ?
			g->GSS_filePos : 0;
    }
    MemUnlock(s->PS_mh);
}

typedef enum { PM_DRAW, PM_COPY, PM_PARSE } PlayMode;

static GSRetType Play(GStateHandle gstate, GStateHandle gstring,
		      GSControl flags, word *lastElement, PlayMode mode,
		      Boolean (*callback)(void *element))
{
    PlayState s;
    GSRetType rt = GSRT_COMPLETE;
    Handle destString = mode == PM_COPY ? GrGStringOf(gstate) : 0;
    word val = 0;
    Boolean draw = (Boolean) (mode == PM_DRAW);

    flags &= ~GSC_PARTIAL;
    if (!Setup(&s, gstring, gstate, draw))
	rt = GSRT_FAULT;
    else {
	GStringBlock *g = s.PS_g;

	if (g->GSS_flags & GSF_ELEMENT_READY) {	/* stopped on it */
	    if (!ReadElement(g) || *g->GSS_elem == GR_END_GSTRING)
		goto done;
	    goto play;
	}
	for (;;) {
	    if (!ReadElement(g)) {
		rt = GSRT_FAULT;
		val = 0;
		break;
	    }
	    if (EndCondition(g->GSS_elem, flags, &rt, &val)) {
		if (mode != PM_PARSE || rt == GSRT_COMPLETE ||
		    !callback || callback(g->GSS_elem))
		    break;
	    }
	play:
	    NextElement(g);
	    if (destString)
		GrGSStore(destString, g->GSS_elem, g->GSS_lastSize);
	    else if (gstate)
		GrPlayElement(gstate, g->GSS_elem);
	    /* an element may have used the GString block: g stays */
	}
    done:
	Cleanup(&s, rt, draw);
    }
    if (lastElement)
	*lastElement = val;
    return rt;
}

GSRetType GrDrawGStringAtCP(GStateHandle gstate, GStateHandle gstringToDraw,
			    GSControl flags, word *lastElement)
{
    return Play(gstate, gstringToDraw, flags, lastElement, PM_DRAW, NULL);
}

GSRetType GrDrawGString(GStateHandle gstate, GStateHandle gstringToDraw,
			sword x, sword y, GSControl flags, word *lastElement)
{
    Handle mh = GrGStringOf(gstringToDraw);
    Boolean cont = FALSE;

    if (mh) {
	GStringBlock *g = MemLock(mh);
	cont = (Boolean) ((g->GSS_flags & GSF_CONTINUING) != 0);
	MemUnlock(mh);
    }
    if (!cont)
	GrMoveTo(gstate, x, y);
    return GrDrawGStringAtCP(gstate, gstringToDraw, flags, lastElement);
}

GSRetType GrCopyGString(GStateHandle source, GStateHandle dest,
			GSControl flags)
{
    return Play(dest, source, flags, NULL, PM_COPY, NULL);
}

void GrParseGString(GStateHandle gstate, GStateHandle gstringToDraw,
		    GSControl flags, Boolean (*callback)(void *element))
{
    (void) Play(gstate, gstringToDraw, flags, NULL, PM_PARSE, callback);
}

GStringElement GrGetGStringElement(GStateHandle gstate, GStateHandle gstring,
				   word bufSize, void *buffer, word *elSize)
{
    PlayState s;
    GStringElement op = GSE_INVALID;
    word size = 0;

    (void) gstate;
    if (Setup(&s, gstring, 0, FALSE)) {
	if (ReadElement(s.PS_g)) {
	    op = *s.PS_g->GSS_elem;
	    size = s.PS_g->GSS_lastSize;
	    if (buffer)
		memcpy(buffer, s.PS_g->GSS_elem, bufSize < size ? bufSize
								: size);
	}
	Cleanup(&s, GSRT_ONE, FALSE);
    }
    if (elSize)
	*elSize = size;
    return op;
}

void GrSetGStringPos(GStateHandle gstate, GStringSetPosType type, word skip)
{
    PlayState s;
    GStringBlock *g;

    if (!Setup(&s, gstate, 0, FALSE))
	return;
    g = s.PS_g;
    switch (type) {
    case GSSPT_SKIP_1:
	skip = 1;
	/* fall through */
    case GSSPT_RELATIVE:
	if ((g->GSS_flags & GSF_HANDLE_TYPE) == GST_VMEM) {
	    g->GSS_flags &= ~GSF_ELEMENT_READY;
	    g->GSS_curPos += (sdword) (sword) skip;
	    break;
	}
	if ((sword) skip < 0)
	    break;
	while (skip-- && ReadElement(g) && *g->GSS_elem != GR_END_GSTRING)
	    NextElement(g);
	break;
    case GSSPT_BEGINNING:
    case GSSPT_END:
	g->GSS_flags &= ~GSF_ELEMENT_READY;
	switch (g->GSS_flags & GSF_HANDLE_TYPE) {
	case GST_STREAM:
	    g->GSS_curPos = type == GSSPT_BEGINNING ? g->GSS_filePos :
			    FilePos((FileHandle) g->GSS_hString, 0,
				    FILE_POS_END);
	    break;
	case GST_CHUNK:
	    if (type == GSSPT_END) {
		MemLock((MemHandle) g->GSS_hString);
		g->GSS_curPos = LMemGetChunkSizeHandles(
		    (MemHandle) g->GSS_hString, g->GSS_firstBlock);
		MemUnlock((MemHandle) g->GSS_hString);
	    } else
		g->GSS_curPos = 0;
	    break;
	case GST_VMEM:
	    g->GSS_curPos = type == GSSPT_BEGINNING ? 0 :
			    HugeArrayGetCount((VMFileHandle) g->GSS_hString,
					      g->GSS_firstBlock);
	    break;
	default:			/* GST_PTR: no end */
	    if (type == GSSPT_BEGINNING)
		g->GSS_curPos = 0;
	    break;
	}
	break;
    }
    Cleanup(&s, GSRT_MISC, FALSE);
}

void GrDeleteGStringElement(GStateHandle gstate, word count)
{
    Handle mh = GrGStringOf(gstate);
    GStringBlock *g;

    if (!mh)
	return;
    g = MemLock(mh);
    if ((g->GSS_flags & GSF_HANDLE_TYPE) == GST_VMEM)
	HugeArrayDelete((VMFileHandle) g->GSS_hString, g->GSS_firstBlock,
			count, g->GSS_curPos);
    MemUnlock(mh);
}
