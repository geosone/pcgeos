/*
 * winInt.h -- internal declarations of the window system in C
 * (Library/Kernel/Win: winUtils.c, winCreDest.c, winWindows.c,
 * winIndividual.c, winState.c, winTrans.c).
 *
 * A window is an LMem block of type LMEM_TYPE_WINDOW that starts with
 * the Window structure; its regions are chunks of the block, in screen
 * coordinates, without bounds words (CInclude/Internal/window.h).
 *
 * Locking: the asm took the tree semaphore for a change of the tree
 * and P-locked each window it worked on.  Here one recursive lock (the
 * tree lock, WinTreeP/WinTreeV) guards every window: a change of the
 * tree, the validation, invalidation, updates and the mask a drawing
 * routine uses.  Windows are then only locked (MemLock).
 */
#ifndef __WININT_H
#define __WININT_H

#include "kernelC.h"
#include <graphics.h>
#include <win.h>
#include <lmem.h>
#include <geosdriver.h>
#include <Internal/window.h>
#include <Internal/gstate.h>
#include <Internal/grWinInt.h>
#include <Internal/videoDr.h>
#include <Internal/im.h>
#include <geode.h>
#include <string.h>

#ifndef WRF_INVAL_TREE
#define WRF_INVAL_TREE		0x04
#endif
#ifndef WPF_WIN_BRANCH_EXCLUDED
#define WPF_WIN_BRANCH_EXCLUDED	0x40
#endif

/* WinPriorityData */
#define WPD_LAYER		0xf0
#define WPD_WIN			0x0f
#define WPD_LAYER_OFFSET	4

#define NULL_WINDOW		0
#define WINDOW_FREE_SPACE	206
#define WIN_NUM_REGIONS		11	/* W_winReg ... W_pathReg */

/* winSaveUnder.c: save under areas */
extern WinUnderFlags winUnderClosing;
extern optr winUnderRestored;
WinUnderFlags WinCheckUnder(Window *w, const Rectangle *b);
void WinClearSaveUnders(WindowHandle any, WinUnderFlags f);
WinUnderFlags WinOverlyingUnders(WindowHandle wh);
void WinUnderChange(WindowHandle wh);
void WinUnderOpened(WindowHandle wh);
void WinUnderInval(WindowHandle wh);
WinUnderFlags WinUnderClosing(WindowHandle wh, WindowHandle parent);
void WinUnderClosed(WindowHandle wh, WindowHandle parent, WinUnderFlags f);

/* winGeode.c: the window variables of a geode, in its private data */
typedef struct {
    optr		GWV_inputObj;	/* gets the geode's input */
    optr		GWV_parentObj;	/* its field */
    optr		GWV_ptrImage;	/* PIL_GEODE */
    word		GWV_flags;	/* GeodeWinFlags */
    WindowHandle	GWV_activeWin;	/* supersedes the implied window */
} GeodeWinVars;
void WinReadGeodeVars(GeodeHandle gh, GeodeWinVars *v);
void WinWriteGeodeVars(GeodeHandle gh, const GeodeWinVars *v);

/* winNotification.c: the window under the pointer */
void WinPtrImageP(void);
void WinPtrImageV(void);
void WinUpdatePtrImages(void);		/* with WinPtrImageP */
WindowHandle WinPtrFinalWin(void);
GeodeHandle WinPtrFinalGeode(void);
void WinChangePtrNotification(void);
void WinPtrClosing(WindowHandle wh, WindowHandle parent);
void WinPtrFreed(WindowHandle wh);

/* winUtils.c: the tree lock */
void WinTreeP(void);
void WinTreeV(void);

/* the window of a window or GState handle, locked; NULL for a GState
 * without a window (a graphics string) */
Window *WinLockFromHandle(Handle h, WindowHandle *win);

/* regions of a window: chunks of block `wh' (the block may move) */
#define WIN_REG(wh, ch)		ConstructOptr((wh), (ch))
Region *WinReg(WindowHandle wh, ChunkHandle ch);
Boolean WinRegIsNull(WindowHandle wh, ChunkHandle ch);
void WinRegNull(WindowHandle wh, ChunkHandle ch);
void WinRegSmall(WindowHandle wh, ChunkHandle ch);
void WinRegRect(WindowHandle wh, ChunkHandle ch, sword l, sword t, sword r,
		sword b);
void WinRegSet(WindowHandle wh, ChunkHandle ch, const Region *reg,
	       const sword *params);
void WinRegCopy(optr src, optr dst);
void WinRegAnd(optr a, optr b, optr dst);
void WinRegOr(optr a, optr b, optr dst);
void WinRegNot(optr a, optr dst);
/* dst = a without b; tmp is a chunk to work in (not a, b or dst) */
void WinRegSub(optr a, optr b, optr dst, optr tmp);
/* Graphics/graphicsPath.c: the clip paths of a GState into a mask */
void GrClipMask(GStateHandle gh, WindowHandle wh, ChunkHandle mask);

/* W_winRect from W_winReg (WinCalcWinBounds) */
void WinCalcWinBounds(WindowHandle wh);
/* call the window's video driver (WinCallVidDriver); TRUE: carry */
Boolean WinCallVidDriver(Window *w, word function, DriverRegs *r);

/* graphicsTransform.c */
void GrComposeMatrix(GStateHandle gstate, GState *gs, Window *w);

/* winWindows.c */
void WinTInsert(WindowHandle wh, WindowHandle parent, byte prio,
		word passFlags);
void WinTRemove(WindowHandle wh);
/* recalculate the regions of the tree that holds wh (WinTValidate) */
void WinTValidateTree(WindowHandle wh);
/* null the visible regions of a branch, so that it is exposed whole */
void WinNullVisBranch(WindowHandle wh);
/* the deepest, frontmost window of the tree at root with the screen
 * point in its W_winReg, or 0 */
WindowHandle WinLocatePointC(WindowHandle root, sword x, sword y);
/* move the regions of a branch by dx, dy (WinMoveHere) */
void WinMoveBranch(WindowHandle wh, sword dx, sword dy);

/* winIndividual.c */
void WinCalcVisReg(WindowHandle wh);
void WinAddToInvalReg(WindowHandle wh);	/* W_temp1Reg, screen */
void WinSendExpEvent(WindowHandle wh);
void WinSendUpdateComplete(WindowHandle wh);
void WinWashOutReg(WindowHandle wh, ChunkHandle reg);
void WinSetupMaskFlags(Window *w);
void WinValWinStruct(GStateHandle gh, GState *gs, WindowHandle wh);

#endif
