/*
 * winTrans.c -- the transformation of a window (after winTrans.asm:
 * WinTransform, WinUntransform, WinTransformDWord, WinUntransformDWord,
 * WinApplyRotation, WinApplyScale, WinApplyTranslation,
 * WinApplyTranslationDWord, WinSetTransform, WinApplyTransform,
 * WinSetNullTransform, WinGetTransform, FinishWinTransform,
 * InvalWholeWin).
 *
 * W_TMatrix goes from page to window coordinates; screen coordinates
 * are those moved by the window's left and top.  A change goes before
 * the matrix (op * W_TMatrix) like the GState's (graphicsTransform.c),
 * makes the composed matrices invalid and, with WIF_INVALIDATE,
 * invalidates the whole window.  WinApplyTranslation does not move
 * what is on the screen (WinScroll does).
 */
#include "winInt.h"
#include "../Graphics/graphicsInt.h"

static Window *Enter(Handle h, WindowHandle *wh)
{
    Window *w;

    WinTreeP();
    w = WinLockFromHandle(h, wh);
    if (!w)
	WinTreeV();
    return w;
}

/* FinishWinTransform, InvalWholeWin */
static void Finish(WindowHandle wh, WinInvalFlag flag)
{
    Window *w = MemDeref(wh);

    w->W_TMatrix.TM_flags &= (TransFlags) ~TF_INV_VALID;
    GrMatCalcInverse(&w->W_TMatrix);
    w->W_grFlags &= (WinGrFlags) ~WGF_XFORM_VALID;
    w->W_grRegFlags &= (WinGrRegFlags) ~(WGRF_PATH_VALID |
					 WGRF_WIN_PATH_VALID);
    if (flag == WIF_INVALIDATE) {
	WinRegCopy(WIN_REG(wh, w->W_visReg), WIN_REG(wh, w->W_temp1Reg));
	WinAddToInvalReg(wh);
    }
    MemUnlock(wh);
    WinTreeV();
}

void WinApplyRotation(WindowHandle win, WWFixedAsDWord angle,
		      WinInvalFlag flag)
{
    WindowHandle wh;
    Window *w = Enter(win, &wh);

    if (!w)
	return;
    GrMatRotate(&w->W_TMatrix, angle);
    Finish(wh, flag);
}

void WinApplyScale(WindowHandle win, WWFixedAsDWord xScale,
		   WWFixedAsDWord yScale, WinInvalFlag flag)
{
    WindowHandle wh;
    Window *w = Enter(win, &wh);

    if (!w)
	return;
    if (xScale == 0x10000UL && yScale == 0x10000UL) {
	MemUnlock(wh);
	WinTreeV();
	return;
    }
    GrMatScale(&w->W_TMatrix, xScale, yScale);
    Finish(wh, flag);
}

void WinApplyTranslation(WindowHandle win, WWFixedAsDWord xTrans,
			 WWFixedAsDWord yTrans, WinInvalFlag flag)
{
    WindowHandle wh;
    Window *w = Enter(win, &wh);

    if (!w)
	return;
    GrMatTranslate(&w->W_TMatrix, (sdword) xTrans, (sdword) yTrans);
    Finish(wh, flag);
}

void WinApplyTranslationDWord(WindowHandle win, sdword xTrans,
			      sdword yTrans, WinInvalFlag flag)
{
    WindowHandle wh;
    Window *w = Enter(win, &wh);

    if (!w)
	return;
    GrMatTranslate(&w->W_TMatrix, (int64_t) xTrans * 65536,
		   (int64_t) yTrans * 65536);
    Finish(wh, flag);
}

void WinSetTransform(WindowHandle win, const TransMatrix *tm,
		     WinInvalFlag flag)
{
    WindowHandle wh;
    Window *w = Enter(win, &wh);

    if (!w)
	return;
    GrMatFromTransMatrix(&w->W_TMatrix, tm);
    Finish(wh, flag);
}

void WinApplyTransform(WindowHandle win, const TransMatrix *tm,
		       WinInvalFlag flag)
{
    WindowHandle wh;
    Window *w = Enter(win, &wh);
    TMatrix m;

    if (!w)
	return;
    GrMatFromTransMatrix(&m, tm);
    GrMatMul(&w->W_TMatrix, &m, &w->W_TMatrix);
    GrMatSetFlags(&w->W_TMatrix);
    Finish(wh, flag);
}

void WinSetNullTransform(WindowHandle win, WinInvalFlag flag)
{
    WindowHandle wh;
    Window *w = Enter(win, &wh);

    if (!w)
	return;
    GrMatIdentity(&w->W_TMatrix);
    Finish(wh, flag);
}

void WinGetTransform(WindowHandle win, TransMatrix *tm)
{
    WindowHandle wh;
    Window *w = Enter(win, &wh);

    if (!w)
	return;
    GrMatToTransMatrix(&w->W_TMatrix, tm);
    MemUnlock(wh);
    WinTreeV();
}

/* WinTransCoord: rounded to words; 0x8000, 0x8000 on overflow */
XYValueAsDWord WinTransform(WindowHandle win, sword x, sword y)
{
    WindowHandle wh;
    Window *w = Enter(win, &wh);
    int64_t px = (int64_t) x * 65536, py = (int64_t) y * 65536;
    sword rx, ry;

    if (!w)
	return 0x80008000UL;
    GrMatTransDWF(&w->W_TMatrix, &px, &py);
    if (!GrRoundToWord(px, &rx) || !GrRoundToWord(py, &ry))
	rx = ry = (sword) 0x8000;
    else {
	rx = (sword) (rx + w->W_winRect.R_left);
	ry = (sword) (ry + w->W_winRect.R_top);
    }
    MemUnlock(wh);
    WinTreeV();
    return ((dword) (word) ry << 16) | (word) rx;
}

XYValueAsDWord WinUntransform(WindowHandle win, sword x, sword y)
{
    WindowHandle wh;
    Window *w = Enter(win, &wh);
    int64_t px, py;
    sword rx, ry;

    if (!w)
	return 0x80008000UL;
    px = (int64_t) (x - w->W_winRect.R_left) * 65536;
    py = (int64_t) (y - w->W_winRect.R_top) * 65536;
    GrMatUntransDWF(&w->W_TMatrix, &px, &py);
    if (!GrRoundToWord(px, &rx) || !GrRoundToWord(py, &ry))
	rx = ry = (sword) 0x8000;
    MemUnlock(wh);
    WinTreeV();
    return ((dword) (word) ry << 16) | (word) rx;
}

void WinTransformDWord(WindowHandle win, sdword xCoord, sdword yCoord,
		       PointDWord *screenCoordinates)
{
    WindowHandle wh;
    Window *w = Enter(win, &wh);
    int64_t px = (int64_t) xCoord * 65536, py = (int64_t) yCoord * 65536;

    if (!w)
	return;
    GrMatTransDWF(&w->W_TMatrix, &px, &py);
    screenCoordinates->PD_x = (sdword) ((px + 0x8000) >> 16) +
			      w->W_winRect.R_left;
    screenCoordinates->PD_y = (sdword) ((py + 0x8000) >> 16) +
			      w->W_winRect.R_top;
    MemUnlock(wh);
    WinTreeV();
}

void WinUntransformDWord(WindowHandle win, sdword xCoord, sdword yCoord,
			 PointDWord *documentCoordinates)
{
    WindowHandle wh;
    Window *w = Enter(win, &wh);
    int64_t px, py;

    if (!w)
	return;
    px = ((int64_t) xCoord - w->W_winRect.R_left) * 65536;
    py = ((int64_t) yCoord - w->W_winRect.R_top) * 65536;
    GrMatUntransDWF(&w->W_TMatrix, &px, &py);
    documentCoordinates->PD_x = (sdword) ((px + 0x8000) >> 16);
    documentCoordinates->PD_y = (sdword) ((py + 0x8000) >> 16);
    MemUnlock(wh);
    WinTreeV();
}
