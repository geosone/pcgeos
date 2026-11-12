/*
 * graphicsPattern.c -- area and text patterns (after graphicsPattern.asm:
 * GrSetAreaPattern, GrSetTextPattern, SetPatternCommon,
 * SetPatternComplex, SetPatternGString, PatternLock, GrGetAreaPattern,
 * GrGetTextPattern, GetPatternCommon, PatternFill, PatternDrawHatch,
 * HatchDrawLine, HatchDoLine, HatchDrawLineSegment, HatchCheckX,
 * HatchCheckY, PatternDrawBitmap, BitmapCalcStartPos, BitmapCalcLow,
 * PatternCalcFillBounds; graphicsPatternHatch.asm: the system hatches;
 * EnterGraphicsFill, EnterGraphicsText and ExitGraphicsFill of
 * graphicsUtils.asm; PathGetPatternInfo of graphicsPathLow.asm).
 *
 * A GState's area and text patterns (GS_areaPattern, GS_textPattern)
 * are a PatternType with its data, and for a custom one a copy of the
 * HatchPattern or Bitmap in a chunk of the GState (GCP_custom; the
 * system hatches are copied there too, as the asm did).
 *
 * Filling with a pattern (GrPatternStart, GrPatternFinish): the filled
 * figure is made a path instead (the GState saved, GrBeginPath), the
 * path is set as the clip path (intersected with the one there), the
 * pattern is drawn over what is visible of it, and the GState is
 * restored with the pen where the figure left it.
 *   - A hatch is lines: each HatchLine in page coordinates (the GState's
 *     transformation replaced by its origin and angle), repeated every
 *     HL_deltaX, HL_deltaY, solid or as dashes, one pixel wide in the
 *     line's color (CF_SAME: the area color), over the window's mask
 *     taken back to that space.
 *   - A bitmap is tiled from the document origin over the mask taken
 *     back to document coordinates (GrDrawBitmap).
 * GrFillPath fills its path the same way.
 *
 * Differences from the asm: text patterns are drawn in the text
 * attributes (the asm used the area ones); the system bitmaps and the
 * user patterns, which the asm did not have, fill solid.
 */
#include "graphicsInt.h"
#include <gstring.h>
#include <stdlib.h>

#define AREA	((word) offsetof(GState, GS_areaAttr))
#define TEXT	((word) offsetof(GState, GS_textAttr))


/* graphicsPatternHatch.asm: a HatchPattern of one or two lines */
typedef struct {
    word	SH_numLines;
    HatchLine	SH_line1;
    HatchLine	SH_line2;
    HatchDash	SH_dash;		/* of the second line */
} SysHatch;

#define WWF_I(i)	{ 0, (i) }
#define HLINE(dx, dy, a, dashes) \
    { { WWF_I(0), WWF_I(0) }, WWF_I(dx), WWF_I(dy), WWF_I(a), \
      { 0, CF_SAME, 0, 0 }, (dashes) }

static const SysHatch sysHatches[] = {
    { 1, HLINE(0, 4, 90, 0), HLINE(0, 0, 0, 0), { WWF_I(0), WWF_I(0) } },
    { 1, HLINE(0, 4, 0, 0), HLINE(0, 0, 0, 0), { WWF_I(0), WWF_I(0) } },
    { 1, HLINE(0, 4, 45, 0), HLINE(0, 0, 0, 0), { WWF_I(0), WWF_I(0) } },
    { 1, HLINE(0, 4, 135, 0), HLINE(0, 0, 0, 0), { WWF_I(0), WWF_I(0) } },
    { 2, HLINE(0, 4, 0, 0), HLINE(4, 4, 90, 1), { WWF_I(4), WWF_I(4) } },
    { 2, HLINE(0, 4, 45, 0), HLINE(4, 4, 135, 1), { WWF_I(4), WWF_I(4) } },
};

#define NUM_SYS_HATCHES	(sizeof(sysHatches) / sizeof(sysHatches[0]))

static sdword WWF(const WWFixed *w)
{
    return (sdword) (((dword) (word) w->WWF_int << 16) | w->WWF_frac);
}

/* the size of a HatchPattern */
static word HatchSize(const HatchPattern *hp)
{
    const byte *p = (const byte *) hp + sizeof(HatchPattern);
    word i;

    for (i = 0; i < hp->HP_numLines; i++) {
	const HatchLine *hl = (const HatchLine *) p;
	p += sizeof(HatchLine) + hl->HL_numDashes * sizeof(HatchDash);
    }
    return (word) (p - (const byte *) hp);
}

static Boolean Drawable(byte type)
{
    return (Boolean) (type == PT_SYSTEM_HATCH || type == PT_CUSTOM_HATCH ||
		      type == PT_CUSTOM_BITMAP);
}

/* ---------------------------------------------------------------------
 * Setting and getting
 * ------------------------------------------------------------------- */

/* SetPatternCommon, SetPatternComplex */
static void SetPattern(GStateHandle gstate, Boolean text, GraphicPattern pat,
		       const void *data, word size)
{
    GState *gs;
    GrCommonPattern *cp;
    ChunkHandle old, ch = 0;
    Boolean custom = (Boolean) (pat.HP_type >= PT_CUSTOM_HATCH);

    if (custom ? GrGSRecord(gstate, (byte) (text ?
					     GR_SET_CUSTOM_TEXT_PATTERN :
					     GR_SET_CUSTOM_AREA_PATTERN),
			     "bbwr", pat.HP_type, pat.HP_data, size, data,
			     size) :
	GrGSRecord(gstate, (byte) (text ? GR_SET_TEXT_PATTERN :
				   GR_SET_AREA_PATTERN), "bb",
		   pat.HP_type, pat.HP_data))
	return;
    gs = MemLock(gstate);
    if (gs->GS_pathFlags & PF_DEFINING_PATH) {	/* a path: nothing */
	MemUnlock(gstate);
	return;
    }
    cp = text ? &gs->GS_textPattern : &gs->GS_areaPattern;
    if (!custom && cp->GCP_pattern == pat.HP_type &&
	cp->GCP_system == pat.HP_data) {
	MemUnlock(gstate);
	return;
    }
    cp->GCP_pattern = pat.HP_type;
    cp->GCP_system = pat.HP_data;
    old = cp->GCP_custom;
    cp->GCP_custom = 0;
    if (old)
	LMemFree(ConstructOptr(gstate, old));
    if (pat.HP_type == PT_SYSTEM_HATCH && pat.HP_data < NUM_SYS_HATCHES) {
	data = &sysHatches[pat.HP_data];
	size = HatchSize(data);
    } else if (!custom)
	size = 0;
    if (size && data && size <= MAX_CUSTOM_PATTERN_SIZE) {
	ch = LMemAlloc(gstate, size);
	if (ch)
	    memcpy(LMemDeref(ConstructOptr(gstate, ch)), data, size);
	gs = MemDeref(gstate);
	cp = text ? &gs->GS_textPattern : &gs->GS_areaPattern;
	cp->GCP_custom = ch;
    }
    MemUnlock(gstate);
}

void GrSetAreaPattern(GStateHandle gstate, GraphicPattern pattern)
{
    SetPattern(gstate, FALSE, pattern, NULL, 0);
}

void GrSetCustomAreaPattern(GStateHandle gstate, GraphicPattern pattern,
			    const void *patternData, word patternSize)
{
    SetPattern(gstate, FALSE, pattern, patternData, patternSize);
}

void GrSetTextPattern(GStateHandle gstate, GraphicPattern pattern)
{
    SetPattern(gstate, TRUE, pattern, NULL, 0);
}

/* the C binding has no size: that of the HatchPattern or Bitmap */
void GrSetCustomTextPattern(GStateHandle gstate, GraphicPattern pattern,
			    const void *patternData)
{
    word size = 0;

    if (patternData)
	size = pattern.HP_type == PT_CUSTOM_HATCH ?
	    HatchSize(patternData) : GrBitmapStringSize(patternData);
    SetPattern(gstate, TRUE, pattern, patternData, size);
}

/* GetPatternCommon: a custom pattern copied into a new block */
static GraphicPattern GetPattern(GStateHandle gstate, Boolean text,
				 const MemHandle *customPattern,
				 word *customSize)
{
    GState *gs = MemLock(gstate);
    GrCommonPattern *cp = text ? &gs->GS_textPattern : &gs->GS_areaPattern;
    GraphicPattern pat;

    pat.HP_type = cp->GCP_pattern;
    pat.HP_data = cp->GCP_system;
    if (pat.HP_type > PT_USER_BITMAP && cp->GCP_custom) {
	optr o = ConstructOptr(gstate, cp->GCP_custom);
	word size = LMemGetChunkSize(o);
	MemHandle mh = MemAlloc(size, HF_SWAPABLE, 0);
	if (mh) {
	    memcpy(MemLock(mh), LMemDeref(o), size);
	    MemUnlock(mh);
	}
	if (customPattern)
	    *(MemHandle *) customPattern = mh;
	if (customSize)
	    *customSize = size;
    }
    MemUnlock(gstate);
    return pat;
}

GraphicPattern GrGetAreaPattern(GStateHandle gstate,
				const MemHandle *customPattern,
				word *customSize)
{
    return GetPattern(gstate, FALSE, customPattern, customSize);
}

GraphicPattern GrGetTextPattern(GStateHandle gstate,
				const MemHandle *customPattern,
				word *customSize)
{
    return GetPattern(gstate, TRUE, customPattern, customSize);
}

/* ---------------------------------------------------------------------
 * Filling
 * ------------------------------------------------------------------- */

/* PatternCalcFillBounds: the mask's bounds through the inverse of the
 * window's matrix; FALSE if nothing is visible */
static Boolean FillBounds(const GrDraw *d, sdword *l, sdword *t, sdword *r,
			  sdword *b)
{
    TMatrix m = d->GD_w->W_curTMatrix;
    const Rectangle *mr = &d->GD_w->W_maskRect;
    int i;

    if ((word) mr->R_left == EOREGREC || (d->GD_w->W_grFlags & WGF_MASK_NULL))
	return FALSE;
    *l = *t = MAX_COORD;
    *r = *b = -MAX_COORD;
    for (i = 0; i < 4; i++) {
	int64_t x = (int64_t) (i == 0 || i == 3 ? mr->R_left : mr->R_right + 1)
	    * 65536;
	int64_t y = (int64_t) (i < 2 ? mr->R_top : mr->R_bottom + 1) * 65536;
	sdword xi, yi;
	GrMatUntransDWF(&m, &x, &y);
	xi = (sdword) (x >> 16);
	yi = (sdword) (y >> 16);
	if (xi < *l) *l = xi;
	if (xi + 1 > *r) *r = xi + 1;
	if (yi < *t) *t = yi;
	if (yi + 1 > *b) *b = yi + 1;
    }
    return TRUE;
}

/* HatchDrawLineSegment: x1 to x2 at y (the line's space, 1/65536) */
static void HatchSegment(const GrDraw *d, sdword x1, sdword x2, sdword y,
		    word attr)
{
    int64_t ax = x1, ay = y, bx = x2, by = y;
    sword dx1, dy1, dx2, dy2;

    GrMatTransDWF(&d->GD_w->W_curTMatrix, &ax, &ay);
    GrMatTransDWF(&d->GD_w->W_curTMatrix, &bx, &by);
    if (GrRoundToWord(ax, &dx1) && GrRoundToWord(ay, &dy1) &&
	GrRoundToWord(bx, &dx2) && GrRoundToWord(by, &dy2))
	GrDrawLineLow(d, dx1, dy1, dx2, dy2, attr);
}

#define I(v)	((sdword) (v) >> 16)

/* HatchDrawLine: one HatchLine repeated over the fill bounds */
static void HatchLineDraw(GStateHandle gstate, const HatchLine *hl,
			  word attr)
{
    const HatchDash *dash = (const HatchDash *) (hl + 1);
    sdword len = 0, x = 0, y = 0, dX = WWF(&hl->HL_deltaX),
	dY = WWF(&hl->HL_deltaY), l, t, r, b;
    GrDraw d;
    word i;
    int last;

    GrSetNullTransform(gstate);
    GrApplyTranslation(gstate, WWF(&hl->HL_origin.PF_x),
		       WWF(&hl->HL_origin.PF_y));
    GrApplyRotation(gstate, WWF(&hl->HL_angle));
    if (hl->HL_color.CQ_info != CF_SAME) {
	if (attr == TEXT)
	    GrSetTextColor(gstate, hl->HL_color.CQ_info,
			   hl->HL_color.CQ_redOrIndex, hl->HL_color.CQ_green,
			   hl->HL_color.CQ_blue);
	else
	    GrSetAreaColor(gstate, hl->HL_color.CQ_info,
			   hl->HL_color.CQ_redOrIndex, hl->HL_color.CQ_green,
			   hl->HL_color.CQ_blue);
    }
    GrEnterGraphics(gstate, &d);
    if (GrTrivialReject(&d) || !FillBounds(&d, &l, &t, &r, &b) || dY <= 0) {
	GrExitGraphics(&d);
	return;
    }
    for (i = 0; i < hl->HL_numDashes; i++)
	len += WWF(&dash[i].HD_on) + WWF(&dash[i].HD_off);
    /* HatchCheckY: the last line above the top */
    while (I(y) < t) {
	x += dX;
	y += dY;
    }
    do {
	x -= dX;
	y -= dY;
    } while (I(y) >= t);
    for (last = 0; last < 2;) {
	sdword sx = x;
	/* HatchCheckX: the last start left of the left edge */
	if (len <= 0)
	    sx = l << 16;
	else {
	    while (I(sx) < l)
		sx += len;
	    do
		sx -= len;
	    while (I(sx) >= l);
	}
	/* HatchDoLine */
	if (!hl->HL_numDashes)
	    HatchSegment(&d, sx, r << 16, y, attr);
	else
	    while (I(sx) <= r)
		for (i = 0; i < hl->HL_numDashes && I(sx) <= r; i++) {
		    sdword e = sx + WWF(&dash[i].HD_on);
		    HatchSegment(&d, sx, e, y, attr);
		    sx = e + WWF(&dash[i].HD_off);
		}
	x += dX;
	y += dY;
	if (I(y) > b)
	    last++;
    }
    GrExitGraphics(&d);
}

/* PatternDrawBitmap: the bitmap tiled from the origin */
static void BitmapTile(GStateHandle gstate, const Bitmap *bm)
{
    GrDraw d;
    sdword l, t, r, b, x, y;
    XYValueAsDWord sz;
    word w, h;

    GrEnterGraphics(gstate, &d);
    if (GrTrivialReject(&d) || !FillBounds(&d, &l, &t, &r, &b)) {
	GrExitGraphics(&d);
	return;
    }
    GrExitGraphics(&d);
    sz = GrGetBitmapSize(bm);
    w = (word) sz;
    h = (word) (sz >> 16);
    if (!w || !h)
	return;
    /* BitmapCalcLow: from the multiple at or before the top left */
    for (y = t - ((t % h) + h) % h; y <= b; y += h)
	for (x = l - ((l % w) + w) % w; x <= r; x += w)
	    GrDrawBitmap(gstate, (sword) x, (sword) y, bm, NULL);
}

/* PatternFillLow: the pattern over the clip path (the GState's own) */
static void DrawPattern(GStateHandle gstate, Boolean text)
{
    GState *gs = MemLock(gstate);
    GrCommonPattern *cp = text ? &gs->GS_textPattern : &gs->GS_areaPattern;
    byte type = cp->GCP_pattern;
    byte *copy = NULL;
    word size = 0;

    if (cp->GCP_custom) {
	optr o = ConstructOptr(gstate, cp->GCP_custom);
	size = LMemGetChunkSize(o);
	if ((copy = malloc(size)) != NULL)
	    memcpy(copy, LMemDeref(o), size);
    }
    MemUnlock(gstate);
    if (!copy)
	return;
    GrSetLineEnd(gstate, LE_SQUARECAP);
    GrSetLineStyle(gstate, LS_SOLID, 0, NULL, 0);
    if (type == PT_CUSTOM_BITMAP)
	BitmapTile(gstate, (const Bitmap *) copy);
    else if (size >= sizeof(HatchPattern)) {	/* PatternDrawHatch */
	const HatchPattern *hp = (const HatchPattern *) copy;
	const byte *p = copy + sizeof(HatchPattern);
	word i;
	for (i = 0; i < hp->HP_numLines &&
		 p + sizeof(HatchLine) <= copy + size; i++) {
	    const HatchLine *hl = (const HatchLine *) p;
	    HatchLineDraw(gstate, hl, text ? TEXT : AREA);
	    p += sizeof(HatchLine) + hl->HL_numDashes * sizeof(HatchDash);
	}
    }
    free(copy);
}

/* EnterGraphicsFill, EnterGraphicsText: TRUE if the figure is to be
 * filled with a pattern; then the GState is saved and a path begun,
 * into which the caller draws the figure before GrPatternFinish */
Boolean GrPatternStart(GStateHandle gstate, Boolean text)
{
    GState *gs = MemLock(gstate);
    GrCommonPattern *cp = text ? &gs->GS_textPattern : &gs->GS_areaPattern;
    Boolean pattern = (Boolean) (Drawable(cp->GCP_pattern) &&
				 cp->GCP_custom && gs->GS_window &&
				 !gs->GS_gstring &&
				 !(gs->GS_pathFlags & (PF_DEFINING_PATH |
						       PF_FILL | PF_STROKE)));

    MemUnlock(gstate);
    if (pattern) {
	GrSaveState(gstate);
	GrBeginPath(gstate, PCT_REPLACE);
    }
    return pattern;
}

/* ExitGraphicsFill, PatternFill: the path as the clip path, the pattern
 * drawn, the GState restored, the pen where the figure left it */
void GrPatternFinish(GStateHandle gstate, RegionFillRule rule, Boolean text)
{
    PointWWFixed pen;

    GrGetCurPosWWFixed(gstate, &pen);
    GrEndPath(gstate);
    GrSetClipPath(gstate, PCT_INTERSECTION, rule);
    DrawPattern(gstate, text);
    GrRestoreState(gstate);
    GrMoveToWWFixed(gstate, GR_WWF_D(pen.PF_x), GR_WWF_D(pen.PF_y));
}

/* GrFillPath with a pattern: TRUE if done */
Boolean GrPatternFillPath(GStateHandle gstate, RegionFillRule rule)
{
    GState *gs = MemLock(gstate);
    Boolean pattern = (Boolean) (Drawable(gs->GS_areaPattern.GCP_pattern) &&
				 gs->GS_areaPattern.GCP_custom &&
				 gs->GS_window && !gs->GS_gstring &&
				 gs->GS_currentPath &&
				 !(gs->GS_pathFlags & (PF_DEFINING_PATH |
						       PF_FILL | PF_STROKE)));

    MemUnlock(gstate);
    if (!pattern)
	return FALSE;
    GrSaveState(gstate);
    GrSetClipPath(gstate, PCT_INTERSECTION, rule);
    DrawPattern(gstate, FALSE);
    GrRestoreState(gstate);
    return TRUE;
}
