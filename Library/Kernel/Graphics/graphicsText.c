/*
 * graphicsText.c -- drawing text (after graphicsText.asm: GrDrawText,
 * GrDrawTextAtCP, GrDrawChar, GrDrawCharAtCP, TextCallDriver,
 * UpdateTextPos; LibDrawKernelStyles and DrawOneBar of
 * graphicsTextObscure.asm).
 *
 * The position is the text's left edge at its top (the font's box),
 * or at the baseline, the accent or the bottom (TM_DRAW_BASE,
 * TM_DRAW_ACCENT, TM_DRAW_BOTTOM).  It goes through the window's matrix
 * to the video driver (DR_VID_PUTSTRING, WBFixed device coordinates);
 * a bitmap font is drawn unscaled there.  The kernel draws the
 * underline and the strike-through itself (KERNEL_STYLES), a bar from
 * the start to the end of the text in the text attributes.  The pen
 * ends after the text.
 *
 * Graphics strings and paths store the text (graphicsString*.c); a path
 * gets the outlines of the characters from the font driver when it is
 * used (GrTextOutline, graphicsPath.c).  Not yet: the bars of rotated
 * text (a polygon in the asm), right to left text, GrDrawTextField.
 */
#include "graphicsInt.h"
#include <Internal/fontDr.h>
#include <stdlib.h>

#define TEXT	((word) offsetof(GState, GS_textAttr))

static sdword W256(const WBFixed *w)
{
    return ((sdword) w->WBF_int << 8) + w->WBF_frac;
}

/* DrawOneBar: from x1 to x2 at y (doc, 1/256) pos below, thick high */
static void DrawBar(const GrDraw *d, sdword x1, sdword x2, sdword y,
		    sdword pos, sdword thick)
{
    int64_t ax = (int64_t) x1 << 8, ay = (int64_t) (y + pos) << 8;
    int64_t bx = (int64_t) x2 << 8, by = (int64_t) (y + pos + thick) << 8;
    sword l, t, r, b;

    if (d->GD_w->W_curTMatrix.TM_flags & TM_ROTATED)
	return;
    GrMatTransDWF(&d->GD_w->W_curTMatrix, &ax, &ay);
    GrMatTransDWF(&d->GD_w->W_curTMatrix, &bx, &by);
    if (GrRoundToWord(ax, &l) && GrRoundToWord(ay, &t) &&
	GrRoundToWord(bx, &r) && GrRoundToWord(by, &b))
	GrFillRectLow(d, l, t, r, b, TEXT);
}

/* TextCallDriver: size characters (0: to the null) at x, y (doc,
 * 1/65536) */
static void DrawText(GStateHandle gstate, int64_t x, int64_t y,
		     const char *str, word size)
{
    GrDraw d;
    GState *gs;
    FontBuf *fb;
    const TMatrix *m;
    sdword width, top;
    word n;

    GrEnterGraphics(gstate, &d);
    gs = d.GD_gs;
    m = d.GD_w ? &d.GD_w->W_curTMatrix : &gs->GS_TMatrix;
    fb = GrLockFont(gs, m);
    if (!fb) {
	GrSetDocWWFPenPos(gs, (sdword) x, (sdword) y);
	GrExitGraphics(&d);
	return;
    }
    for (n = 0; (!size || n < size) && str[n]; n++)
	;
    width = GrTextWidthLow(gs, fb, str, n);

    top = (sdword) (y >> 8);
    if (gs->GS_textMode & TM_DRAW_BASE)
	top -= W256(&fb->FB_baselinePos);
    else if (gs->GS_textMode & TM_DRAW_ACCENT)
	top -= W256(&fb->FB_accent);
    else if (gs->GS_textMode & TM_DRAW_BOTTOM)
	top -= W256(&fb->FB_height);

    if (d.GD_w && !(d.GD_w->W_grFlags & WGF_MASK_NULL) && n) {
	int64_t px = x, py = (int64_t) top << 8;
	sword ix, iy;
	GrMatTransDWF(m, &px, &py);
	if (GrRoundToWord(px >> 8 << 8, &ix) &&
	    GrRoundToWord(py >> 8 << 8, &iy)) {
	    VidPutStringArgs a;
	    DriverRegs r;
	    memset(&r, 0, sizeof(r));
	    a.VPS_string = str;
	    a.VPS_numChars = n;
	    a.VPS_font = fb;
	    r.DR_ax = (word) (sword) (px >> 16);
	    r.DR_bx = (word) (sword) (py >> 16);
	    r.DR_dx = (word) ((((py >> 8) & 0xff) << 8) | ((px >> 8) & 0xff));
	    r.DR_bpPtr = &a;
	    GrCallDriver(&d, DR_VID_PUTSTRING, &r, TEXT);
	    fb = a.VPS_font;	/* characters built: it may have moved */
	}
	/* LibDrawKernelStyles */
	if (gs->GS_fontAttr.FCA_textStyle & TS_UNDERLINE)
	    DrawBar(&d, (sdword) (x >> 8), (sdword) (x >> 8) + width, top,
		    W256(&fb->FB_underPos), W256(&fb->FB_underThickness));
	if (gs->GS_fontAttr.FCA_textStyle & TS_STRIKE_THRU)
	    DrawBar(&d, (sdword) (x >> 8), (sdword) (x >> 8) + width, top,
		    W256(&fb->FB_strikePos), W256(&fb->FB_underThickness));
    }
    GrUnlockFont(gs);
    /* UpdateTextPos */
    GrSetDocWWFPenPos(gs, (sdword) (x + ((int64_t) width << 8)), (sdword) y);
    GrExitGraphics(&d);
}

/*
 * DrawTextToPath: the outlines of size characters (0: to the null) at
 * x, y (doc, 1/65536) as path elements (C heap, *len bytes; NULL if
 * none): each character's from the font driver (DR_FONT_GEN_PATH, the
 * driver drawing it with GrMoveTo, GrDrawLineTo, GrDrawCurveTo and
 * transformations), recorded as a path is, between GR_SAVE_TRANSFORM
 * and GR_RESTORE_TRANSFORM.  The characters are where DrawText puts
 * them (the widths with kerning); the pen ends after the text.
 */
byte *GrTextOutline(GStateHandle gstate, int64_t x, int64_t y,
		    const char *str, word size, word *len)
{
    GState *gs = MemLock(gstate);
    FontBuf *fb = GrLockFont(gs, &gs->GS_TMatrix);
    sdword *at, top, width;
    MemHandle path;
    Handle saved;
    PathFlags flags;
    byte *elems = NULL;
    word n, i;

    *len = 0;
    if (!fb) {
	MemUnlock(gstate);
	return NULL;
    }
    for (n = 0; (!size || n < size) && str[n]; n++)
	;
    at = malloc(((size_t) n + 1) * sizeof(sdword));
    for (i = 0; at && i <= n; i++)	/* (0 would be to the null) */
	at[i] = i ? GrTextWidthLow(gs, fb, str, i) : 0;
    top = (sdword) (y >> 8);
    if (gs->GS_textMode & TM_DRAW_BASE)
	top -= W256(&fb->FB_baselinePos);
    else if (gs->GS_textMode & TM_DRAW_ACCENT)
	top -= W256(&fb->FB_accent);
    else if (gs->GS_textMode & TM_DRAW_BOTTOM)
	top -= W256(&fb->FB_height);
    width = GrTextWidthLow(gs, fb, str, n);
    GrUnlockFont(gs);
    path = at ? GrAllocPathString() : 0;
    if (path) {
	saved = gs->GS_gstring;
	flags = gs->GS_pathFlags;
	gs->GS_gstring = path;
	gs->GS_pathFlags = PF_DEFINING_PATH;
	MemUnlock(gstate);
	for (i = 0; i < n; i++) {
	    DriverRegs r;
	    GrSaveTransform(gstate);
	    gs = MemLock(gstate);
	    GrSetDocWWFPenPos(gs, (sdword) (x + ((int64_t) at[i] << 8)),
			      top << 8);
	    MemUnlock(gstate);
	    GrMoveToWWFixed(gstate, (sdword) (x + ((int64_t) at[i] << 8)),
			    top << 8);
	    memset(&r, 0, sizeof(r));
	    r.DR_bx = gstate;
	    r.DR_dx = (byte) str[i];
	    (void) GrCallFontDriver(gstate, DR_FONT_GEN_PATH, &r);
	    GrRestoreTransform(gstate);
	}
	gs = MemLock(gstate);
	gs->GS_gstring = saved;
	gs->GS_pathFlags = flags;
	elems = GrFreePathString(path, len);
    }
    free(at);
    GrSetDocWWFPenPos(gs, (sdword) (x + ((int64_t) width << 8)), (sdword) y);
    MemUnlock(gstate);
    if (elems && !*len) {
	free(elems);
	elems = NULL;
    }
    return elems;
}

/* the font's box of size characters (0: to the null) at x, y (doc,
 * 1/65536): left, top, right, bottom in box (doc, 1/65536); FALSE
 * without a font (GrGetGStringBounds, where a font has no outlines) */
Boolean GrTextBox(GStateHandle gstate, int64_t x, int64_t y, const char *str,
		  word size, int64_t *box)
{
    GState *gs = MemLock(gstate);
    FontBuf *fb = GrLockFont(gs, &gs->GS_TMatrix);
    sdword top;
    word n;

    if (!fb) {
	MemUnlock(gstate);
	return FALSE;
    }
    for (n = 0; (!size || n < size) && str[n]; n++)
	;
    top = (sdword) (y >> 8);
    if (gs->GS_textMode & TM_DRAW_BASE)
	top -= W256(&fb->FB_baselinePos);
    else if (gs->GS_textMode & TM_DRAW_ACCENT)
	top -= W256(&fb->FB_accent);
    else if (gs->GS_textMode & TM_DRAW_BOTTOM)
	top -= W256(&fb->FB_height);
    box[0] = x;
    box[1] = (int64_t) top << 8;
    box[2] = x + ((int64_t) GrTextWidthLow(gs, fb, str, n) << 8);
    box[3] = (int64_t) (top + W256(&fb->FB_height)) << 8;
    GrUnlockFont(gs);
    MemUnlock(gstate);
    return TRUE;
}

/* the length of a string (size 0: null terminated) */
static word TextLen(const char *str, word size)
{
    return size ? size : (word) strlen(str);
}

void GrDrawText(GStateHandle gstate, sword x, sword y, const char *str,
		word size)
{
    if (GrPatternStart(gstate, TRUE)) {
	GrDrawText(gstate, x, y, str, size);
	GrPatternFinish(gstate, ODD_EVEN, TRUE);
	return;
    }
    (void) GrGSRecord(gstate, GR_DRAW_TEXT, "wwwr", x, y, TextLen(str, size),
		      str, TextLen(str, size));
    DrawText(gstate, (int64_t) x * 65536, (int64_t) y * 65536, str, size);
}

static void TextAtCP(GStateHandle gstate, const char *str, word size);

void GrDrawTextAtCP(GStateHandle gstate, const char *str, word size)
{
    if (GrPatternStart(gstate, TRUE)) {
	GrDrawTextAtCP(gstate, str, size);
	GrPatternFinish(gstate, ODD_EVEN, TRUE);
	return;
    }
    (void) GrGSRecord(gstate, GR_DRAW_TEXT_CP, "wr", TextLen(str, size), str,
		      TextLen(str, size));
    TextAtCP(gstate, str, size);
}

static void TextAtCP(GStateHandle gstate, const char *str, word size)
{
    PointWWFixed cp;

    GrGetCurPosWWFixed(gstate, &cp);
    DrawText(gstate,
	     (int64_t) cp.PF_x.WWF_int * 65536 + cp.PF_x.WWF_frac,
	     (int64_t) cp.PF_y.WWF_int * 65536 + cp.PF_y.WWF_frac, str, size);
}

void GrDrawChar(GStateHandle gstate, sword x, sword y, word ch)
{
    char c = (char) ch;

    if (GrPatternStart(gstate, TRUE)) {
	GrDrawChar(gstate, x, y, ch);
	GrPatternFinish(gstate, ODD_EVEN, TRUE);
	return;
    }
    (void) GrGSRecord(gstate, GR_DRAW_CHAR, "bww", ch, x, y);
    DrawText(gstate, (int64_t) x * 65536, (int64_t) y * 65536, &c, 1);
}

void GrDrawCharAtCP(GStateHandle gstate, word ch)
{
    char c = (char) ch;

    if (GrPatternStart(gstate, TRUE)) {
	GrDrawCharAtCP(gstate, ch);
	GrPatternFinish(gstate, ODD_EVEN, TRUE);
	return;
    }
    (void) GrGSRecord(gstate, GR_DRAW_CHAR_CP, "b", ch);
    TextAtCP(gstate, &c, 1);
}
