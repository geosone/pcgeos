/*
 * graphicsTransformSimple.c -- after graphicsTransformSimple.asm:
 * WinSetNullTransform, WinGetTransform.
 */
#include "../Win/winInt.h"
#include "../Graphics/graphicsInt.h"

void WinSetNullTransform(WindowHandle win, WinInvalFlag flag)
{
    WindowHandle wh;
    Window *w = WinEnterTrans(win, &wh);

    if (!w)
	return;
    GrMatIdentity(&w->W_TMatrix);
    WinFinishTrans(wh, flag);
}

void WinGetTransform(WindowHandle win, TransMatrix *tm)
{
    WindowHandle wh;
    Window *w = WinEnterTrans(win, &wh);

    if (!w)
	return;
    GrMatToTransMatrix(&w->W_TMatrix, tm);
    MemUnlock(wh);
    WinTreeV();
}
