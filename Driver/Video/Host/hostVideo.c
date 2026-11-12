/*
 * hostVideo.c -- the device part of the host video driver (with the C
 * vidcom of Driver/Video/VidCom).  No asm original: DOS16 had a driver
 * per video hardware; on a host the screen is the surface of
 * ghal_display (CInclude/ghal.h), 32 bits a pixel, 0x00RRGGBB, a window
 * of the window system, the whole screen or a frame buffer, whatever
 * the GHAL backend is.
 *
 * The device name gives the size, "<anything>: WxH" or "WxH" (640x480
 * when it has none); full screen (GEOS_DISPLAY) takes the screen's.
 * The driver draws into the surface and flushes what changed at the end
 * of each driver function; with the screen off (DR_VID_SCREEN_OFF) it
 * keeps drawing and shows all when it is on again.  To the GEOS side it
 * is a 24-bit device (BMF_24BIT).
 */
#include "host.h"

#define DEFAULT_WIDTH	640
#define DEFAULT_HEIGHT	480

VIDEO_DRIVER_TABLE(24, 24, BMF_24BIT,
		   (DS_STANDARD << DT_DISP_SIZE_OFFSET) |
		   (DAR_NORMAL << DT_DISP_ASPECT_RATIO_OFFSET) | DC_CF_RGB);

ghal_surface hostSurf;
static Boolean hostOpen, screenOn = TRUE;
static sword dirtyL, dirtyT, dirtyR = -1, dirtyB = -1;

void HostDirty(sword left, sword top, sword right, sword bottom)
{
    if (dirtyR < dirtyL) {
	dirtyL = left;
	dirtyT = top;
	dirtyR = right;
	dirtyB = bottom;
	return;
    }
    if (left < dirtyL)
	dirtyL = left;
    if (top < dirtyT)
	dirtyT = top;
    if (right > dirtyR)
	dirtyR = right;
    if (bottom > dirtyB)
	dirtyB = bottom;
}

void VidDevUpdate(void)
{
    if (dirtyR < dirtyL || !hostOpen)
	return;
    if (screenOn)
	ghal_display_flush(dirtyL, dirtyT, dirtyR - dirtyL + 1,
			   dirtyB - dirtyT + 1);
    dirtyL = dirtyT = 0;
    dirtyR = dirtyB = -1;
}

Boolean VidDevInit(void)
{
    return FALSE;
}

void VidDevExit(void)
{
    if (hostOpen)
	ghal_display_close();
    hostOpen = FALSE;
    dirtyR = dirtyB = -1;
}

/* "...: WxH" or "WxH" */
static void ParseSize(const char *name, unsigned *w, unsigned *h)
{
    const char *p;
    unsigned a = 0, b = 0;

    *w = DEFAULT_WIDTH;
    *h = DEFAULT_HEIGHT;
    if (!name)
	return;
    for (p = name; *p && *p != ':'; p++)
	;
    p = *p ? p + 1 : name;
    while (*p == ' ')
	p++;
    if (*p < '0' || *p > '9')
	return;
    while (*p >= '0' && *p <= '9')
	a = a * 10 + (unsigned) (*p++ - '0');
    if (*p != 'x' && *p != 'X')
	return;
    p++;
    if (*p < '0' || *p > '9')
	return;
    while (*p >= '0' && *p <= '9')
	b = b * 10 + (unsigned) (*p++ - '0');
    if (a >= 16 && b >= 16 && a <= 16384 && b <= 16384) {
	*w = a;
	*h = b;
    }
}

word VidDevTest(const char *name)
{
    (void) name;
    return DP_PRESENT;
}

Boolean VidDevSet(const char *name)
{
    unsigned w, h;

    VidDevExit();
    ParseSize(name, &w, &h);
    if (ghal_display_open(w, h, 0, &hostSurf) != GHAL_OK)
	return TRUE;
    hostOpen = TRUE;
    DriverTable.VDI_pageW = hostSurf.width;
    DriverTable.VDI_pageH = hostSurf.height;
    DriverTable.VDI_bpScan = (word) (hostSurf.width * 3);
    HostPtrReset();
    HostDirty(0, 0, (sword) (hostSurf.width - 1),
	      (sword) (hostSurf.height - 1));
    return FALSE;
}

void VidDevScreen(Boolean on)
{
    if (on && !screenOn && hostOpen)
	HostDirty(0, 0, (sword) (hostSurf.width - 1),
		  (sword) (hostSurf.height - 1));
    screenOn = on;
}

static dword Mix(dword d, dword c, MixMode m);

void VidDevPutPixel(sword x, sword y, dword color, MixMode mix)
{
    uint32_t *p;

    if (!hostOpen || x < 0 || y < 0 || x >= (sword) hostSurf.width ||
	y >= (sword) hostSurf.height)
	return;
    p = HostPixel(x, y);
    *p = Mix(*p, color, mix);
    HostDirty(x, y, x, y);
}

void VidDevCopySpan(sword sx, sword sy, sword dx, sword dy, sword n)
{
    sword w = (sword) hostSurf.width, h = (sword) hostSurf.height;

    if (!hostOpen || sy < 0 || dy < 0 || sy >= h || dy >= h)
	return;
    /* clip both to the screen */
    if (sx < 0) { n = (sword) (n + sx); dx = (sword) (dx - sx); sx = 0; }
    if (dx < 0) { n = (sword) (n + dx); sx = (sword) (sx - dx); dx = 0; }
    if (sx + n > w)
	n = (sword) (w - sx);
    if (dx + n > w)
	n = (sword) (w - dx);
    if (n <= 0)
	return;
    memmove(HostPixel(dx, dy), HostPixel(sx, sy), (size_t) n * 4);
    HostDirty(dx, dy, (sword) (dx + n - 1), dy);
}

static dword Mix(dword d, dword c, MixMode m)
{
    switch (m) {
    case MM_CLEAR:	return 0;
    case MM_COPY:	return c;
    case MM_NOP:	return d;
    case MM_AND:	return d & c;
    case MM_INVERT:	return ~d & 0xffffff;
    case MM_XOR:	return d ^ c;
    case MM_SET:	return 0xffffff;
    case MM_OR:		return d | c;
    default:		return c;
    }
}

void VidDevFillRect(sword left, sword top, sword right, sword bottom,
		    const VidDrawState *ds)
{
    sword x, y;
    uint32_t *p;
    byte m;

    if (!hostOpen)
	return;
    if (left < 0)
	left = 0;
    if (top < 0)
	top = 0;
    if (right >= (sword) hostSurf.width)
	right = (sword) (hostSurf.width - 1);
    if (bottom >= (sword) hostSurf.height)
	bottom = (sword) (hostSurf.height - 1);
    if (left > right || top > bottom)
	return;
    for (y = top; y <= bottom; y++) {
	p = HostPixel(left, y);
	m = ds->VDS_mask ? ds->VDS_mask[y & 7] : 0xff;
	if (m == 0xff && ds->VDS_mix == MM_COPY)
	    for (x = left; x <= right; x++)
		*p++ = ds->VDS_color;
	else
	    for (x = left; x <= right; x++, p++)
		if (m & (0x80 >> (x & 7)))
		    *p = Mix(*p, ds->VDS_color, ds->VDS_mix);
    }
    HostDirty(left, top, right, bottom);
}

dword VidDevGetPixel(sword x, sword y)
{
    if (!hostOpen || x < 0 || y < 0 || x >= (sword) hostSurf.width ||
	y >= (sword) hostSurf.height)
	return 0;
    return *HostPixel(x, y) & 0xffffff;
}
