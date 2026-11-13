/*
 * vidmemUtils.c -- the scan lines and pixels of the memory video
 * driver's bitmap (after vidmemUtils.asm: LockHugeBlock, NextHugeBlock,
 * and the pixel code of the format modules Mono, Clr4, Clr8, Clr24).
 *
 * A scan line is an element of the huge array: the mask first if the
 * bitmap has one ((width + 7) / 8 bytes), then the data.  The scan
 * lines of one data block are locked at a time (as bm_lastSeg) and
 * unlocked, dirty, at the end of the driver function.
 *
 * Pixels: monochrome 1 = black, MSB left; 4 bits two a byte, the high
 * nibble left; 8 bits a byte; 24 bits R, G, B.  4 and 8 bits are
 * palette indices (the bitmap's palette, or the default one): a color
 * is drawn as its nearest entry, and the mix modes work on the indices
 * as on the DOS16 color drivers.  Monochrome: the mix on the colors,
 * black where the result is dark, or, in BM_CLUSTERED_DITHER (the print
 * spooler's swaths), where the clustered dither of its luminance is
 * (CMYK/cmykDither.c).  Editing the mask (BM_EDIT_MASK) draws
 * monochrome into the mask.
 */
#include "vidmem.h"

VMBitmap vmBM;
byte vmDefaultPalette[256 * 3];

/* CalcLineSize: the bytes of a scan line, the mask too */
word VMLineSize(word width, byte type)
{
    word bits = (word) ((width + 7) >> 3), n;

    switch (type & BMT_FORMAT) {
    case BMF_MONO:	n = bits; break;
    case BMF_4BIT:	n = (word) ((width + 1) >> 1); break;
    case BMF_8BIT:	n = width; break;
    case BMF_24BIT:	n = (word) (width * 3); break;
    default:		n = (word) (bits << 2); break;	/* CMYK */
    }
    if (type & BMT_MASK)
	n = (word) (n + bits);
    return n;
}

void VMSelect(VMFileHandle file, VMBlockHandle block, void *dir)
{
    const EditableBitmap *eb;
    const CBitmap *c;
    word maskBytes;

    VMRelease();
    vmBM.ownDir = (Boolean) (dir == NULL);
    if (!dir)
	HugeArrayLockDir(file, block, &dir);
    vmBM.file = file;
    vmBM.block = block;
    vmBM.dir = dir;
    eb = dir;
    c = EB_BM(dir);
    vmBM.width = c->CB_simple.B_width;
    vmBM.height = c->CB_simple.B_height;
    vmBM.lineSize = VMLineSize(vmBM.width, c->CB_simple.B_type);
    maskBytes = (word) ((vmBM.width + 7) >> 3);
    vmBM.format = (byte) (c->CB_simple.B_type & BMT_FORMAT);
    vmBM.dataOffset = (c->CB_simple.B_type & BMT_MASK) ? maskBytes : 0;
    vmBM.cluster = (Boolean) ((eb->EB_flags & BM_CLUSTERED_DITHER) &&
			      vmBM.format == BMF_MONO);
    if ((eb->EB_flags & BM_EDIT_MASK) && (c->CB_simple.B_type & BMT_MASK)) {
	vmBM.format = BMF_MONO;
	vmBM.dataOffset = 0;
	vmBM.cluster = FALSE;
    }
    if (c->CB_palette) {
	const byte *p = (const byte *) c + c->CB_palette;
	vmBM.nPal = (word) (p[0] | (p[1] << 8));
	vmBM.pal = p + 2;
    } else {
	vmBM.nPal = 256;
	vmBM.pal = vmDefaultPalette;
    }
    if (vmBM.nPal > 256)
	vmBM.nPal = 256;
    vmBM.line0 = NULL;
    vmBM.count = 0;
    vmBM.dirty = FALSE;
}

static void UnlockLines(void)
{
    if (vmBM.line0) {
	if (vmBM.dirty)
	    HugeArrayDirty(vmBM.line0);
	HugeArrayUnlock(vmBM.line0);
    }
    vmBM.line0 = NULL;
    vmBM.count = 0;
    vmBM.dirty = FALSE;
}

void VMRelease(void)
{
    UnlockLines();
    if (vmBM.file && vmBM.ownDir)
	HugeArrayUnlockDir(vmBM.dir);
    vmBM.file = 0;
    vmBM.dir = NULL;
}

/* LockHugeBlock: scan line y (its data or mask part); NULL off the
 * bitmap */
static byte *Line(sword y)
{
    void *p;
    word size;
    dword r;

    if (!vmBM.file || y < 0 || (word) y >= vmBM.height)
	return NULL;
    if (!vmBM.line0 || (word) y < vmBM.first ||
	(word) y >= vmBM.first + vmBM.count) {
	UnlockLines();
	r = HugeArrayLock(vmBM.file, vmBM.block, (dword) y, &p, &size);
	if (!HAL_COUNT(r))
	    return NULL;
	vmBM.line0 = p;
	vmBM.first = (word) y;
	vmBM.count = (word) HAL_COUNT(r);
    }
    return vmBM.line0 + (word) (y - vmBM.first) * vmBM.lineSize +
	   vmBM.dataOffset;
}

static dword PalColor(byte i)
{
    const byte *e = vmBM.pal + 3 * (i < vmBM.nPal ? i : 0);

    return ((dword) e[0] << 16) | ((dword) e[1] << 8) | e[2];
}

/* the nearest entry of the bitmap's palette (the first n) */
static byte Nearest(dword c, word n)
{
    int r = (int) (c >> 16) & 0xff, g = (int) (c >> 8) & 0xff,
	b = (int) c & 0xff;
    dword best = 0xffffffffUL;
    word i, bi = 0;

    if (n > vmBM.nPal)
	n = vmBM.nPal;
    for (i = 0; i < n; i++) {
	const byte *e = vmBM.pal + 3 * i;
	int dr = e[0] - r, dg = e[1] - g, db = e[2] - b;
	dword d = (dword) (dr * dr + dg * dg + db * db);
	if (d < best) {
	    best = d;
	    bi = i;
	    if (!d)
		break;
	}
    }
    return (byte) bi;
}

static dword Mix(dword d, dword c, MixMode m, dword ones)
{
    switch (m) {
    case MM_CLEAR:	return 0;
    case MM_COPY:	return c;
    case MM_NOP:	return d;
    case MM_AND:	return d & c;
    case MM_INVERT:	return ~d & ones;
    case MM_XOR:	return d ^ c;
    case MM_SET:	return ones;
    case MM_OR:		return d | c;
    default:		return c;
    }
}

static Boolean Dark(dword c)
{
    return (Boolean) ((((c >> 16) & 0xff) * 30 + ((c >> 8) & 0xff) * 59 +
		       (c & 0xff) * 11) < 128 * 100);
}

dword VidDevGetPixel(sword x, sword y)
{
    const byte *p = Line(y);

    if (!p || x < 0 || (word) x >= vmBM.width)
	return 0;
    switch (vmBM.format) {
    case BMF_MONO:
	return (p[x >> 3] & (0x80 >> (x & 7))) ? 0 : 0xffffffUL;
    case BMF_4BIT:
	return PalColor((byte) ((x & 1) ? p[x >> 1] & 0x0f : p[x >> 1] >> 4));
    case BMF_8BIT:
	return PalColor(p[x]);
    default:
	p += 3 * x;
	return ((dword) p[0] << 16) | ((dword) p[1] << 8) | p[2];
    }
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
static Boolean Black(dword c, sword x, sword y)
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

/* a pixel in color c (0x00RRGGBB) with mix mode m */
static void Put(byte *p, sword x, sword y, dword c, MixMode m)
{
    switch (vmBM.format) {
    case BMF_MONO: {
	byte bit = (byte) (0x80 >> (x & 7));
	dword d = (p[x >> 3] & bit) ? 0 : 0xffffffUL;
	if (Black(Mix(d, c, m, 0xffffffUL), x, y))
	    p[x >> 3] |= bit;
	else
	    p[x >> 3] &= (byte) ~bit;
	break;
    }
    case BMF_4BIT: {
	byte *q = p + (x >> 1);
	byte d = (byte) ((x & 1) ? *q & 0x0f : *q >> 4);
	byte v = (byte) (Mix(d, Nearest(c, 16), m, 0x0f) & 0x0f);
	*q = (byte) ((x & 1) ? (*q & 0xf0) | v : (*q & 0x0f) | (v << 4));
	break;
    }
    case BMF_8BIT:
	p[x] = (byte) Mix(p[x], Nearest(c, 256), m, 0xff);
	break;
    default: {
	byte *q = p + 3 * x;
	dword d = ((dword) q[0] << 16) | ((dword) q[1] << 8) | q[2];
	d = Mix(d, c, m, 0xffffffUL);
	q[0] = (byte) (d >> 16);
	q[1] = (byte) (d >> 8);
	q[2] = (byte) d;
	break;
    }
    }
    vmBM.dirty = TRUE;
}

void VidDevPutPixel(sword x, sword y, dword color, MixMode mix)
{
    byte *p = Line(y);

    if (p && x >= 0 && (word) x < vmBM.width)
	Put(p, x, y, color, mix);
}

void VidDevFillRect(sword left, sword top, sword right, sword bottom,
		    const VidDrawState *ds)
{
    sword x, y;

    if (!vmBM.file)
	return;
    if (left < 0)
	left = 0;
    if (top < 0)
	top = 0;
    if (right >= (sword) vmBM.width)
	right = (sword) (vmBM.width - 1);
    if (bottom >= (sword) vmBM.height)
	bottom = (sword) (vmBM.height - 1);
    for (y = top; y <= bottom; y++) {
	byte *p = Line(y);
	byte m = ds->VDS_mask ? ds->VDS_mask[y & 7] : 0xff;
	if (!p)
	    break;
	for (x = left; x <= right; x++)
	    if (m & (0x80 >> (x & 7)))
		Put(p, x, y, ds->VDS_color, ds->VDS_mix);
    }
}

/* n pixels of line sy from sx to line dy at dx, through a buffer (the
 * two lines may be in different blocks), right to left where the
 * destination is to the right on the same line */
#define SPAN_BUF	256

void VidDevCopySpan(sword sx, sword sy, sword dx, sword dy, sword n)
{
    dword buf[SPAN_BUF];
    Boolean back = (Boolean) (sy == dy && dx > sx);
    sword done = 0;

    while (done < n) {
	sword k = (sword) (n - done < SPAN_BUF ? n - done : SPAN_BUF);
	sword off = back ? (sword) (n - done - k) : done, i;
	for (i = 0; i < k; i++)
	    buf[i] = VidDevGetPixel((sword) (sx + off + i), sy);
	for (i = 0; i < k; i++)
	    VidDevPutPixel((sword) (dx + off + i), dy, buf[i], MM_COPY);
	done = (sword) (done + k);
    }
}

void VidDevUpdate(void)
{
    VMRelease();
}
