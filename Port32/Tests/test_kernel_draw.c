/*
 * test_kernel_draw.c -- drawing through the window system to the C
 * host video driver (Library/Kernel/Graphics: graphicsOutput.c,
 * graphicsLine.c, graphicsUtils.c; Library/Kernel/Win).
 *
 * The test boots, loads hostvid.geo, sets a 320x200 device, opens a
 * root window on it and windows on the root, and reads the pixels back
 * from the display (ghal_display_read_back):
 *   - the windows' backgrounds (the washes when they open);
 *   - GrFillRect with the imaging conventions (right and bottom not
 *     filled), clipped to the window, in front windows' areas not drawn;
 *   - a translated and a scaled GState, a rotated rectangle (polygon);
 *   - GrDrawRect, GrDrawLine with butt and square ends, horizontal and
 *     vertical lines, GrDrawLineTo from the pen, GrDrawPoint;
 *   - an update: only the update region drawn;
 *   - a window moved away: what it covered washed with the root's
 *     background.
 */
#include "kernelC.h"
#include "ghal.h"
#include "Win/winInt.h"
#include <file.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern const unsigned char hostvid_geode[];
extern const unsigned long hostvid_geode_size;

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static char top[260], sep = '/';

#define GREY	0x808080UL
#define WHITE	0xffffffUL
#define RED	0xff0000UL
#define GREEN	0x00ff00UL
#define BLUE	0x0000ffUL
#define BLACK	0x000000UL

static void Put(const char *rel, const void *data, unsigned long size)
{
    char path[300], *p;
    ghal_file *f;
    size_t done = 0;

    sprintf(path, "%s%c%s", top, sep, rel);
    for (p = path; *p; p++)
	if (*p == '/')
	    *p = sep;
    if (!size) {
	ghal_fs_mkdir(path);
	return;
    }
    CHECK(ghal_fs_open(path, GHAL_FS_WRITE | GHAL_FS_CREATE |
				 GHAL_FS_TRUNCATE, &f) == GHAL_OK);
    ghal_fs_write(f, data, size, &done);
    CHECK(done == size);
    ghal_fs_close(f);
}

static unsigned long Pixel(int x, int y)
{
    return ghal_display_read_back(x, y) & 0xffffff;
}

static void SetCol(GStateHandle g, unsigned long rgb)
{
    GrSetAreaColor(g, CF_RGB, (word) (rgb >> 16), (word) ((rgb >> 8) & 0xff),
		   (word) (rgb & 0xff));
    GrSetLineColor(g, CF_RGB, (word) (rgb >> 16), (word) ((rgb >> 8) & 0xff),
		   (word) (rgb & 0xff));
}

static WindowHandle Open(Handle parent, word flags, unsigned long rgb,
			 sword l, sword t, sword r, sword b)
{
    return WinOpen(parent, 0, 0, WCF_RGB | WCF_PLAIN, (word) (rgb >> 16),
		   (word) ((rgb >> 8) & 0xff), (word) (rgb & 0xff), flags, 0,
		   0, NULL, (word) l, (word) t, (word) r, (word) b);
}

int main(int argc, char **argv)
{
    char *slash;
    GeodeLoadError e;
    GeodeHandle drv;
    DriverInfoStruct *dis;
    DriverRegs r;
    WindowHandle root, a, b;
    GStateHandle g;
    int i, bad;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    strncpy(top, argv[0], sizeof(top) - 8);
    top[sizeof(top) - 8] = 0;
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash) {
	sep = *slash;
	strcpy(slash + 1, "GDRTOP");
    } else
	strcpy(top, "GDRTOP");
    ghal_fs_mkdir(top);
    Put("SYSTEM", NULL, 0);
    Put("SYSTEM/VIDEO", NULL, 0);
    Put("SYSTEM/VIDEO/hostvid.geo", hostvid_geode, hostvid_geode_size);
    CHECK(KernelBoot(top, "", &e) == 0 && e == GLE_OK);
    FileSetStandardPath(SP_VIDEO_DRIVERS);
    drv = GeodeUseDriver("hostvid.geo", VIDEO_PROTO_MAJOR,
			 VIDEO_PROTO_MINOR, &e);
    CHECK(drv != 0);
    if (!drv)
	return 1;
    dis = GeodeInfoDriver(drv);
    memset(&r, 0, sizeof(r));
    r.DR_dsSi = "Host Display: 320x200";
    CHECK(!DriverCall(dis, DRE_SET_DEVICE, &r));

    /* the root: grey; A: white at 20,20 - 119,99; B: blue at 100,60 -
     * 179,139 in front of A */
    root = Open(drv, WPF_ROOT, GREY, 0, 0, 319, 199);
    CHECK(Pixel(0, 0) == GREY && Pixel(319, 199) == GREY);
    a = Open(root, 0, WHITE, 20, 20, 119, 99);
    CHECK(Pixel(20, 20) == WHITE && Pixel(119, 99) == WHITE &&
	  Pixel(19, 20) == GREY && Pixel(120, 99) == GREY);
    b = Open(root, 0, BLUE, 100, 60, 179, 139);
    CHECK(Pixel(110, 70) == BLUE && Pixel(99, 70) == WHITE);

    /* rectangles in A: window coordinates, right and bottom not
     * filled, clipped to what of A is visible */
    g = GrCreateState(a);
    SetCol(g, RED);
    GrFillRect(g, 10, 10, 20, 15);
    CHECK(Pixel(30, 30) == RED && Pixel(39, 34) == RED &&
	  Pixel(40, 34) == WHITE && Pixel(39, 35) == WHITE &&
	  Pixel(29, 30) == WHITE);
    GrFillRect(g, 50, 30, 200, 300);	/* past A, under B */
    CHECK(Pixel(70, 50) == RED && Pixel(99, 99) == RED &&
	  Pixel(119, 50) == RED && Pixel(120, 50) == GREY &&
	  Pixel(110, 70) == BLUE && Pixel(70, 100) == GREY);
    GrFillRect(g, 5, 5, 5, 5);		/* a point stays a point */
    CHECK(Pixel(25, 25) == RED && Pixel(26, 25) == WHITE);

    /* translated by 10, 5 and scaled by 2 */
    SetCol(g, GREEN);
    GrApplyTranslation(g, 10L << 16, 5L << 16);
    GrApplyScale(g, 2L << 16, 2L << 16);
    GrFillRect(g, 0, 0, 3, 2);		/* scaled, then moved: 30,25 - 35,28 */
    CHECK(Pixel(30, 25) == GREEN && Pixel(35, 28) == GREEN &&
	  Pixel(36, 28) == WHITE && Pixel(35, 29) == WHITE);
    GrSetNullTransform(g);

    /* rotated by 90 degrees: a polygon; first one wholly above A (all
     * clipped), then one inside */
    SetCol(g, BLACK);
    GrApplyTranslation(g, 80L << 16, 0);
    GrApplyRotation(g, 90L << 16);
    GrFillRect(g, 10, 10, 20, 30);	/* x' = 80 + y, y' = -x */
    GrSetNullTransform(g);
    GrApplyTranslation(g, 10L << 16, 80L << 16);
    GrApplyRotation(g, 90L << 16);
    GrFillRect(g, 10, 10, 20, 30);	/* x' = 10 + y, y' = 80 - x */
    GrSetNullTransform(g);
    CHECK(Pixel(20 + 20, 20 + 65) == BLACK &&
	  Pixel(20 + 35, 20 + 65) == BLACK &&
	  Pixel(20 + 45, 20 + 65) != BLACK);

    /* frames and lines */
    SetCol(g, BLUE);
    GrFillRect(g, 0, 0, 100, 100);	/* clear A to blue */
    SetCol(g, WHITE);
    GrDrawRect(g, 2, 2, 10, 8);		/* square ends: all corners */
    CHECK(Pixel(22, 22) == WHITE && Pixel(30, 22) == WHITE &&
	  Pixel(22, 28) == WHITE && Pixel(30, 28) == WHITE &&
	  Pixel(26, 25) == BLUE && Pixel(31, 22) == BLUE);
    GrDrawLine(g, 40, 10, 50, 10);	/* butt: the last pixel left out */
    CHECK(Pixel(60, 30) == WHITE && Pixel(69, 30) == WHITE &&
	  Pixel(70, 30) == BLUE);
    GrSetLineEnd(g, LE_SQUARECAP);
    GrDrawLine(g, 40, 12, 50, 12);
    CHECK(Pixel(70, 32) == WHITE);
    GrDrawVLine(g, 60, 5, 15);
    CHECK(Pixel(80, 25) == WHITE && Pixel(80, 35) == WHITE &&
	  Pixel(80, 36) == BLUE);
    GrDrawLine(g, 10, 20, 30, 40);	/* 45 degrees */
    for (i = 0, bad = 0; i <= 20; i++)
	if (Pixel(30 + i, 40 + i) != WHITE)
	    bad++;
    CHECK(bad == 0 && Pixel(31, 40) == BLUE);
    GrMoveTo(g, 10, 50);
    GrDrawLineTo(g, 20, 50);
    CHECK(Pixel(30, 70) == WHITE && Pixel(40, 70) == WHITE &&
	  GrGetCurPos(g) == ((50UL << 16) | 20));
    GrDrawHLineTo(g, 25);
    CHECK(Pixel(45, 70) == WHITE);
    GrDrawPoint(g, 70, 70);
    CHECK(Pixel(90, 90) == WHITE && Pixel(91, 90) == BLUE &&
	  Pixel(90, 91) == BLUE);

    /* an update: only the invalid area is drawn */
    WinInvalReg(a, NULL, 0, 0, 9, 9);	/* plain: washed white, valid */
    CHECK(Pixel(20, 20) == WHITE && Pixel(29, 29) == WHITE &&
	  Pixel(30, 30) == BLUE);
    WinSetInfo(a, WIT_COLOR, (optr) (WCF_RGB << 8) | 0xff |
	       (0xffUL << 16) | (0xffUL << 24));
    {
	Window *w = MemLock(a);
	w->W_color &= (WinColorFlags) ~WCF_PLAIN;	/* exposures now */
	MemUnlock(a);
    }
    WinInvalReg(a, NULL, 0, 0, 9, 9);
    GrBeginUpdate(g);
    SetCol(g, GREEN);
    GrFillRect(g, 0, 0, 50, 50);
    GrEndUpdate(g);
    CHECK(Pixel(20, 20) == GREEN && Pixel(29, 29) == GREEN &&
	  Pixel(30, 30) == BLUE && Pixel(40, 40) != GREEN);

    /* B moved away: A gets back what B covered, washed white; the root
     * where B was, grey */
    WinMove(b, 100, 0, 0);
    CHECK(Pixel(110, 70) == WHITE && Pixel(150, 120) == GREY &&
	  Pixel(210, 70) == BLUE);

    GrDestroyState(g);
    WinClose(b);
    WinClose(a);
    CHECK(Pixel(50, 50) == GREY && Pixel(210, 70) == GREY);
    WinClose(root);

    printf("%s (%d failed)\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
