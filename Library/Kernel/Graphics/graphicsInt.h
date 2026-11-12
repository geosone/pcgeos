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
#include <stdint.h>
#include <geosdriver.h>
#include <Internal/videoDr.h>
#include <Internal/tmatrix.h>

/* graphicsTables.c */
#define GR_NUM_SYS_DRAW_MASKS	90
extern const DrawMask grSysDrawMasks[GR_NUM_SYS_DRAW_MASKS];
extern const byte grDefaultPalette[256 * 3];
extern const byte grSysDashArrays[4][1 + 2 * 3];
extern const word grSinIntTable[91];
extern const word grSinFracTable[90];

/* TextMode bits only the kernel sets (TM_INTERNAL) */
#define GR_TM_INTERNAL		0x70
/* the styles the kernel draws itself, not the font (KERNEL_STYLES) */
#define GR_KERNEL_STYLES	(TS_UNDERLINE | TS_STRIKE_THRU)

/* graphicsMath.c: the asm's internal names */
#define GR_WWF(i, f)	((WWFixedAsDWord) (((dword) (word) (i) << 16) | (word) (f)))
WWFixedAsDWord GrReciprocal32(WWFixedAsDWord v);
/* GrSDiv32 and GrUDiv32: FALSE on overflow (*q undefined) */
Boolean GrSDiv32(WWFixedAsDWord dividend, WWFixedAsDWord divisor,
		 WWFixedAsDWord *q);
Boolean GrUDiv32(WWFixedAsDWord dividend, WWFixedAsDWord divisor,
		 WWFixedAsDWord *q);

/* graphicsTransformUtils.c: values in 1/65536 as int64_t */
int64_t GrDWF(const DWFixed *d);
void GrSetDWF(DWFixed *d, int64_t v);
int64_t GrMulWD(sdword w, int64_t d);
void GrMatSetFlags(TMatrix *m);
void GrMatIdentity(TMatrix *m);
void GrMatMul(TMatrix *r, const TMatrix *a, const TMatrix *b);
void GrMatScale(TMatrix *m, WWFixedAsDWord sx, WWFixedAsDWord sy);
void GrMatRotate(TMatrix *m, WWFixedAsDWord angle);
void GrMatTranslate(TMatrix *m, int64_t tx, int64_t ty);
void GrMatFromTransMatrix(TMatrix *m, const TransMatrix *tm);
void GrMatToTransMatrix(const TMatrix *m, TransMatrix *tm);
void GrMatTransDWF(const TMatrix *m, int64_t *x, int64_t *y);
void GrMatCalcInverse(TMatrix *m);
void GrMatUntransDWF(TMatrix *m, int64_t *x, int64_t *y);
Boolean GrRoundToWord(int64_t v, sword *r);

/* graphicsTransform.c: the matrix from document to device coordinates
 * of a GState (the window's W_curTMatrix, composed when not valid, or
 * the GState's own); unlock with GrUnlockTransform */
TMatrix *GrLockTransform(GStateHandle gstate, GState **gs, Window **win);
void GrUnlockTransform(GStateHandle gstate, GState *gs, Window *win);
void GrComposeMatrix(GStateHandle gstate, GState *gs, Window *w);

/* graphicsUtils.c: drawing (EnterGraphics, ExitGraphics) */
typedef struct {
    GStateHandle	GD_gstate;
    GState		*GD_gs;
    WindowHandle	GD_win;		/* 0: nothing to draw on */
    Window		*GD_w;
} GrDraw;
void GrEnterGraphics(GStateHandle gstate, GrDraw *d);
void GrExitGraphics(GrDraw *d);
Boolean GrTrivialReject(const GrDraw *d);	/* TRUE: nothing to draw */
Boolean GrTransDev(const GrDraw *d, sword x, sword y, sword *dx,
		   sword *dy);			/* FALSE: overflow */
Boolean GrGetDevPenPos(const GrDraw *d, sword *x, sword *y);
void GrSetDocPenPos(GState *gs, sword x, sword y);
Boolean GrCallDriver(const GrDraw *d, word function, DriverRegs *r,
		     word attr);

/* graphicsOutput.c, graphicsLine.c */
void GrFillRectLow(const GrDraw *d, sword l, sword t, sword r, sword b,
		   word attr);
void GrDrawLineLow(const GrDraw *d, sword x1, sword y1, sword x2, sword y2,
		   word attr);
Boolean GrRejectRect(const GrDraw *d, sword l, sword t, sword r, sword b);
void GrAdjustLineEnds(const GState *gs, sword *x1, sword *y1, sword *x2,
		      sword *y2);

/* graphicsPolyline.c: points in the separator format (C heap) */
sword *GrSeparatorFormat(const GrDraw *d, const Point *points, word n,
			 Boolean close, word *count);
void GrPolylineBounds(const sword *sep, Rectangle *b);
word GrScaleScalar(const GrDraw *d, sdword len);
void GrScaleLineWidth(const GrDraw *d);
Boolean GrCheckThinLine(const GrDraw *d);
void GrDrawSeparatorFormat(const GrDraw *d, const sword *sep, word brush);
void GrDrawPolylineLow(const GrDraw *d, const sword *sep, word count,
		       Boolean connected);
void GrDrawPolylineMed(const GrDraw *d, const sword *sep, word count,
		       Boolean connected);
void GrDrawPolylineDoc(const GrDraw *d, const Point *points, word n,
		       Boolean connected);

/* graphicsPolygon.c */
word *GrPolygonRegion(const Window *w, const sword *sep, word n,
		      RegionFillRule rule, Boolean trivial);
void GrDrawPolygonDev(const GrDraw *d, const sword *sep, word n,
		      RegionFillRule rule, word attr);

/* graphicsMath.c, not in graphics.h */
WWFixedAsDWord GrSqrWWFixed(WWFixedAsDWord i);

/* graphicsCalcConic.c: the conic section scan converter (ConicParams) */
#define GR_CT_ELLIPSE		0
#define GR_CT_ARC		1
#define GR_CAI_FIND_START	0
#define GR_CAI_FIND_END		1
#define GR_CAI_DONE		2
typedef struct {
    Point	C_center;	/* doubled device coordinates, as are all */
    Point	C_beg;		/* where the algorithm starts */
    Point	C_end;		/* and ends */
    int64_t	C_A, C_B, C_C, C_D, C_E, C_F;	/* Ax^2+Bxy+Cy^2+Dx+Ey+F */
    sword	C_winTop;	/* device: points outside these rows */
    sword	C_winBot;	/*   are left out */
    Rectangle	C_bounds;	/* the even octants stop outside */
    byte	C_conicType;	/* GR_CT_ */
    byte	C_conicInfo;	/* GR_CAI_ */
    Point	C_conicBeg;	/* an arc's ends, device */
    Point	C_conicEnd;
} GrConic;
sword *GrCalcConic(GrConic *c, word *count);

/* graphicsCalcEllipse.c: CalcEllipseFlags */
#define GR_CET_ELLIPSE		0
#define GR_CET_BOUNDED_ARC	2
#define GR_CET_BOUNDED_ARC_RR	4
#define GR_CET_3PT_ARC		6
#define GR_CEF_TYPE		0x0007
#define GR_CEF_COLINEAR		0x4000
#define GR_CEF_DONT_TRANSFORM	0x8000
/* the arc parameters CalcEllipse takes and returns (the asm's
 * BoundedArcParams, ThreePointArcParams and ArcReturnParams over one
 * stack buffer) */
typedef struct {
    Point	GA_startPoint;	/* bounded arcs: device, doubled */
    Point	GA_endPoint;
    PointWWFixed GA_point1;	/* 3-point arcs: document */
    PointWWFixed GA_point2;
    PointWWFixed GA_point3;
    Point	GA_start;	/* returned: the arc's ends and center, */
    Point	GA_end;		/*   device */
    Point	GA_center;
} GrArcParams;
sword *GrCalcEllipse(const GrDraw *d, sword l, sword t, sword r, sword b,
		     word flags, GrArcParams *ap, word *count, sword *top,
		     sword *bot);

/* graphicsArcLow.c */
void GrFillEllipseDev(const GrDraw *d, sword l, sword t, sword r, sword b,
		      word attr);
void GrDrawArcEllipseLow(const GrDraw *d, sword l, sword t, sword r,
			 sword b, word flags, GrArcParams *ap);
void GrFillArcEllipseLow(const GrDraw *d, sword l, sword t, sword r,
			 sword b, word flags, GrArcParams *ap, word attr);

/* graphicsFatLine.c */
void GrDrawFatLines(const GrDraw *d, const sword *sep, Boolean connected);

/* graphicsLineStyles.c */
sword *GrDashPolyline(const GrDraw *d, const sword *sep, word count);
void GrDrawLineLowStyled(const GrDraw *d, sword x1, sword y1, sword x2,
			 sword y2, word attr);

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
void GrInvalidateFont(GState *gs);
/* IntExitGState: the window's cached flags for this GState, and'ed */
void GrWinStateFlags(GStateHandle gstate, const GState *gs, WinGrFlags keep,
		     WinGrRegFlags keepReg);

#endif
