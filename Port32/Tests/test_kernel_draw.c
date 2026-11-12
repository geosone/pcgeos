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
 *   - filled polygons by both rules, GrTestPointInPolygon, polylines,
 *     polygon outlines, a brushed polyline;
 *   - ellipses filled and outlined, rotated, fat, and round line ends;
 *   - arcs: a pie, open and chord outlines, 3-point arcs, an arc from
 *     the pen;
 *   - rounded rectangles filled, drawn and from the pen;
 *   - a pie through a mirroring matrix;
 *   - regions, with a parameter; the window's and the mask's bounds,
 *     GrTestRectInMask, GrInvalRect (rotated too), GrInvalRectDWord;
 *   - bitmaps: monochrome drawn and filled, 24 bits, PackBits, a
 *     complex one with a palette in two slices; scaled twice, half,
 *     mirrored in x and y, and at 144 dpi;
 *   - fat lines: butt and square ends, mitered joins;
 *   - dashes on a line and along a polyline;
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

static Bitmap *nextSlice;

/* a bitmap callback: the next slice once */
static Bitmap *NextSlice(Bitmap *bm)
{
    Bitmap *n = nextSlice;

    (void) bm;
    nextSlice = NULL;
    return n;
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

    /* polygons: the outline's pixels and the inside */
    SetCol(g, BLUE);
    GrFillRect(g, 0, 0, 100, 100);
    SetCol(g, RED);
    {
	static Point tri[] = { {0, 0}, {20, 0}, {0, 20} };
	static Point sq[] = { {30, 0}, {40, 0}, {40, 10}, {30, 10} };
	static Point star[] = { {20, 30}, {32, 68}, {1, 44}, {39, 44},
				{8, 68} };

	GrFillPolygon(g, ODD_EVEN, tri, 3);
	CHECK(Pixel(20, 20) == RED && Pixel(40, 20) == RED &&
	      Pixel(20, 40) == RED && Pixel(25, 25) == RED &&
	      Pixel(30, 30) == RED && Pixel(41, 20) == BLUE &&
	      Pixel(35, 35) == BLUE);
	GrFillPolygon(g, ODD_EVEN, sq, 4);	/* both ends filled */
	CHECK(Pixel(50, 20) == RED && Pixel(60, 30) == RED &&
	      Pixel(61, 30) == BLUE && Pixel(60, 31) == BLUE &&
	      Pixel(49, 25) == BLUE);
	GrFillPolygon(g, ODD_EVEN, star, 5);	/* the middle is out */
	CHECK(Pixel(20 + 20, 20 + 52) == BLUE &&
	      Pixel(20 + 20, 20 + 34) == RED &&
	      Pixel(20 + 5, 20 + 45) == RED);
	CHECK(!GrTestPointInPolygon(g, ODD_EVEN, star, 5, 20, 52) &&
	      GrTestPointInPolygon(g, WINDING, star, 5, 20, 52) &&
	      GrTestPointInPolygon(g, ODD_EVEN, star, 5, 20, 34) &&
	      !GrTestPointInPolygon(g, WINDING, star, 5, 60, 52));
	GrFillPolygon(g, WINDING, star, 5);	/* the middle is in */
	CHECK(Pixel(20 + 20, 20 + 52) == RED);

	/* outlines: open and closed */
	SetCol(g, BLUE);
	GrFillRect(g, 0, 0, 100, 100);
	SetCol(g, WHITE);
	GrDrawPolyline(g, sq, 4);
	CHECK(Pixel(50, 20) == WHITE && Pixel(60, 25) == WHITE &&
	      Pixel(55, 30) == WHITE && Pixel(50, 25) == BLUE &&
	      Pixel(55, 25) == BLUE &&
	      GrGetCurPos(g) == ((10UL << 16) | 30));
	GrDrawPolygon(g, tri, 3);
	CHECK(Pixel(30, 20) == WHITE && Pixel(20, 30) == WHITE &&
	      Pixel(30, 30) == WHITE && Pixel(25, 25) == BLUE &&
	      GrGetCurPos(g) == 0);
	GrBrushPolyline(g, star, 2, 3, 2);	/* 3 wide, 2 high */
	CHECK(Pixel(40, 50) == WHITE && Pixel(42, 51) == WHITE &&
	      Pixel(43, 50) == BLUE && Pixel(40, 49) == BLUE &&
	      Pixel(39, 50) == BLUE);
    }

    /* fat lines */
    SetCol(g, BLUE);
    GrFillRect(g, 0, 0, 100, 100);
    SetCol(g, WHITE);
    GrSetLineWidth(g, 5L << 16);
    GrSetLineEnd(g, LE_BUTTCAP);
    GrDrawLine(g, 10, 10, 40, 10);	/* 5 high, both ends in */
    CHECK(Pixel(30, 28) == WHITE && Pixel(60, 32) == WHITE &&
	  Pixel(29, 30) == BLUE && Pixel(61, 30) == BLUE &&
	  Pixel(45, 27) == BLUE && Pixel(45, 33) == BLUE);
    {
	static Point zig[] = { {10, 30}, {40, 30}, {25, 60}, {60, 45} };

	GrSetLineJoin(g, LJ_MITERED);
	GrDrawPolyline(g, zig, 4);
	CHECK(Pixel(30, 48) == WHITE && Pixel(30, 52) == WHITE);
	CHECK(Pixel(30, 47) == BLUE && Pixel(30, 53) == BLUE);
	CHECK(Pixel(41, 84) == WHITE && Pixel(41, 85) == BLUE &&
	      Pixel(40, 84) == BLUE);	/* the mitered tip */
	GrSetLineEnd(g, LE_SQUARECAP);
	GrSetLineJoin(g, LJ_BEVELED);
	GrDrawLine(g, 60, 5, 80, 25);	/* square ends: further */
	CHECK(Pixel(100, 45) == WHITE && Pixel(99, 46) == WHITE &&
	      Pixel(80, 22) == WHITE && Pixel(84, 34) == BLUE);
	GrSetLineWidth(g, 1L << 16);
	GrSetLineEnd(g, LE_BUTTCAP);
    }

    /* dashes: a line through the driver, a polyline cut here */
    SetCol(g, BLUE);
    GrFillRect(g, 0, 0, 100, 100);
    SetCol(g, WHITE);
    GrSetLineStyle(g, LS_DASHED, 0, NULL, 0);
    GrDrawLine(g, 10, 50, 50, 50);
    CHECK(Pixel(30, 70) == WHITE && Pixel(33, 70) == WHITE &&
	  Pixel(34, 70) == BLUE && Pixel(37, 70) == BLUE &&
	  Pixel(38, 70) == WHITE);
    {
	static Point bend[] = { {10, 70}, {40, 70}, {40, 78} };

	GrDrawPolyline(g, bend, 3);
	CHECK(Pixel(30, 90) == WHITE && Pixel(33, 90) == WHITE &&
	      Pixel(34, 90) == BLUE && Pixel(37, 90) == BLUE &&
	      Pixel(38, 90) == WHITE && Pixel(60, 90) == BLUE &&
	      Pixel(60, 91) == BLUE && Pixel(60, 92) == WHITE &&
	      Pixel(60, 95) == WHITE && Pixel(60, 96) == BLUE);
    }
    GrSetLineStyle(g, LS_SOLID, 0, NULL, 0);

    /* ellipses */
    SetCol(g, BLUE);
    GrFillRect(g, 0, 0, 100, 100);
    SetCol(g, WHITE);
    GrFillEllipse(g, 10, 10, 30, 24);
    GrDrawEllipse(g, 40, 10, 60, 24);
    GrFillEllipse(g, 10, 40, 13, 50);
    GrSetLineWidth(g, 5L << 16);
    GrSetLineEnd(g, LE_ROUNDCAP);
    GrDrawLine(g, 50, 50, 70, 50);
    GrSetLineWidth(g, 1L << 16);
    GrSetLineEnd(g, LE_BUTTCAP);
    CHECK(Pixel(40, 37) == WHITE && Pixel(30, 37) == WHITE &&
	  Pixel(29, 37) == BLUE && Pixel(50, 37) == WHITE &&
	  Pixel(51, 37) == BLUE && Pixel(38, 30) == WHITE &&
	  Pixel(37, 30) == BLUE && Pixel(40, 29) == BLUE &&
	  Pixel(40, 44) == WHITE && Pixel(40, 45) == BLUE &&
	  Pixel(31, 31) == BLUE);
    CHECK(Pixel(70, 37) == BLUE && Pixel(60, 37) == WHITE &&
	  Pixel(61, 37) == BLUE && Pixel(80, 37) == WHITE &&
	  Pixel(70, 30) == WHITE && Pixel(70, 44) == WHITE);
    CHECK(Pixel(68, 70) == WHITE && Pixel(67, 70) == BLUE &&	/* round */
	  Pixel(92, 70) == WHITE && Pixel(93, 70) == BLUE);

    /* rotated by 45 degrees, filled and a fat outline */
    SetCol(g, BLUE);
    GrFillRect(g, 0, 0, 100, 100);
    SetCol(g, WHITE);
    GrApplyTranslation(g, 25L << 16, 40L << 16);
    GrApplyRotation(g, 45L << 16);
    GrFillEllipse(g, -20, -8, 20, 8);
    GrSetNullTransform(g);
    GrSetLineWidth(g, 3L << 16);
    GrDrawEllipse(g, 55, 20, 90, 60);
    GrSetLineWidth(g, 1L << 16);
    CHECK(Pixel(45, 60) == WHITE && Pixel(58, 47) == WHITE &&
	  Pixel(53, 68) == BLUE && Pixel(37, 52) == BLUE);
    CHECK(Pixel(92, 39) == WHITE && Pixel(92, 41) == WHITE &&
	  Pixel(92, 38) == BLUE && Pixel(92, 42) == BLUE &&
	  Pixel(75, 60) == WHITE && Pixel(73, 60) == BLUE &&
	  Pixel(77, 60) == BLUE);

    /* arcs */
    SetCol(g, BLUE);
    GrFillRect(g, 0, 0, 100, 100);
    SetCol(g, WHITE);
    GrFillArc(g, 5, 5, 35, 35, 0, 90, ACT_PIE);
    GrDrawArc(g, 40, 5, 70, 35, 0, 180, ACT_OPEN);
    GrDrawArc(g, 40, 15, 70, 45, 180, 270, ACT_CHORD);
    {
	ThreePointArcParams tp;
	memset(&tp, 0, sizeof(tp));
	tp.TPAP_close = ACT_PIE;
	tp.TPAP_point1.PF_x.WWF_int = 5;  tp.TPAP_point1.PF_y.WWF_int = 55;
	tp.TPAP_point2.PF_x.WWF_int = 20; tp.TPAP_point2.PF_y.WWF_int = 40;
	tp.TPAP_point3.PF_x.WWF_int = 35; tp.TPAP_point3.PF_y.WWF_int = 55;
	GrFillArc3Point(g, &tp);
	tp.TPAP_close = ACT_OPEN;
	tp.TPAP_point1.PF_x.WWF_int = 45; tp.TPAP_point1.PF_y.WWF_int = 55;
	tp.TPAP_point2.PF_x.WWF_int = 60; tp.TPAP_point2.PF_y.WWF_int = 70;
	tp.TPAP_point3.PF_x.WWF_int = 75; tp.TPAP_point3.PF_y.WWF_int = 55;
	GrDrawArc3Point(g, &tp);
    }
    CHECK(GrGetCurPos(g) == ((55UL << 16) | 45));	/* point 1, open */
    {
	ThreePointArcToParams tt;
	memset(&tt, 0, sizeof(tt));
	tt.TPATP_close = ACT_OPEN;
	tt.TPATP_point2.PF_x.WWF_int = 90; tt.TPATP_point2.PF_y.WWF_int = 20;
	tt.TPATP_point3.PF_x.WWF_int = 80; tt.TPATP_point3.PF_y.WWF_int = 30;
	GrMoveTo(g, 80, 10);
	GrDrawArc3PointTo(g, &tt);
	CHECK(GrGetCurPos(g) == ((30UL << 16) | 80));
    }
    CHECK(Pixel(45, 35) == WHITE && Pixel(40, 25) == WHITE &&	/* pie */
	  Pixel(39, 35) == BLUE && Pixel(45, 41) == BLUE &&
	  Pixel(54, 27) == BLUE);
    CHECK(Pixel(75, 25) == WHITE && Pixel(60, 40) == WHITE &&	/* open */
	  Pixel(90, 40) == WHITE && Pixel(75, 30) == BLUE &&
	  Pixel(75, 40) == BLUE);
    CHECK(Pixel(70, 60) == WHITE && Pixel(64, 60) == WHITE &&	/* chord */
	  Pixel(67, 60) == BLUE);
    CHECK(Pixel(40, 70) == WHITE && Pixel(25, 75) == WHITE &&	/* 3 points */
	  Pixel(40, 59) == BLUE && Pixel(40, 76) == BLUE);
    CHECK(Pixel(80, 90) == WHITE && Pixel(65, 75) == WHITE &&
	  Pixel(95, 75) == WHITE && Pixel(80, 85) == BLUE &&
	  Pixel(80, 75) == BLUE);
    CHECK(Pixel(110, 40) == WHITE && Pixel(100, 30) == WHITE &&	/* to */
	  Pixel(100, 40) == BLUE && Pixel(91, 40) == BLUE);

    /* rounded rectangles */
    SetCol(g, BLUE);
    GrFillRect(g, 0, 0, 100, 100);
    SetCol(g, WHITE);
    GrFillRoundRect(g, 5, 5, 45, 35, 8);
    GrDrawRoundRect(g, 50, 5, 90, 35, 10);
    GrMoveTo(g, 5, 40);
    GrDrawRoundRectTo(g, 30, 75, 100);	/* the radius cut to 12 */
    CHECK(GrGetCurPos(g) == ((40UL << 16) | 5));	/* the pen stays */
    CHECK(Pixel(45, 40) == WHITE && Pixel(25, 40) == WHITE &&	/* filled */
	  Pixel(65, 40) == WHITE && Pixel(66, 40) == BLUE &&
	  Pixel(25, 25) == BLUE && Pixel(26, 26) == BLUE &&
	  Pixel(45, 25) == WHITE && Pixel(45, 24) == BLUE &&
	  Pixel(45, 55) == WHITE && Pixel(45, 56) == BLUE);
    CHECK(Pixel(90, 25) == WHITE && Pixel(70, 40) == WHITE &&	/* drawn */
	  Pixel(110, 40) == WHITE && Pixel(73, 28) == WHITE &&
	  Pixel(90, 40) == BLUE && Pixel(70, 26) == BLUE);
    CHECK(Pixel(37, 60) == WHITE && Pixel(25, 80) == WHITE &&	/* to */
	  Pixel(26, 80) == BLUE && Pixel(25, 61) == BLUE);

    /* mirrored: the arc's ends trade places, the same quarter is filled */
    SetCol(g, BLUE);
    GrFillRect(g, 0, 0, 100, 100);
    SetCol(g, WHITE);
    GrApplyTranslation(g, 40L << 16, 30L << 16);
    GrApplyScale(g, 1L << 16, -(1L << 16));
    GrFillArc(g, -10, -10, 10, 10, 0, 90, ACT_PIE);
    GrSetNullTransform(g);
    CHECK(Pixel(65, 55) == WHITE && Pixel(65, 45) == BLUE &&
	  Pixel(55, 55) == BLUE && Pixel(55, 45) == BLUE);

    /* regions: two bands, and one with a parameter for its right edge */
    SetCol(g, BLUE);
    GrFillRect(g, 0, 0, 100, 100);
    SetCol(g, WHITE);
    {
	static const word bands[] = {
	    0, 0, 9, 9, (word) -1, EOREGREC,
	    4, 0, 9, EOREGREC,
	    9, 0, 2, 7, 9, EOREGREC,
	    EOREGREC
	};
	static const word param[] = {
	    0, 0, 0x9000, 4, (word) -1, EOREGREC,
	    4, 0, 0x9000, EOREGREC,
	    EOREGREC
	};
	GrDrawRegion(g, 50, 10, (const Region *) bands, 0, 0);
	CHECK(Pixel(70, 30) == WHITE && Pixel(79, 34) == WHITE &&
	      Pixel(80, 34) == BLUE && Pixel(72, 39) == WHITE &&
	      Pixel(73, 39) == BLUE && Pixel(77, 39) == WHITE &&
	      Pixel(70, 40) == BLUE && Pixel(69, 30) == BLUE);
	GrMoveTo(g, 10, 50);
	GrDrawRegionAtCP(g, (const Region *) param, 0, 0, 19, 0);
	CHECK(Pixel(30, 70) == WHITE && Pixel(49, 74) == WHITE &&
	      Pixel(50, 70) == BLUE && Pixel(30, 75) == BLUE);
    }

    /* the window's bounds, the mask, invalidation */
    {
	Rectangle rb;
	RectDWord rd;

	CHECK(GrGetWinHandle(g) == a);
	CHECK(GrGetWinBounds(g, &rb) && rb.R_left == 0 && rb.R_top == 0 &&
	      rb.R_right == 100 && rb.R_bottom == 80);
	GrApplyTranslation(g, 10L << 16, 5L << 16);
	GrGetWinBoundsDWord(g, &rd);
	CHECK(rd.RD_left == -10 && rd.RD_top == -5 && rd.RD_right == 90 &&
	      rd.RD_bottom == 75);
	GrSetNullTransform(g);
	CHECK(GrGetMaskBounds(g, &rb) && rb.R_left == 0 && rb.R_top == 0 &&
	      rb.R_right == 100 && rb.R_bottom == 80);
	CHECK(GrTestRectInMask(g, 10, 10, 20, 20) == TRRT_IN &&
	      GrTestRectInMask(g, 70, 30, 90, 50) == TRRT_PARTIAL &&
	      GrTestRectInMask(g, 200, 200, 210, 210) == TRRT_OUT);

	/* A is plain: what is invalidated is washed white at once */
	SetCol(g, BLUE);
	GrFillRect(g, 0, 0, 100, 100);
	GrInvalRect(g, 10, 10, 19, 19);
	CHECK(Pixel(30, 30) == WHITE && Pixel(39, 39) == WHITE &&
	      Pixel(40, 40) == BLUE && Pixel(29, 30) == BLUE);
	rd.RD_left = 60; rd.RD_top = 10; rd.RD_right = 69; rd.RD_bottom = 19;
	GrInvalRectDWord(g, &rd);
	CHECK(Pixel(80, 30) == WHITE && Pixel(89, 39) == WHITE &&
	      Pixel(90, 39) == BLUE);
	GrApplyTranslation(g, 50L << 16, 40L << 16);
	GrApplyRotation(g, 45L << 16);
	GrInvalRect(g, -5, -5, 5, 5);		/* a diamond */
	GrSetNullTransform(g);
	CHECK(Pixel(70, 60) == WHITE && Pixel(76, 60) == WHITE &&
	      Pixel(70, 54) == WHITE && Pixel(75, 65) == BLUE &&
	      Pixel(65, 55) == BLUE);
    }

    /* bitmaps */
    SetCol(g, BLUE);
    GrFillRect(g, 0, 0, 100, 100);
    {
	static union { Bitmap b; byte raw[32]; } mono;
	static union { Bitmap b; byte raw[32]; } rgb;
	static union { Bitmap b; byte raw[32]; } packed;
	static union { CBitmap c; byte raw[64]; } pal, pal2;
	static const byte monoData[] = { 0xf0, 0x0f, 0x80, 0x01 };
	static const byte packData[] = { 0x01, 0xf0, 0x0f, 0xff, 0xff };
	byte *p;

	CHECK(sizeof(Bitmap) == 6 && sizeof(CBitmap) == 20);
	mono.b.B_width = 16; mono.b.B_height = 2;
	mono.b.B_compact = BMC_UNCOMPACTED; mono.b.B_type = BMF_MONO;
	memcpy(mono.raw + 6, monoData, 4);
	GrDrawBitmap(g, 10, 10, &mono.b, NULL);	/* 1 black, 0 white */
	CHECK(Pixel(30, 30) == BLACK && Pixel(33, 30) == BLACK &&
	      Pixel(34, 30) == WHITE && Pixel(41, 30) == WHITE &&
	      Pixel(42, 30) == BLACK && Pixel(45, 30) == BLACK &&
	      Pixel(46, 30) == BLUE && Pixel(30, 31) == BLACK &&
	      Pixel(31, 31) == WHITE && Pixel(45, 31) == BLACK &&
	      Pixel(30, 32) == BLUE);
	SetCol(g, GREEN);
	GrFillBitmap(g, 10, 20, &mono.b, NULL);	/* 1 green, 0 left */
	CHECK(Pixel(30, 40) == GREEN && Pixel(34, 40) == BLUE &&
	      Pixel(42, 40) == GREEN && Pixel(31, 41) == BLUE);

	rgb.b.B_width = 2; rgb.b.B_height = 1;
	rgb.b.B_compact = BMC_UNCOMPACTED; rgb.b.B_type = BMF_24BIT;
	p = rgb.raw + 6;
	p[0] = 0xff; p[1] = 0; p[2] = 0; p[3] = 0; p[4] = 0xff; p[5] = 0;
	GrMoveTo(g, 50, 10);
	GrDrawBitmapAtCP(g, &rgb.b, NULL);
	CHECK(Pixel(70, 30) == RED && Pixel(71, 30) == GREEN &&
	      Pixel(72, 30) == BLUE);

	packed.b.B_width = 16; packed.b.B_height = 2;
	packed.b.B_compact = BMC_PACKBITS; packed.b.B_type = BMF_MONO;
	memcpy(packed.raw + 6, packData, sizeof(packData));
	GrDrawBitmap(g, 10, 30, &packed.b, NULL);
	CHECK(Pixel(30, 50) == BLACK && Pixel(34, 50) == WHITE &&
	      Pixel(42, 50) == BLACK && Pixel(31, 51) == BLACK &&
	      Pixel(38, 51) == BLACK && Pixel(30, 52) == BLUE);

	/* complex, 4 bits with a palette, in two slices */
	pal.c.CB_simple.B_width = 2; pal.c.CB_simple.B_height = 2;
	pal.c.CB_simple.B_compact = BMC_UNCOMPACTED;
	pal.c.CB_simple.B_type = BMF_4BIT | BMT_COMPLEX | BMT_PALETTE;
	pal.c.CB_startScan = 0; pal.c.CB_numScans = 1;
	pal.c.CB_palette = 20; pal.c.CB_data = 28;
	pal.c.CB_xres = pal.c.CB_yres = 72;
	p = pal.raw + 20;
	p[0] = 2; p[1] = 0;			/* two colors: red, green */
	p[2] = 0xff; p[3] = 0; p[4] = 0; p[5] = 0; p[6] = 0xff; p[7] = 0;
	pal.raw[28] = 0x01;			/* red, green */
	pal2 = pal;
	pal2.c.CB_startScan = 1;
	pal2.raw[28] = 0x10;			/* green, red */
	nextSlice = &pal2.c.CB_simple;
	GrDrawBitmap(g, 60, 30, &pal.c.CB_simple, NextSlice);
	CHECK(Pixel(80, 50) == RED && Pixel(81, 50) == GREEN &&
	      Pixel(80, 51) == GREEN && Pixel(81, 51) == RED &&
	      Pixel(80, 52) == BLUE);

	/* scaled: twice, half, mirrored in x and in y */
	GrApplyScale(g, 2L << 16, 2L << 16);
	GrDrawBitmap(g, 5, 35, &mono.b, NULL);
	GrSetNullTransform(g);
	CHECK(Pixel(30, 90) == BLACK && Pixel(37, 91) == BLACK &&
	      Pixel(38, 90) == WHITE && Pixel(53, 91) == WHITE &&
	      Pixel(54, 90) == BLACK && Pixel(61, 91) == BLACK &&
	      Pixel(62, 90) == BLUE && Pixel(31, 93) == BLACK &&
	      Pixel(32, 92) == WHITE && Pixel(60, 93) == BLACK &&
	      Pixel(30, 94) == BLUE);
	GrApplyScale(g, 1L << 15, 1L << 15);
	GrDrawBitmap(g, 20, 100, &mono.b, NULL);
	GrSetNullTransform(g);
	CHECK(Pixel(30, 70) == BLACK && Pixel(31, 70) == BLACK &&
	      Pixel(32, 70) == WHITE && Pixel(35, 70) == WHITE &&
	      Pixel(36, 70) == BLACK && Pixel(37, 70) == BLACK &&
	      Pixel(38, 70) == BLUE && Pixel(30, 71) == BLUE);
	GrApplyScale(g, -(1L << 16), 1L << 16);
	GrDrawBitmap(g, -20, 60, &rgb.b, NULL);
	GrSetNullTransform(g);
	CHECK(Pixel(37, 80) == BLUE && Pixel(38, 80) == GREEN &&
	      Pixel(39, 80) == RED && Pixel(40, 80) == BLUE);
	GrApplyScale(g, 1L << 16, -(1L << 16));
	GrDrawBitmap(g, 50, -76, &mono.b, NULL);
	GrSetNullTransform(g);
	CHECK(Pixel(70, 96) == BLACK && Pixel(74, 96) == WHITE &&
	      Pixel(71, 95) == WHITE && Pixel(85, 95) == BLACK &&
	      Pixel(70, 94) == BLUE && Pixel(70, 97) == BLUE);

	/* a complex one at 144 dpi: half the size */
	pal.c.CB_xres = pal.c.CB_yres = 144;
	pal.c.CB_simple.B_width = 4;
	pal.raw[28] = 0x00; pal.raw[29] = 0x11;	/* red red green green */
	pal.c.CB_numScans = 2; pal.c.CB_simple.B_height = 2;
	GrDrawBitmap(g, 60, 40, &pal.c.CB_simple, NULL);
	CHECK(Pixel(80, 60) == RED && Pixel(81, 60) == GREEN &&
	      Pixel(82, 60) == BLUE && Pixel(80, 61) == BLUE);
    }

    /* an update: only the invalid area is drawn */
    SetCol(g, BLUE);
    GrFillRect(g, 0, 0, 100, 100);
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
