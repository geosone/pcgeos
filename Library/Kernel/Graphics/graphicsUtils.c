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
static void SetPen(GState *gs, int64_t x, int64_t y)	/* SetDocWWFPenPos */
{
    GrMatTransDWF(&gs->GS_TMatrix, &x, &y);
    GrSetDWF(&gs->GS_penPos.PDF_x, x);
    GrSetDWF(&gs->GS_penPos.PDF_y, y);
}

void GrMoveTo(GStateHandle gstate, sword x, sword y)
{
    GState *gs = MemLock(gstate);

    SetPen(gs, (int64_t) x * 65536, (int64_t) y * 65536);
    MemUnlock(gstate);
}

void GrMoveToWWFixed(GStateHandle gstate, WWFixedAsDWord x, WWFixedAsDWord y)
{
    GState *gs = MemLock(gstate);

    SetPen(gs, (sdword) x, (sdword) y);
    MemUnlock(gstate);
}

/* SetRelDocPenPos: the offset through the 2x2 part only
 * (TransformRelVector) */
void GrRelMoveTo(GStateHandle gstate, WWFixedAsDWord x, WWFixedAsDWord y)
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
