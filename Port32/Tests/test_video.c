/*
 * test_video.c -- the C host video driver (Driver/Video: VidCom,
 * Host/) in the C kernel, on ghal_display.
 *
 * The test boots, loads hostvid.geo from SP_VIDEO_DRIVERS, sets a
 * 320x200 device and draws with a Window and a GState of its own (the
 * window system is not there yet):
 *   - nothing before DRE_SET_DEVICE; DR_VID_INFO after it;
 *   - rectangles in a rectangular mask, clipped to its bounds;
 *   - a mask region with a hole (WinValClipLine, the clip line);
 *   - a mix mode with a draw mask;
 *   - the exclusive: another GState's drawing not done, its bounds;
 *   - the pointer: shown, moved, hidden, and kept over drawing;
 *   - lines, through the mask region too;
 *   - GETBITS, PUTBITS (mono, filled, 4 and 24 bits, a mask), MOVBITS
 *     both ways over itself;
 *   - polygons (clockwise only too), regions (with a parameter),
 *     polylines with a brush, dashed lines and their pattern index,
 *     fat dashes, a bitmap's scan line along a line;
 *   - the XOR region: over drawing, following the pointer, cleared;
 *   - DR_VID_GETPIXEL, the palette, an escape, the screen off and on;
 * and reads the pixels back from the display (ghal_display_read_back).
 */
#include "kernelC.h"
#include "ghal.h"
#include <geosdriver.h>
#include <Internal/videoDr.h>
#include <Internal/window.h>
#include <Internal/gstate.h>

#include <stddef.h>
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
static DriverInfoStruct *dis;
static union { Window w; byte b[2048]; } win;
static GState gs;

#define WHITE	0xffffffUL
#define RED	0xff0000UL
#define BLUE	0x0000ffUL

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

static void SetMask(sword l, sword t, sword r, sword b, Boolean simple)
{
    win.w.W_maskRect.R_left = l;
    win.w.W_maskRect.R_top = t;
    win.w.W_maskRect.R_right = r;
    win.w.W_maskRect.R_bottom = b;
    win.w.W_grFlags = (WinGrFlags) (WGF_MASK_VALID |
				    (simple ? WGF_MASK_SIMPLE : 0));
    win.w.W_clipRect.R_top = (sword) EOREGREC;	/* clip line not valid */
    win.w.W_clipRect.R_bottom = (sword) EOREGREC;
}

static void SetColor(unsigned long rgb)
{
    gs.GS_areaAttr.CA_colorRGB.RGB_red = (byte) (rgb >> 16);
    gs.GS_areaAttr.CA_colorRGB.RGB_green = (byte) (rgb >> 8);
    gs.GS_areaAttr.CA_colorRGB.RGB_blue = (byte) rgb;
}

static Boolean Rect(int l, int t, int r, int b)
{
    DriverRegs regs;

    memset(&regs, 0, sizeof(regs));
    regs.DR_ax = (word) l;
    regs.DR_bx = (word) t;
    regs.DR_cx = (word) r;
    regs.DR_dx = (word) b;
    regs.DR_dsSi = &gs;
    regs.DR_si = offsetof(GState, GS_areaAttr);
    regs.DR_esDi = &win.w;
    return DriverCall(dis, DR_VID_RECT, &regs);
}

static Boolean Line(int x1, int y1, int x2, int y2)
{
    DriverRegs regs;

    memset(&regs, 0, sizeof(regs));
    regs.DR_ax = (word) x1;
    regs.DR_bx = (word) y1;
    regs.DR_cx = (word) x2;
    regs.DR_dx = (word) y2;
    regs.DR_dsSi = &gs;
    regs.DR_si = offsetof(GState, GS_areaAttr);
    regs.DR_esDi = &win.w;
    return DriverCall(dis, DR_VID_LINE, &regs);
}

/* a function with a far pointer (DR_farPtr), the area attributes */
static Boolean Far(word f, dword ax, dword cx, dword bx, void *p)
{
    DriverRegs regs;

    memset(&regs, 0, sizeof(regs));
    regs.DR_ax = ax;
    regs.DR_bx = bx;
    regs.DR_cx = cx;
    regs.DR_dsSi = &gs;
    regs.DR_si = offsetof(GState, GS_areaAttr);
    regs.DR_esDi = &win.w;
    regs.DR_farPtr = p;
    return DriverCall(dis, f, &regs);
}

static Boolean PutBits(int x, int y, PutBitsArgs *pba)
{
    DriverRegs regs;

    memset(&regs, 0, sizeof(regs));
    regs.DR_ax = (word) x;
    regs.DR_bx = (word) y;
    regs.DR_dsSi = &gs;
    regs.DR_esDi = &win.w;
    regs.DR_bpPtr = pba;
    return DriverCall(dis, DR_VID_PUTBITS, &regs);
}

static void Call(word f, dword ax, dword bx)
{
    DriverRegs regs;

    memset(&regs, 0, sizeof(regs));
    regs.DR_ax = ax;
    regs.DR_bx = bx;
    CHECK(!DriverCall(dis, f, &regs));
}

int main(int argc, char **argv)
{
    static const sword region[] = {
	9, (sword) EOREGREC,
	29, 10, 99, (sword) EOREGREC,
	39, 10, 39, 60, 99, (sword) EOREGREC,
	59, 10, 99, (sword) EOREGREC,
	(sword) EOREGREC
    };
    char *slash;
    GeodeLoadError e;
    GeodeHandle drv;
    VideoDriverInfo *vdi;
    DriverRegs r;
    word *chunk;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    strncpy(top, argv[0], sizeof(top) - 8);
    top[sizeof(top) - 8] = 0;
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash) {
	sep = *slash;
	strcpy(slash + 1, "GVDTOP");
    } else
	strcpy(top, "GVDTOP");
    ghal_fs_mkdir(top);
    Put("SYSTEM", NULL, 0);
    Put("SYSTEM/VIDEO", NULL, 0);
    Put("SYSTEM/VIDEO/hostvid.geo", hostvid_geode, hostvid_geode_size);
    CHECK(KernelBoot(top, "", &e) == 0 && e == GLE_OK);

    FileSetStandardPath(SP_VIDEO_DRIVERS);
    drv = GeodeUseDriver("hostvid.geo", VIDEO_PROTO_MAJOR,
			 VIDEO_PROTO_MINOR, &e);
    CHECK(drv != 0);
    if (!drv) {
	printf("GeodeLoadError %d\n", (int) e);
	return 1;
    }
    dis = GeodeInfoDriver(drv);
    vdi = (VideoDriverInfo *) dis;
    CHECK(dis->DIS_driverType == DRIVER_TYPE_VIDEO &&
	  (dis->DIS_driverAttributes & DA_HAS_EXTENDED_INFO));

    /* no device yet */
    memset(&r, 0, sizeof(r));
    CHECK(DriverCall(dis, DR_VID_INFO, &r));
    CHECK(vdi->VDI_device == 0xffff);
    memset(&r, 0, sizeof(r));
    r.DR_dsSi = "Host Display: 320x200";
    CHECK(!DriverCall(dis, DRE_TEST_DEVICE, &r) && r.DR_ax == DP_PRESENT);
    CHECK(!DriverCall(dis, DRE_SET_DEVICE, &r) && r.DR_ax == DP_PRESENT);
    memset(&r, 0, sizeof(r));
    CHECK(!DriverCall(dis, DR_VID_INFO, &r) && r.DR_dsSi == vdi);
    CHECK(vdi->VDI_device == 0 && vdi->VDI_bmFormat == BMF_24BIT);
    if (!getenv("GEOS_DISPLAY"))
	CHECK(vdi->VDI_pageW == 320 && vdi->VDI_pageH == 200);

    /* the window: the mask region is a chunk of its block */
    chunk = (word *) (win.b + 1024);
    chunk[0] = 1024 + 2;
    memcpy(chunk + 1, region, sizeof(region));
    win.w.W_maskReg = 1024;
    gs.GS_header.LMBH_handle = 0x1230;
    gs.GS_mixMode = MM_COPY;
    gs.GS_areaAttr.CA_flags = AO_MASK_1 | AO_MASK_1_COPY;

    /* a rectangular mask: the whole screen, then clipped */
    SetMask(0, 0, 319, 199, TRUE);
    SetColor(WHITE);
    CHECK(!Rect(0, 0, 319, 199));
    CHECK(Pixel(0, 0) == WHITE && Pixel(319, 199) == WHITE);
    SetColor(RED);
    Rect(5, 5, 14, 9);
    CHECK(Pixel(5, 5) == RED && Pixel(14, 9) == RED);
    CHECK(Pixel(15, 5) == WHITE && Pixel(5, 10) == WHITE &&
	  Pixel(4, 5) == WHITE);
    Call(DR_VID_GETPIXEL, 5, 5);
    memset(&r, 0, sizeof(r));
    r.DR_ax = 5;
    r.DR_bx = 5;
    DriverCall(dis, DR_VID_GETPIXEL, &r);
    CHECK((r.DR_ax & 0xff) == 0xff && r.DR_bx == 0);	/* al r, bl g, bh b */
    SetColor(WHITE);
    Rect(0, 0, 319, 199);
    SetMask(10, 10, 99, 59, TRUE);
    SetColor(BLUE);
    Rect(-50, -50, 400, 300);
    CHECK(Pixel(9, 10) == WHITE && Pixel(10, 9) == WHITE);
    CHECK(Pixel(10, 10) == BLUE && Pixel(99, 59) == BLUE);
    CHECK(Pixel(100, 59) == WHITE && Pixel(99, 60) == WHITE);

    /* the mask region with its hole at 40..59 x 30..39 */
    SetMask(0, 0, 319, 199, TRUE);
    SetColor(WHITE);
    Rect(0, 0, 319, 199);
    SetMask(10, 10, 99, 59, FALSE);
    SetColor(RED);
    Rect(0, 0, 319, 199);
    CHECK(Pixel(45, 35) == WHITE);			/* the hole */
    CHECK(Pixel(39, 35) == RED && Pixel(60, 35) == RED);
    CHECK(Pixel(40, 35) == WHITE && Pixel(59, 39) == WHITE);
    CHECK(Pixel(45, 29) == RED && Pixel(45, 40) == RED);
    CHECK(Pixel(10, 10) == RED && Pixel(99, 59) == RED);
    CHECK(Pixel(9, 20) == WHITE && Pixel(100, 20) == WHITE &&
	  Pixel(50, 60) == WHITE);
    /* the clip line went down the region; back up for a rectangle
     * in the hole's band only */
    SetColor(BLUE);
    Rect(30, 32, 70, 33);
    CHECK(Pixel(30, 32) == BLUE && Pixel(70, 33) == BLUE &&
	  Pixel(50, 32) == WHITE);
    CHECK(win.w.W_clipRect.R_top == 30 && win.w.W_clipRect.R_bottom == 39);
    CHECK(!(win.w.W_grFlags & WGF_CLIP_SIMPLE) &&
	  win.w.W_clipRect.R_left == 10 && win.w.W_clipRect.R_right == 99);

    /* a draw mask, every other pixel, XOR */
    SetMask(0, 0, 319, 199, TRUE);
    SetColor(WHITE);
    Rect(0, 0, 319, 199);
    gs.GS_areaAttr.CA_flags = 0;
    memset(gs.GS_areaAttr.CA_mask, 0xaa, sizeof(DrawMask));
    gs.GS_mixMode = MM_XOR;
    SetColor(WHITE);
    Rect(200, 100, 207, 101);
    CHECK(Pixel(200, 100) == 0 && Pixel(201, 100) == WHITE &&
	  Pixel(206, 101) == 0 && Pixel(207, 101) == WHITE);
    gs.GS_mixMode = MM_COPY;
    gs.GS_areaAttr.CA_flags = AO_MASK_1 | AO_MASK_1_COPY;

    /* the exclusive: another GState does not draw */
    Call(DR_VID_START_EXCLUSIVE, 0, 0x4560);
    memset(&r, 0, sizeof(r));
    DriverCall(dis, DR_VID_GET_EXCLUSIVE, &r);
    CHECK(r.DR_bx == 0x4560);
    SetColor(RED);
    CHECK(!Rect(20, 20, 30, 25));
    CHECK(!Rect(300, 190, 400, 300));
    CHECK(Pixel(25, 22) == WHITE && Pixel(310, 195) == WHITE);
    memset(&r, 0, sizeof(r));
    r.DR_bx = 0x4560;
    DriverCall(dis, DR_VID_END_EXCLUSIVE, &r);
    CHECK(r.DR_ax != 0 && r.DR_si == 20 && r.DR_di == 20 &&
	  r.DR_cx == 319 && r.DR_dx == 199);
    Call(DR_VID_START_EXCLUSIVE, 0, 0x1230);	/* ours: drawn */
    Rect(20, 20, 30, 25);
    CHECK(Pixel(25, 22) == RED);
    memset(&r, 0, sizeof(r));
    r.DR_bx = 0x1230;
    DriverCall(dis, DR_VID_END_EXCLUSIVE, &r);
    CHECK(r.DR_ax == 0);

    /* the pointer, the default arrow: hidden until shown */
    SetColor(WHITE);
    Rect(0, 0, 319, 199);
    Call(DR_VID_MOVEPTR, 100, 100);
    CHECK(Pixel(101, 101) == WHITE);
    Call(DR_VID_SHOWPTR, 0, 0);
    CHECK(Pixel(100, 100) == WHITE && Pixel(101, 101) == 0 &&
	  Pixel(105, 101) == WHITE);
    Call(DR_VID_MOVEPTR, 150, 120);
    CHECK(Pixel(101, 101) == WHITE && Pixel(151, 121) == 0);
    /* drawing under it: the pointer stays on top */
    SetColor(RED);
    Rect(140, 110, 170, 140);
    CHECK(Pixel(151, 121) == 0 && Pixel(155, 121) == RED &&
	  Pixel(140, 110) == RED);
    Call(DR_VID_HIDEPTR, 0, 0);
    CHECK(Pixel(151, 121) == RED && Pixel(150, 120) == RED);
    Call(DR_VID_HIDEPTR, 0, 0);		/* nested */
    Call(DR_VID_SHOWPTR, 0, 0);
    CHECK(Pixel(151, 121) == RED);
    Call(DR_VID_SHOWPTR, 0, 0);
    CHECK(Pixel(151, 121) == 0);
    /* a pointer of our own: 8x2, hot spot 1,1; black, white, inverted */
    {
	static const byte ptr[sizeof(PointerDef) + 4] = {
	    8, 2, 1, 1,
	    0xc0, 0x00,		/* mask */
	    0x40, 0x80		/* image */
	};
	memset(&r, 0, sizeof(r));
	r.DR_dsSi = (void *) ptr;
	CHECK(!DriverCall(dis, DR_VID_SETPTR, &r));
	CHECK(Pixel(149, 119) == 0 && Pixel(150, 119) == WHITE &&
	      Pixel(149, 120) == (~RED & 0xffffff) && Pixel(151, 121) == RED);
	Call(DR_VID_HIDEPTR, 0, 0);
	CHECK(Pixel(149, 119) == RED && Pixel(149, 120) == RED);
    }

    /* lines: across the hole of the mask region, both ends drawn */
    SetMask(0, 0, 319, 199, TRUE);
    SetColor(WHITE);
    Rect(0, 0, 319, 199);
    Call(DR_VID_HIDEPTR, 0, 0);
    Call(DR_VID_SHOWPTR, 0, 0);
    Call(DR_VID_HIDEPTR, 0, 0);		/* the pointer out of the way */
    SetColor(RED);
    CHECK(!Line(0, 0, 9, 9));
    CHECK(Pixel(0, 0) == RED && Pixel(5, 5) == RED && Pixel(9, 9) == RED &&
	  Pixel(10, 10) == WHITE && Pixel(1, 0) == WHITE);
    Line(20, 5, 20, 1);				/* vertical, backwards */
    CHECK(Pixel(20, 1) == RED && Pixel(20, 5) == RED && Pixel(20, 0) == WHITE);
    Line(30, 3, 40, 5);				/* x major: runs */
    CHECK(Pixel(30, 3) == RED && Pixel(40, 5) == RED && Pixel(35, 4) == RED);
    {
	int x, n = 0;
	for (x = 30; x <= 40; x++)
	    n += (Pixel(x, 3) == RED) + (Pixel(x, 4) == RED) +
		 (Pixel(x, 5) == RED);
	CHECK(n == 11);				/* one pixel a column */
    }
    Line(50, 0, 52, 8);				/* y major */
    CHECK(Pixel(50, 0) == RED && Pixel(52, 8) == RED);
    SetMask(10, 10, 99, 59, FALSE);
    SetColor(BLUE);
    Line(0, 35, 200, 35);
    CHECK(Pixel(9, 35) == WHITE && Pixel(10, 35) == BLUE &&
	  Pixel(39, 35) == BLUE && Pixel(45, 35) == WHITE &&
	  Pixel(60, 35) == BLUE && Pixel(99, 35) == BLUE &&
	  Pixel(100, 35) == WHITE);

    /* GETBITS, PUTBITS */
    SetMask(0, 0, 319, 199, TRUE);
    {
	static union { Bitmap bm; byte b[sizeof(Bitmap) + 3 * 4 * 2]; } got;
	static const byte mono[2] = { 0xa0, 0x50 };	/* 4 x 2 */
	static const byte rgb[3 * 3] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	static const byte idx4[2] = { 0x4f, 0x10 };	/* red white, blue blk */
	static const byte masked[2 * 2] = { 0x80, 0xff, 0x40, 0x00 };
	PutBitsArgs pba;

	SetColor(WHITE);
	Rect(0, 0, 319, 199);
	SetColor(RED);
	Rect(100, 100, 101, 100);
	memset(&r, 0, sizeof(r));
	r.DR_ax = 100;
	r.DR_bx = 100;
	r.DR_cx = 4;
	r.DR_dx = 3;
	r.DR_bpPtr = &got;
	r.DR_si = sizeof(got);			/* room for two lines */
	CHECK(!DriverCall(dis, DR_VID_GETBITS, &r));
	CHECK(got.bm.B_width == 4 && got.bm.B_height == 2 &&
	      got.bm.B_type == BMF_24BIT && got.bm.B_compact == 0);
	CHECK(got.b[sizeof(Bitmap)] == 0xff && got.b[sizeof(Bitmap) + 1] == 0 &&
	      got.b[sizeof(Bitmap) + 6] == 0xff &&
	      got.b[sizeof(Bitmap) + 7] == 0xff);

	memset(&pba, 0, sizeof(pba));
	pba.PBA_bm.B_width = 4;
	pba.PBA_bm.B_height = 2;
	pba.PBA_bm.B_type = BMF_MONO;
	pba.PBA_data = mono;
	pba.PBA_size = 1;
	CHECK(!PutBits(10, 150, &pba));
	CHECK(Pixel(10, 150) == 0 && Pixel(11, 150) == WHITE &&
	      Pixel(12, 150) == 0 && Pixel(11, 151) == 0 &&
	      Pixel(10, 151) == WHITE && Pixel(14, 150) == WHITE);
	/* filled: the 1s in the area color, the 0s left */
	SetColor(RED);
	Rect(10, 150, 13, 151);
	SetColor(BLUE);
	pba.PBA_flags = PBF_FILL_MASK;
	PutBits(10, 150, &pba);
	CHECK(Pixel(10, 150) == BLUE && Pixel(11, 150) == RED &&
	      Pixel(11, 151) == BLUE);
	pba.PBA_flags = 0;
	/* 24 bits, 3 x 1 */
	pba.PBA_bm.B_width = 3;
	pba.PBA_bm.B_height = 1;
	pba.PBA_bm.B_type = BMF_24BIT;
	pba.PBA_data = rgb;
	pba.PBA_size = 9;
	PutBits(20, 150, &pba);
	CHECK(Pixel(20, 150) == 0x010203 && Pixel(22, 150) == 0x070809);
	/* 4 bits through the driver's palette (the standard colors) */
	pba.PBA_bm.B_width = 2;
	pba.PBA_bm.B_height = 2;
	pba.PBA_bm.B_type = BMF_4BIT;
	pba.PBA_data = idx4;
	pba.PBA_size = 1;
	PutBits(30, 150, &pba);
	CHECK(Pixel(30, 150) == 0xaa0000 && Pixel(31, 150) == WHITE &&
	      Pixel(30, 151) == 0x0000aa && Pixel(31, 151) == 0);
	/* with a mask: only the masked-in pixels */
	SetColor(WHITE);
	Rect(40, 150, 49, 151);
	pba.PBA_bm.B_width = 8;
	pba.PBA_bm.B_height = 2;
	pba.PBA_bm.B_type = BMF_MONO | BMT_MASK;
	pba.PBA_data = masked;
	pba.PBA_size = 2;
	PutBits(40, 150, &pba);
	CHECK(Pixel(40, 150) == 0 && Pixel(41, 150) == WHITE &&
	      Pixel(41, 151) == WHITE && Pixel(40, 151) == WHITE);
    }

    /* MOVBITS: down and right, overlapping */
    SetColor(WHITE);
    Rect(0, 0, 319, 199);
    SetColor(RED);
    Rect(200, 20, 209, 29);
    SetColor(BLUE);
    Rect(200, 20, 200, 20);
    memset(&r, 0, sizeof(r));
    r.DR_ax = 200;
    r.DR_bx = 20;
    r.DR_cx = 205;
    r.DR_dx = 25;
    r.DR_si = 10;
    r.DR_bp = 10;
    r.DR_dsSi = &gs;
    r.DR_esDi = &win.w;
    CHECK(!DriverCall(dis, DR_VID_MOVBITS, &r));
    CHECK(Pixel(205, 25) == BLUE && Pixel(214, 34) == RED &&
	  Pixel(206, 26) == RED && Pixel(215, 35) == WHITE);
    /* and back up, overlapping */
    r.DR_ax = 205;
    r.DR_bx = 25;
    r.DR_cx = 200;
    r.DR_dx = 20;
    CHECK(!DriverCall(dis, DR_VID_MOVBITS, &r));
    CHECK(Pixel(200, 20) == BLUE && Pixel(209, 29) == RED);

    /* polygons, regions, polylines, dashes, bits along a line */
    SetMask(0, 0, 319, 199, TRUE);
    SetColor(WHITE);
    Rect(0, 0, 319, 199);
    SetColor(RED);
    gs.GS_lineAttr = gs.GS_areaAttr;
    {
	static const Point tri[3] = { {100, 100}, {120, 100}, {100, 120} };
	static const Point back[3] = { {200, 100}, {200, 120}, {220, 100} };
	CHECK(!Far(DR_VID_POLYGON, 0, 3, 0, (void *) tri));
	CHECK(Pixel(105, 105) == RED && Pixel(120, 100) == RED &&
	      Pixel(100, 120) == RED && Pixel(110, 110) == RED &&
	      Pixel(115, 115) == WHITE && Pixel(99, 100) == WHITE &&
	      Pixel(100, 121) == WHITE);
	/* only clockwise: these go the other way round */
	Far(DR_VID_POLYGON, 1, 3, 0, (void *) back);
	CHECK(Pixel(205, 105) == WHITE);
	Far(DR_VID_POLYGON, 0, 3, 0, (void *) back);
	CHECK(Pixel(205, 105) == RED);
	/* the exclusive: not drawn, its bounds */
	Call(DR_VID_START_EXCLUSIVE, 0, 0x4560);
	Far(DR_VID_POLYGON, 0, 3, 0, (void *) tri);
	memset(&r, 0, sizeof(r));
	r.DR_bx = 0x4560;
	DriverCall(dis, DR_VID_END_EXCLUSIVE, &r);
	CHECK(r.DR_ax && r.DR_si == 100 && r.DR_di == 100 &&
	      r.DR_cx == 120 && r.DR_dx == 120);
    }
    {
	static const word rgn[] = {
	    0, 0, 9, 9,
	    (word) -1, EOREGREC,
	    4, 0, 9, EOREGREC,
	    9, 0, 4, EOREGREC,
	    EOREGREC
	};
	static const word prgn[] = {		/* right: PARAM_0 */
	    0, 0, PARAM_0, 4,
	    (word) -1, EOREGREC,
	    4, 0, PARAM_0, EOREGREC,
	    EOREGREC
	};
	static sword params[4] = { 7, 0, 0, 0 };
	Far(DR_VID_REGION, 50, 0, 150, (void *) rgn);
	CHECK(Pixel(50, 150) == RED && Pixel(59, 154) == RED &&
	      Pixel(59, 155) == WHITE && Pixel(54, 159) == RED &&
	      Pixel(55, 159) == WHITE && Pixel(50, 149) == WHITE &&
	      Pixel(50, 160) == WHITE);
	memset(&r, 0, sizeof(r));
	r.DR_ax = 200;
	r.DR_bx = 50;
	r.DR_dsSi = &gs;
	r.DR_si = offsetof(GState, GS_areaAttr);
	r.DR_esDi = &win.w;
	r.DR_farPtr = (void *) prgn;
	r.DR_bpPtr = params;
	CHECK(!DriverCall(dis, DR_VID_REGION, &r));
	CHECK(Pixel(207, 54) == RED && Pixel(208, 54) == WHITE &&
	      Pixel(200, 50) == RED);
    }
    {
	static const sword pl[] = {
	    10, 10, 20, 10, 20, 15, SEPARATOR,
	    30, 10, 30, 12, SEPARATOR, SEPARATOR
	};
	/* a brush 3 wide, 2 high */
	CHECK(!Far(DR_VID_POLYLINE, (2 << 8) | 3, 0, 0, (void *) pl));
	CHECK(Pixel(10, 10) == RED && Pixel(22, 11) == RED &&
	      Pixel(23, 11) == WHITE && Pixel(10, 12) == WHITE &&
	      Pixel(22, 16) == RED && Pixel(22, 17) == WHITE &&
	      Pixel(32, 13) == RED && Pixel(29, 10) == WHITE &&
	      Pixel(25, 12) == WHITE);
    }
    {
	static const WWFixed patt[2] = { {0, 2}, {0, 3} };	/* on 2, off 3 */
	static const char l0[] = "XX...XX...", l1[] = "X...XX...X",
	    l2[] = ".XX...XX..";
	DashInfo di;
	int x, ok;

	memset(&di, 0, sizeof(di));
	di.DI_patt = patt;
	di.DI_nPairs = 1;
	di.DI_pt1.P_x = 10;
	di.DI_pt1.P_y = 50;
	di.DI_pt2.P_x = 19;
	di.DI_pt2.P_y = 50;
	CHECK(!Far(DR_VID_DASH_LINE, 0, 0, 0, &di));
	for (ok = 1, x = 0; x < 10; x++)
	    ok &= (Pixel(10 + x, 50) == RED) == (l0[x] == 'X');
	CHECK(ok && di.DI_pattIdx == 0);
	di.DI_pattIdx = 1;
	di.DI_pt1.P_y = di.DI_pt2.P_y = 52;
	Far(DR_VID_DASH_LINE, 0, 0, 0, &di);
	for (ok = 1, x = 0; x < 10; x++)
	    ok &= (Pixel(10 + x, 52) == RED) == (l1[x] == 'X');
	CHECK(ok && di.DI_pattIdx == 1);
	di.DI_pattIdx = 0;			/* without the first pixel */
	di.DI_pt1.P_y = di.DI_pt2.P_y = 54;
	Far(DR_VID_DASH_LINE, 1, 0, 0, &di);
	for (ok = 1, x = 0; x < 10; x++)
	    ok &= (Pixel(10 + x, 54) == RED) == (l2[x] == 'X');
	CHECK(ok && di.DI_pattIdx == 4);
	di.DI_patt = NULL;			/* solid */
	di.DI_pt1.P_y = di.DI_pt2.P_y = 56;
	Far(DR_VID_DASH_LINE, 0, 0, 0, &di);
	CHECK(Pixel(10, 56) == RED && Pixel(19, 56) == RED);
	/* fat: each dash and its copy 3 lower */
	di.DI_patt = patt;
	di.DI_pattIdx = 0;
	di.DI_pt1.P_x = 40;
	di.DI_pt2.P_x = 49;
	di.DI_pt1.P_y = di.DI_pt2.P_y = 50;
	CHECK(!Far(DR_VID_DASH_FILL, 0, 3, 0, &di));
	CHECK(Pixel(40, 50) == RED && Pixel(41, 53) == RED &&
	      Pixel(42, 51) == WHITE && Pixel(45, 53) == RED &&
	      Pixel(47, 50) == WHITE && Pixel(41, 54) == WHITE);
    }
    {
	static const byte rgb[4 * 3] = {
	    0xff, 0, 0, 0, 0xff, 0, 0, 0, 0xff, 0xff, 0xff, 0xff
	};
	PutBitsArgs pba;

	SetColor(BLUE);
	Rect(100, 60, 110, 75);
	memset(&pba, 0, sizeof(pba));
	pba.PBA_bm.B_width = 4;
	pba.PBA_bm.B_height = 1;
	pba.PBA_bm.B_type = BMF_24BIT;
	pba.PBA_data = rgb;
	pba.PBA_size = 12;
	memset(&r, 0, sizeof(r));
	r.DR_ax = 100;
	r.DR_bx = 60;
	r.DR_cx = 103;
	r.DR_dx = 63;
	r.DR_dsSi = &gs;
	r.DR_esDi = &win.w;
	r.DR_bpPtr = &pba;
	CHECK(!DriverCall(dis, DR_VID_PUTLINE, &r));
	CHECK(Pixel(100, 60) == RED && Pixel(101, 61) == 0x00ff00 &&
	      Pixel(102, 62) == BLUE && Pixel(103, 63) == WHITE &&
	      Pixel(101, 60) == BLUE);
	r.DR_ax = 100;				/* longer than the bitmap */
	r.DR_bx = 70;
	r.DR_cx = 109;
	r.DR_dx = 70;
	DriverCall(dis, DR_VID_PUTLINE, &r);
	CHECK(Pixel(103, 70) == WHITE && Pixel(104, 70) == BLUE);
	r.DR_ax = 103;				/* right to left, bottom up: */
	r.DR_bx = 72;				/* still from the left end */
	r.DR_cx = 100;
	r.DR_dx = 72;
	DriverCall(dis, DR_VID_PUTLINE, &r);
	CHECK(Pixel(100, 72) == RED && Pixel(103, 72) == WHITE);
	r.DR_ax = 108;
	r.DR_bx = 75;
	r.DR_cx = 108;
	r.DR_dx = 72;
	DriverCall(dis, DR_VID_PUTLINE, &r);
	CHECK(Pixel(108, 72) == RED && Pixel(108, 73) == 0x00ff00 &&
	      Pixel(108, 75) == WHITE);
    }

    /* the XOR region: a 10x10 square whose right edge (PARAM_2)
     * follows the pointer; drawing over it, taking it away */
    {
	static const word sq[] = {
	    0, 0, PARAM_2, 9,
	    (word) -1, EOREGREC,
	    9, 0, PARAM_2, EOREGREC,
	    EOREGREC
	};
	MemHandle mh = MemAlloc(sizeof(sq), HF_DYNAMIC, 0);
	VisXORParams xp;

	CHECK(mh != 0);
	memcpy(MemLock(mh), sq, sizeof(sq));
	MemUnlock(mh);
	SetMask(0, 0, 319, 199, TRUE);
	SetColor(WHITE);
	Rect(0, 0, 319, 199);
	Call(DR_VID_MOVEPTR, 100, 100);
	memset(&xp, 0, sizeof(xp));
	xp.VXP_cx = 9;
	xp.VXP_dx = 9;
	xp.VXP_mousePos.P_x = 100;
	xp.VXP_mousePos.P_y = 100;
	memset(&r, 0, sizeof(r));
	r.DR_ax = 50;
	r.DR_bx = 50;
	r.DR_dx = mh;
	r.DR_cx = 0;
	r.DR_si = VXF_CX_PARAM_FOLLOWS_MOUSE;
	r.DR_bpPtr = &xp;
	CHECK(!DriverCall(dis, DR_VID_SET_XOR, &r));
	CHECK(Pixel(50, 50) == 0 && Pixel(59, 59) == 0 &&
	      Pixel(60, 59) == WHITE && Pixel(50, 60) == WHITE);
	/* drawing under it: it stays inverted on top */
	SetColor(RED);
	Rect(55, 55, 70, 70);
	CHECK(Pixel(56, 56) == 0x00ffff && Pixel(65, 65) == RED &&
	      Pixel(50, 50) == 0);
	/* the pointer moves 5 right: the right edge with it */
	Call(DR_VID_MOVEPTR, 105, 100);
	CHECK(Pixel(64, 50) == 0 && Pixel(50, 50) == 0 &&
	      Pixel(65, 50) == WHITE && Pixel(62, 56) == 0x00ffff);
	memset(&r, 0, sizeof(r));
	CHECK(!DriverCall(dis, DR_VID_CLEAR_XOR, &r));
	CHECK(r.DR_cx == 14 && r.DR_dx == 9 && r.DR_si == 50 &&
	      r.DR_di == 50);
	CHECK(Pixel(50, 50) == WHITE && Pixel(56, 56) == RED &&
	      Pixel(64, 50) == WHITE);
	MemFree(mh);
    }

    /* the palette */
    {
	RGBValue in[3] = { {1, 2, 3}, {4, 5, 6}, {7, 8, 9} }, out[3];
	memset(&r, 0, sizeof(r));
	r.DR_dsSi = in;
	r.DR_ax = 10;
	r.DR_cx = 3;
	CHECK(!DriverCall(dis, DR_VID_SET_PALETTE, &r));
	memset(out, 0, sizeof(out));
	r.DR_dsSi = out;
	CHECK(!DriverCall(dis, DR_VID_GET_PALETTE, &r));
	CHECK(!memcmp(in, out, sizeof(in)));
    }
    /* an escape: not supported */
    memset(&r, 0, sizeof(r));
    CHECK(!DriverCall(dis, VID_ESC_INVERT_SCREEN, &r) && r.DR_di == 0);
    /* the screen off and on again */
    Call(DR_VID_SCREEN_OFF, 0, 0);
    SetColor(BLUE);
    Rect(0, 0, 9, 9);
    Call(DR_VID_SCREEN_ON, 0, 0);
    CHECK(Pixel(5, 5) == BLUE);
    /* save under: not possible */
    memset(&r, 0, sizeof(r));
    r.DR_esDi = &win.w;
    CHECK(DriverCall(dis, DR_VID_SAVE_UNDER, &r));

    GeodeFreeDriver(drv);
    printf("%s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
