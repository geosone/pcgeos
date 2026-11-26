/*
 * exportFontMap.c -- after exportFontMap.asm: This routine implements a
 * font-mapping algorithm that returns the difference between two fontIDs
 */
#include "exportInt.h"
#include "exportMacro.h"
#include <font.h>
#include <fontID.h>
#include <char.h>
#include <Internal/fontDr.h>

/* CalcFontDiff: how far apart two faces are (FontMap: 0 the same, 0xff
 * not to be used for each other) */
byte CalcFontDiff(FontID want, FontID test)
{
    word a = (word) (want & ~FID_MAKER_MASK), b = (word) (test & ~FID_MAKER_MASK);
    word fa, fb, fam;
    int d;

    if (a == b)
	return FM_EXACT;
    fa = (word) ((a >> 8) & 0x0e);
    fb = (word) ((b >> 8) & 0x0e);
    if (fa != fb) {
	if (fa == (FG_MONO >> 8) || fb == (FG_MONO >> 8))
	    return FM_DONT_USE - 1;
	return FM_DONT_USE;
    }
    fam = (word) (fa >> 1);		/* serif, sans, script, ... */
    if (fam >= 3) {
	/* ornament and symbol faces: none; monospaced: any */
	return fam == (FG_MONO >> 9) ? 1 : FM_DONT_USE;
    }
    a &= 0x1ff;
    b &= 0x1ff;
    if (a > (word) (epsFaceStart[fam + 1] - epsFaceStart[fam]) ||
	b > (word) (epsFaceStart[fam + 1] - epsFaceStart[fam]))
	return FM_DONT_USE;
    d = (int) epsFaces[epsFaceStart[fam] + b] -
	(int) epsFaces[epsFaceStart[fam] + a];
    if (d < 0)
	d = -d;
    return (byte) (d + 1);
}
