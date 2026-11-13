/*
 * colorMapRGBToCMYK.c -- PrMapRGBtoCMYKIndex (the C port of
 * colorMapRGBToCMYK.asm): the ribbon color closest to an RGB value, by
 * the sum of the differences plus the largest; a gray is black (0).
 * The asm's loop went one entry past its 7 colors (into the code after
 * the table); C stops at the 7.
 */
#include "printcom.h"

static const byte ribbonPalette[][3] = {
    { 0, 0, 0 }, { 170, 47, 212 }, { 47, 47, 212 }, { 47, 0, 170 },
    { 212, 212, 47 }, { 170, 0, 0 }, { 0, 200, 0 }
};

static word Diff(byte a, byte b)
{
    return (word) (a > b ? a - b : b - a);
}

byte PrMapRGBtoCMYKIndex(byte r, byte g, byte b)
{
    word best = 0xffff, d, dr, dg, db, max;
    byte i, index = 0;

    if (r == g && r == b)
	return 0;
    for (i = 0; i < sizeof(ribbonPalette) / sizeof(ribbonPalette[0]); i++) {
	dr = Diff(ribbonPalette[i][0], r);
	dg = Diff(ribbonPalette[i][1], g);
	db = Diff(ribbonPalette[i][2], b);
	max = dr;
	if (dg >= max)
	    max = dg;
	if (db >= max)
	    max = db;
	d = (word) (dr + dg + db + max);
	if (d < best) {
	    best = d;
	    index = i;
	}
    }
    return index;
}
