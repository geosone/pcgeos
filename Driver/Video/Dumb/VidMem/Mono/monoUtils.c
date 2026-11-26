/*
 * monoUtils.c -- after monoUtils.asm: misc routines to support vidmem
 * mono mode
 */
#include "../vidmem.h"

static Boolean Dark(dword c)
{
    return (Boolean) ((((c >> 16) & 0xff) * 30 + ((c >> 8) & 0xff) * 59 +
		       (c & 0xff) * 11) < 128 * 100);
}

/* CalcLuminance (graphicsUtils.asm): r/4 + r/16 + g/2 + g/16 + b/8, the
 * bits shifted out rounding it as the asm's carries did */
static byte Luminance(dword c)
{
    byte r = (byte) (c >> 16), g = (byte) (c >> 8), b = (byte) c;
    byte l, carries = 0;

    l = (byte) (r >> 2);
    carries = (byte) (carries + ((r >> 3) & 1));
    l = (byte) (l + (r >> 4));
    l = (byte) (l + (g >> 1));
    carries = (byte) (carries + ((g >> 3) & 1));
    l = (byte) (l + (g >> 4));
    carries = (byte) (carries + ((b >> 2) & 1));
    l = (byte) (l + (b >> 3));
    return (byte) (l + (carries >> 1) + (carries & 1));
}

/* Black on a monochrome bitmap: by the clustered dither of the color's
 * luminance at x, y (SetDitherClustered: the pattern of (255 - lum) / 8,
 * rounded, anchored at the bitmap's origin, W_ditherY 0), else by its
 * brightness */
Boolean MonoClusteredBlack(dword c, sword x, sword y)
{
    word level;
    const byte (*pat)[3];

    if (!vmBM.cluster)
	return Dark(c);
    level = (word) (255 - Luminance(c));
    level = (word) ((level >> 3) + ((level >> 2) & 1));
    pat = vidmemDitherBlack[level];
    x = (sword) (x % VM_DITHER_WIDTH);
    y = (sword) (y % VM_DITHER_HEIGHT);
    return (Boolean) ((pat[y][x >> 3] & (0x80 >> (x & 7))) != 0);
}
