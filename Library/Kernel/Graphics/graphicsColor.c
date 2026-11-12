/*
 * graphicsColor.c -- colors of the graphics system (after
 * graphicsColor.asm: MapRGBtoIndex, GrMapColorIndex, GrMapColorRGB;
 * graphicsState.asm: SetColor, CalcOtherColorValue).
 *
 * A color is kept both ways in a CommonAttr: the palette index and the
 * RGB value.  Set by RGB, the index is the closest palette entry, and
 * AO_USE_INDEX says whether it is the same color.
 *
 * Not yet: GrCreatePalette and the other palette routines (with the
 * window system); devices of 16 colors or fewer, where the asm
 * searched only the first 16 entries.
 */
#include "graphicsInt.h"

static word Dist(const byte *e, byte r, byte g, byte b)
{
    word dr = (word) (e[0] > r ? e[0] - r : r - e[0]);
    word dg = (word) (e[1] > g ? e[1] - g : g - e[1]);
    word db = (word) (e[2] > b ? e[2] - b : b - e[2]);
    word m = dr > dg ? dr : dg;

    return (word) (dr + dg + db + (db > m ? db : m));
}

/*
 * MapRGBtoIndex: the closest of entries 0 .. last.  On the whole
 * default palette only the first 32 are searched, then the one of the
 * 6x6x6 color cube at 40.
 */
byte GrMapRGBtoIndex(const byte *palette, word last, byte red, byte green,
		     byte blue)
{
    word i, d, best = 0xffff, end = last, cube;
    byte index = 0;

    if (palette == grDefaultPalette && last == 255)
	end = 31;
    for (i = 0; i <= end; i++) {
	d = Dist(palette + 3 * i, red, green, blue);
	if (d < best) {
	    best = d;
	    index = (byte) i;
	    if (!d)
		return index;
	}
    }
    if (end != last) {
	cube = (word) (40 + (((red + 0x19) / 0x33) * 6 +
			     (green + 0x19) / 0x33) * 6 + (blue + 0x19) / 0x33);
	if (Dist(palette + 3 * cube, red, green, blue) < best)
	    index = (byte) cube;
    }
    return index;
}

const byte *GrLockPalette(const GState *gs, word *last, Handle *win)
{
    Window *w;

    *last = 255;
    *win = 0;
    if (gs->GS_gstring || !gs->GS_window)
	return grDefaultPalette;
    w = MemLock(gs->GS_window);
    if (!w)
	return grDefaultPalette;
    if (!w->W_palette) {
	MemUnlock(gs->GS_window);
	return grDefaultPalette;
    }
    *win = gs->GS_window;
    return LMemDeref(ConstructOptr(gs->GS_window, w->W_palette));
}

void GrUnlockPalette(Handle win)
{
    if (win)
	MemUnlock(win);
}

/* GrGetPalette: a copy of the palette (the window's or the default),
 * 256 RGBValues in a new block; the caller frees it */
MemHandle GrGetPalette(GStateHandle gstate, GetPalType flag)
{
    const byte *p = grDefaultPalette;
    GState *gs = NULL;
    Handle win = 0;
    MemHandle mh;
    word last;

    if (flag != GPT_DEFAULT && gstate) {
	gs = MemLock(gstate);
	p = GrLockPalette(gs, &last, &win);
    }
    mh = MemAlloc(256 * 3, HF_SWAPABLE, 0);
    if (mh) {
	memcpy(MemLock(mh), p, 256 * 3);
	MemUnlock(mh);
    }
    GrUnlockPalette(win);
    if (gs)
	MemUnlock(gstate);
    return mh;
}

void GrSetColorIn(const GState *gs, CommonAttr *ca, ColorFlag flag,
		  byte redOrIndex, byte green, byte blue)
{
    const byte *pal, *e;
    word last;
    Handle win;
    byte index;

    if (flag == CF_GRAY) {
	flag = CF_RGB;
	green = blue = redOrIndex;
    } else if (flag == CF_CMY) {
	flag = CF_RGB;
	redOrIndex = (byte) (255 - redOrIndex);
	green = (byte) (255 - green);
	blue = (byte) (255 - blue);
    }
    pal = GrLockPalette(gs, &last, &win);
    ca->CA_flags |= AO_USE_INDEX;
    if (flag == CF_RGB) {
	index = GrMapRGBtoIndex(pal, last, redOrIndex, green, blue);
	e = pal + 3 * index;
	if (e[0] != redOrIndex || e[1] != green || e[2] != blue)
	    ca->CA_flags &= (AttrOpts) ~AO_USE_INDEX;
	ca->CA_colorIndex = index;
	ca->CA_colorRGB.RGB_red = redOrIndex;
	ca->CA_colorRGB.RGB_green = green;
	ca->CA_colorRGB.RGB_blue = blue;
    } else {
	e = pal + 3 * redOrIndex;
	ca->CA_colorIndex = redOrIndex;
	ca->CA_colorRGB.RGB_red = e[0];
	ca->CA_colorRGB.RGB_green = e[1];
	ca->CA_colorRGB.RGB_blue = e[2];
    }
    GrUnlockPalette(win);
}

/* the index in the high byte, red, green, blue in the low three */
static RGBColorAsDWord AsDWord(byte index, const byte *e)
{
    return ((dword) index << 24) | ((dword) e[2] << 16) |
	   ((dword) e[1] << 8) | e[0];
}

RGBColorAsDWord GrMapColorIndex(GStateHandle gstate, Color c)
{
    GState *gs = MemLock(gstate);
    const byte *pal;
    word last;
    Handle win;
    RGBColorAsDWord v;

    pal = GrLockPalette(gs, &last, &win);
    v = AsDWord(c, pal + 3 * c);
    GrUnlockPalette(win);
    MemUnlock(gstate);
    return v;
}

RGBColorAsDWord GrMapColorRGB(GStateHandle gstate, word red, word green,
			      word blue, Color *index)
{
    GState *gs = MemLock(gstate);
    const byte *pal;
    word last;
    Handle win;
    byte i;
    RGBColorAsDWord v;

    pal = GrLockPalette(gs, &last, &win);
    i = GrMapRGBtoIndex(pal, last, (byte) red, (byte) green, (byte) blue);
    v = AsDWord(i, pal + 3 * i);
    GrUnlockPalette(win);
    MemUnlock(gstate);
    if (index)
	*index = i;
    return v;
}
