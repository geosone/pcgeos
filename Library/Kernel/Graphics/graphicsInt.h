/*
 * graphicsInt.h -- inside the graphics system of the C kernel (after
 * graphicsConstant.def, graphicsVariable.def and graphicsMacro.def).
 */
#ifndef __GRAPHICSINT_H
#define __GRAPHICSINT_H

#include "kernelC.h"
#include <graphics.h>
#include <color.h>
#include <lmem.h>
#include <Internal/gstate.h>
#include <Internal/window.h>
#include <stddef.h>
#include <string.h>

/* graphicsTables.c */
#define GR_NUM_SYS_DRAW_MASKS	90
extern const DrawMask grSysDrawMasks[GR_NUM_SYS_DRAW_MASKS];
extern const byte grDefaultPalette[256 * 3];
extern const byte grSysDashArrays[4][1 + 2 * 3];

/* TextMode bits only the kernel sets (TM_INTERNAL) */
#define GR_TM_INTERNAL		0x70
/* the styles the kernel draws itself, not the font (KERNEL_STYLES) */
#define GR_KERNEL_STYLES	(TS_UNDERLINE | TS_STRIKE_THRU)

/* graphicsColor.c */
byte GrMapRGBtoIndex(const byte *palette, word last, byte red, byte green,
		     byte blue);
/* the palette for a GState: the window's, or the default; *win is the
 * window to unlock after (GrUnlockPalette) */
const byte *GrLockPalette(const GState *gs, word *last, Handle *win);
void GrUnlockPalette(Handle win);
/* SetColor: index and RGB of a color into ca */
void GrSetColorIn(const GState *gs, CommonAttr *ca, ColorFlag flag,
		  byte redOrIndex, byte green, byte blue);

/* graphicsState.c */
void GrSetMaskIn(GState *gs, CommonAttr *ca, SysDrawMask sdm,
		 const DrawMask *custom);
void GrShiftMask(GState *gs, DrawMask m, byte xShift, byte yShift);
void GrUpdateOpts(GState *gs);
dword GrReciprocal32(dword v);
void GrInvalidateFont(GState *gs);
/* IntExitGState: the window's cached flags for this GState, and'ed */
void GrWinStateFlags(GStateHandle gstate, const GState *gs, WinGrFlags keep,
		     WinGrRegFlags keepReg);

#endif
