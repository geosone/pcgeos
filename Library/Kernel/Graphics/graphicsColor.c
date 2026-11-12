/*
 * graphicsColor.c -- colors of the graphics system (after
 * graphicsColor.asm: MapRGBtoIndex, GrMapColorIndex, GrMapColorRGB;
 * graphicsState.asm: SetColor, CalcOtherColorValue).
 *
 * A color is kept both ways in a CommonAttr: the palette index and the
 * RGB value.  Set by RGB, the index is the closest palette entry, and
 * AO_USE_INDEX says whether it is the same color.
 *
 * A window may have a palette of its own (GrCreatePalette): a chunk of
 * 256 RGB values in the window block (W_palette), the default palette
 * at first.  GrSetPalette and GrSetPaletteEntry change it, GrGetPalette
 * copies it (or the default) into a block, GrDestroyPalette frees it.
 * They do not reach the device: WinRealizePalette does (winState.c).
 * Graphics strings store them (not paths).
 *
 * Not yet: devices of 16 colors or fewer, where the asm searched only
 * the first 16 entries.
 */
#include "graphicsInt.h"
#include "../Win/winInt.h"

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

/* EnterColor: the window of gstate, locked under the tree lock (its
 * handle in *wh); NULL for none or a graphics string */
static Window *EnterColor(GStateHandle gstate, WindowHandle *wh)
{
    GState *gs = MemLock(gstate);
    WindowHandle win = gs->GS_window;
    Handle gstring = gs->GS_gstring;

    MemUnlock(gstate);
    *wh = 0;
    if (gstring || !win)
	return NULL;
    WinTreeP();
    *wh = win;
    return MemLock(win);
}

static void ExitColor(WindowHandle wh)
{
    if (wh) {
	MemUnlock(wh);
	WinTreeV();
    }
}

static byte *WinPalette(WindowHandle wh)
{
    Window *w = MemDeref(wh);

    return w->W_palette ? LMemDeref(ConstructOptr(wh, w->W_palette))
			: NULL;
}

word GrCreatePalette(GStateHandle gstate)
{
    WindowHandle wh;
    Window *w;
    ChunkHandle ch = 0;

    (void) GrGSRecord(gstate, GR_CREATE_PALETTE, "");
    if ((w = EnterColor(gstate, &wh)) != NULL) {
	ch = w->W_palette;
	if (!ch && (ch = LMemAlloc(wh, 256 * 3)) != 0) {
	    w = MemDeref(wh);
	    w->W_palette = ch;
	    memcpy(WinPalette(wh), grDefaultPalette, 256 * 3);
	}
    }
    ExitColor(wh);
    return ch;
}

void GrDestroyPalette(GStateHandle gstate)
{
    WindowHandle wh;
    Window *w;

    (void) GrGSRecord(gstate, GR_DESTROY_PALETTE, "");
    if ((w = EnterColor(gstate, &wh)) != NULL && w->W_palette) {
	ChunkHandle ch = w->W_palette;
	w->W_palette = 0;
	LMemFree(ConstructOptr(wh, ch));
    }
    ExitColor(wh);
}

/* the entries from index on (a byte), as many as fit */
void GrSetPalette(GStateHandle gstate, const RGBValue *buffer, word index,
		  word numEntries)
{
    WindowHandle wh;
    byte *p;

    index = (byte) index;
    (void) GrGSRecord(gstate, GR_SET_PALETTE, "bwr", index, numEntries,
		      buffer, (word) (numEntries * 3));
    if (EnterColor(gstate, &wh) && (p = WinPalette(wh)) != NULL) {
	if (index + numEntries > 256)
	    numEntries = (word) (256 - index);
	memcpy(p + index * 3, buffer, (size_t) numEntries * 3);
    }
    ExitColor(wh);
}

/* OpSetPaletteEntry: the entry as a word, then red, green, blue (the
 * asm wrote the index into both bytes of the word) */
void GrSetPaletteEntry(GStateHandle gstate, word index, word red,
		       word green, word blue)
{
    WindowHandle wh;
    byte *p;

    index = (byte) index;
    (void) GrGSRecord(gstate, GR_SET_PALETTE_ENTRY, "wbbb", index, red,
		      green, blue);
    if (EnterColor(gstate, &wh) && (p = WinPalette(wh)) != NULL) {
	p += index * 3;
	p[0] = (byte) red;
	p[1] = (byte) green;
	p[2] = (byte) blue;
    }
    ExitColor(wh);
}

/* a block: the number of entries (256), then the RGB values; the
 * window's palette or the default one; 0 for a graphics string */
MemHandle GrGetPalette(GStateHandle gstate, GetPalType flag)
{
    WindowHandle wh = 0;
    const byte *p;
    MemHandle mh;
    byte *b;

    if (gstate) {
	GState *gs = MemLock(gstate);
	Handle gstring = gs->GS_gstring;
	MemUnlock(gstate);
	if (gstring)
	    return 0;
    }
    mh = MemAlloc(256 * 3 + 2, HF_SHARABLE | HF_SWAPABLE,
		  HAF_ZERO_INIT | HAF_LOCK | HAF_NO_ERR);
    b = MemDeref(mh);
    if (gstate)
	(void) EnterColor(gstate, &wh);
    p = wh && flag != GPT_DEFAULT ? WinPalette(wh) : NULL;
    b[0] = 0;
    b[1] = 1;				/* 256 entries */
    memcpy(b + 2, p ? p : grDefaultPalette, 256 * 3);
    ExitColor(wh);
    MemUnlock(mh);
    return mh;
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
