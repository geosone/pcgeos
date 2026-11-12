/*
 * graphicsEllipse.c -- ellipses (after graphicsEllipse.asm and
 * graphicsArcLow.asm).
 *
 * For now only the entry the fat line code uses for round ends and
 * joins (FillArcEllipseLow with SetupEllipseLineLow), which draws
 * nothing until CalcEllipse and CalcConic are ported.
 */
#include "graphicsInt.h"

void GrFillEllipseDev(const GrDraw *d, sword l, sword t, sword r, sword b,
		      word attr)
{
    (void) d; (void) l; (void) t; (void) r; (void) b; (void) attr;
}
