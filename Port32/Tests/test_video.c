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
