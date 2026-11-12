/*
 * graphicsUtils.c -- coordinates and the pen position (after
 * graphicsUtils.asm: GrTransform, GrTransformWWFixed, GrTransformDWord,
 * GrTransformDWFixed, GrUntransform and the rest, SetDocWWFPenPos,
 * SetRelDocPenPos, GetDocPenPos).
 *
 * The pen position (GS_penPos) is kept in page coordinates: the
 * document position through GS_TMatrix.
 *
 * On overflow the asm's C stubs return 0x8000 for both coordinates;
 * here the coordinates in the structures are set to it as well.
 */
#include "graphicsInt.h"
#include "../Win/winInt.h"

#define OVER	((sword) 0x8000)

/* TransCoordCommon, TransComplex: rounded to words */
XYValueAsDWord GrTransform(GStateHandle gstate, sword xCoord, sword yCoord)
{
    GState *gs;
    Window *w;
    TMatrix *m = GrLockTransform(gstate, &gs, &w);
    int64_t x = (int64_t) xCoord * 65536, y = (int64_t) yCoord * 65536;
    sword rx, ry;

    GrMatTransDWF(m, &x, &y);
    GrUnlockTransform(gstate, gs, w);
    if (!GrRoundToWord(x, &rx) || !GrRoundToWord(y, &ry))
	rx = ry = OVER;
    return ((dword) (word) ry << 16) | (word) rx;
}

XYValueAsDWord GrUntransform(GStateHandle gstate, sword xCoord, sword yCoord)
{
    GState *gs;
    Window *w;
    TMatrix *m = GrLockTransform(gstate, &gs, &w);
    int64_t x = (int64_t) xCoord * 65536, y = (int64_t) yCoord * 65536;
    sword rx, ry;

    GrMatUntransDWF(m, &x, &y);
    GrUnlockTransform(gstate, gs, w);
    if (!GrRoundToWord(x, &rx) || !GrRoundToWord(y, &ry))
	rx = ry = OVER;
    return ((dword) (word) ry << 16) | (word) rx;
}

/* the WWFixed results: FALSE if they do not fit */
static Boolean ToWWF(int64_t x, int64_t y, PointWWFixed *p)
{
    Boolean ok = x >= -((int64_t) 1 << 31) && x < ((int64_t) 1 << 31) &&
		 y >= -((int64_t) 1 << 31) && y < ((int64_t) 1 << 31);

    if (!ok)
	x = y = (int64_t) OVER * 65536;
    p->PF_x.WWF_int = (sword) (x >> 16);
    p->PF_x.WWF_frac = (word) x;
    p->PF_y.WWF_int = (sword) (y >> 16);
    p->PF_y.WWF_frac = (word) y;
    return ok;
}

/* TransCoordFixed: not rounded */
void GrTransformWWFixed(GStateHandle gstate, WWFixedAsDWord xPos,
			WWFixedAsDWord yPos, PointWWFixed *deviceCoordinates)
{
    GState *gs;
    Window *w;
    TMatrix *m = GrLockTransform(gstate, &gs, &w);
    int64_t x = (sdword) xPos, y = (sdword) yPos;

    GrMatTransDWF(m, &x, &y);
    GrUnlockTransform(gstate, gs, w);
    ToWWF(x, y, deviceCoordinates);
}

void GrUntransformWWFixed(GStateHandle gstate, WWFixedAsDWord xPos,
			  WWFixedAsDWord yPos, PointWWFixed *documentCoordinates)
{
    GState *gs;
    Window *w;
    TMatrix *m = GrLockTransform(gstate, &gs, &w);
    int64_t x = (sdword) xPos, y = (sdword) yPos;

    GrMatUntransDWF(m, &x, &y);
    GrUnlockTransform(gstate, gs, w);
    ToWWF(x, y, documentCoordinates);
}

void GrTransformDWFixed(GStateHandle gstate, PointDWFixed *coord)
{
    GState *gs;
    Window *w;
    TMatrix *m = GrLockTransform(gstate, &gs, &w);
    int64_t x = GrDWF(&coord->PDF_x), y = GrDWF(&coord->PDF_y);

    GrMatTransDWF(m, &x, &y);
    GrUnlockTransform(gstate, gs, w);
    GrSetDWF(&coord->PDF_x, x);
    GrSetDWF(&coord->PDF_y, y);
}

void GrUntransformDWFixed(GStateHandle gstate, PointDWFixed *coord)
{
    GState *gs;
    Window *w;
    TMatrix *m = GrLockTransform(gstate, &gs, &w);
    int64_t x = GrDWF(&coord->PDF_x), y = GrDWF(&coord->PDF_y);

    GrMatUntransDWF(m, &x, &y);
    GrUnlockTransform(gstate, gs, w);
    GrSetDWF(&coord->PDF_x, x);
    GrSetDWF(&coord->PDF_y, y);
}

/* TransExtCoordCommon: rounded to dwords */
static sdword RoundD(int64_t v)
{
    return (sdword) ((v + 0x8000) >> 16);
}

void GrTransformDWord(GStateHandle gstate, sdword xCoord, sdword yCoord,
		      PointDWord *deviceCoordinates)
{
    GState *gs;
    Window *w;
    TMatrix *m = GrLockTransform(gstate, &gs, &w);
    int64_t x = (int64_t) xCoord * 65536, y = (int64_t) yCoord * 65536;

    GrMatTransDWF(m, &x, &y);
    GrUnlockTransform(gstate, gs, w);
    deviceCoordinates->PD_x = RoundD(x);
    deviceCoordinates->PD_y = RoundD(y);
}

void GrUntransformDWord(GStateHandle gstate, sdword xCoord, sdword yCoord,
			PointDWord *documentCoordinates)
{
    GState *gs;
    Window *w;
    TMatrix *m = GrLockTransform(gstate, &gs, &w);
    int64_t x = (int64_t) xCoord * 65536, y = (int64_t) yCoord * 65536;

    GrMatUntransDWF(m, &x, &y);
    GrUnlockTransform(gstate, gs, w);
    documentCoordinates->PD_x = RoundD(x);
    documentCoordinates->PD_y = RoundD(y);
}

/* the pen position */
void GrSetDocPenPos(GState *gs, sword x, sword y);

static void SetPen(GState *gs, int64_t x, int64_t y)	/* SetDocWWFPenPos */
{
    GrMatTransDWF(&gs->GS_TMatrix, &x, &y);
    GrSetDWF(&gs->GS_penPos.PDF_x, x);
    GrSetDWF(&gs->GS_penPos.PDF_y, y);
}

void GrMoveTo(GStateHandle gstate, sword x, sword y)
{
    GState *gs = MemLock(gstate);

    (void) GrGSRecord(gstate, GR_MOVE_TO, "ww", x, y);
    SetPen(gs, (int64_t) x * 65536, (int64_t) y * 65536);
    MemUnlock(gstate);
}

void GrMoveToWWFixed(GStateHandle gstate, WWFixedAsDWord x, WWFixedAsDWord y)
{
    GState *gs = MemLock(gstate);

    (void) GrGSRecord(gstate, GR_MOVE_TO_WWFIXED, "dd", x, y);
    SetPen(gs, (sdword) x, (sdword) y);
    MemUnlock(gstate);
}

/* SetRelDocPenPos: the offset through the 2x2 part only
 * (TransformRelVector) */
void GrRelMoveTo(GStateHandle gstate, WWFixedAsDWord x, WWFixedAsDWord y)
{
    (void) GrGSRecord(gstate, GR_REL_MOVE_TO, "dd", x, y);
    GrRelMovePen(gstate, x, y);
}

/* GrRelMoveTo, not stored in a graphics string */
void GrRelMovePen(GStateHandle gstate, WWFixedAsDWord x, WWFixedAsDWord y)
{
    GState *gs = MemLock(gstate);
    TMatrix m = gs->GS_TMatrix;
    int64_t dx = (sdword) x, dy = (sdword) y;

    memset(&m.TM_31, 0, sizeof(m.TM_31));
    memset(&m.TM_32, 0, sizeof(m.TM_32));
    GrMatTransDWF(&m, &dx, &dy);
    GrSetDWF(&gs->GS_penPos.PDF_x, GrDWF(&gs->GS_penPos.PDF_x) + dx);
    GrSetDWF(&gs->GS_penPos.PDF_y, GrDWF(&gs->GS_penPos.PDF_y) + dy);
    MemUnlock(gstate);
}

/* GetDocPenPos: back through GS_TMatrix */
static void GetPen(GState *gs, int64_t *x, int64_t *y)
{
    *x = GrDWF(&gs->GS_penPos.PDF_x);
    *y = GrDWF(&gs->GS_penPos.PDF_y);
    GrMatUntransDWF(&gs->GS_TMatrix, x, y);
}

XYValueAsDWord GrGetCurPos(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    int64_t x, y;
    sword rx, ry;

    GetPen(gs, &x, &y);
    MemUnlock(gstate);
    if (!GrRoundToWord(x, &rx) || !GrRoundToWord(y, &ry))
	rx = ry = OVER;
    return ((dword) (word) ry << 16) | (word) rx;
}

void GrGetCurPosWWFixed(GStateHandle gstate, PointWWFixed *cp)
{
    GState *gs = MemLock(gstate);
    int64_t x, y;

    GetPen(gs, &x, &y);
    MemUnlock(gstate);
    ToWWF(x, y, cp);
}

void GrSetDocPenPos(GState *gs, sword x, sword y)
{
    SetPen(gs, (int64_t) x * 65536, (int64_t) y * 65536);
}

void GrSetDocWWFPenPos(GState *gs, sdword x, sdword y)
{
    SetPen(gs, x, y);
}

/* ---------------------------------------------------------------------
 * Drawing
 * ------------------------------------------------------------------- */

/*
 * EnterGraphics: the GState locked and, if it has a window, the tree
 * lock taken, the window locked and made ready for this GState
 * (WinValWinStruct).  A graphics string (GS_gstring) has no window:
 * the routines stored their element before (GrGSRecord) and draw
 * nothing.
 */
void GrEnterGraphics(GStateHandle gstate, GrDraw *d)
{
    Window *w;

    d->GD_gstate = gstate;
    d->GD_gs = MemLock(gstate);
    d->GD_win = 0;
    d->GD_w = NULL;
    if (d->GD_gs->GS_gstring || !d->GD_gs->GS_window)
	return;
    WinTreeP();
    d->GD_win = d->GD_gs->GS_window;
    w = MemLock(d->GD_win);
    if (w->W_curState != gstate ||
	(w->W_grFlags & (WGF_MASK_VALID | WGF_XFORM_VALID)) !=
	(WGF_MASK_VALID | WGF_XFORM_VALID) ||
	(w->W_grRegFlags & (WGRF_PATH_VALID | WGRF_WIN_PATH_VALID)) !=
	(WGRF_PATH_VALID | WGRF_WIN_PATH_VALID))
	WinValWinStruct(gstate, d->GD_gs, d->GD_win);
    d->GD_w = MemDeref(d->GD_win);
}

void GrExitGraphics(GrDraw *d)
{
    if (d->GD_win) {
	MemUnlock(d->GD_win);
	WinTreeV();
    }
    MemUnlock(d->GD_gstate);
}

/* TrivialReject: no window, nothing visible, or a matrix that makes
 * everything a point */
Boolean GrTrivialReject(const GrDraw *d)
{
    const TMatrix *m;

    if (!d->GD_w || (d->GD_w->W_grFlags & WGF_MASK_NULL))
	return TRUE;
    m = &d->GD_w->W_curTMatrix;
    return (Boolean) ((!m->TM_11.WWF_int && !m->TM_11.WWF_frac &&
		       !m->TM_21.WWF_int && !m->TM_21.WWF_frac) ||
		      (!m->TM_12.WWF_int && !m->TM_12.WWF_frac &&
		       !m->TM_22.WWF_int && !m->TM_22.WWF_frac));
}

/* GrTransCoord: a document point to the device (W_curTMatrix) */
Boolean GrTransDev(const GrDraw *d, sword x, sword y, sword *dx, sword *dy)
{
    int64_t px = (int64_t) x * 65536, py = (int64_t) y * 65536;

    GrMatTransDWF(&d->GD_w->W_curTMatrix, &px, &py);
    return (Boolean) (GrRoundToWord(px, dx) && GrRoundToWord(py, dy));
}

/* GetDevPenPos: the pen (page coordinates) through W_TMatrix, moved by
 * the window's position */
Boolean GrGetDevPenPos(const GrDraw *d, sword *x, sword *y)
{
    int64_t px = GrDWF(&d->GD_gs->GS_penPos.PDF_x),
	    py = GrDWF(&d->GD_gs->GS_penPos.PDF_y);

    GrMatTransDWF(&d->GD_w->W_TMatrix, &px, &py);
    px += (int64_t) d->GD_w->W_winRect.R_left * 65536;
    py += (int64_t) d->GD_w->W_winRect.R_top * 65536;
    return (Boolean) (GrRoundToWord(px, x) && GrRoundToWord(py, y));
}

/* a call to the window's video driver with the GState and an
 * attribute offset (GS_areaAttr, GS_lineAttr) */
Boolean GrCallDriver(const GrDraw *d, word function, DriverRegs *r,
		     word attr)
{
    r->DR_dsSi = d->GD_gs;
    r->DR_si = attr;
    return WinCallVidDriver(d->GD_w, function, r);
}
