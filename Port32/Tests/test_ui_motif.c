/*
 * test_ui_motif.c -- the Motif specific UI in C (Library/SpecUI:
 * motif.geo, CommonUI's OLSystemClass, OLScreenClass, OLFieldClass)
 * through the process geode uimotif (Geode/uimotif.goc), with "[ui]
 * specific = motif" and the host video driver hostvid at 320x200 as the
 * default video driver.  Where the display is X11, on a screen of its
 * own (Xvfb).
 *
 * The steps:
 *   1. the system object: built as an OLSystem;
 *   2. a screen: usable, an OLScreen of the driver's page (on Xvfb the
 *      whole screen of the server), its root window opened;
 *   3. a field ([ui] backgroundcolor blue): attached, on the screen,
 *      usable, on top: an OLField with its window on the root, the
 *      system's focus and target and default field; the queries (the
 *      screen, the field, the video driver, the display scheme, the
 *      vis parents); the field's window washed blue;
 *   4. the stagger slots of windows on the field;
 *   5. the background color changed (green), the field reset;
 *   6. lowered: the system finds it again as its focus and target;
 *   7. an application on the field: an OLApplication, the geode's input
 *      object; built on the field (its window and size); the queries;
 *   8. on top: the field's focus and target, the focus application;
 *      busy, input held up and ignored; the gadget timer (the process
 *      gets MSG_TIMER_EXPIRED);
 *   9. no longer targetable: the target leaves it, the focus stays;
 *  10. OLCtrl: a framed group with a moniker above, a toolbox;
 *  11. OLWin: a command window under the application: built on it,
 *      staggered in a field slot, its window opened on the field's
 *      with a title bar; on top it has the application's focus; closed
 *      it gives its slot back.
 */
#include "kernelC.h"
#include <Internal/videoDr.h>
#include <geosdriver.h>
#include <initfile.h>
#include "ghal.h"

#include <stdio.h>
#include <string.h>

extern const unsigned char ui_geode[], uimotif_geode[], motif_geode[],
			   hostvid_geode[];
extern const unsigned long ui_geode_size, uimotif_geode_size,
			   motif_geode_size, hostvid_geode_size;

/* as uimotif.goc */
typedef struct {
    GeodeHandle		UMS_process;
    word		UMS_attached;
    word		UMS_checks, UMS_fails, UMS_failLine;
    word		UMS_failLines[8];
    WindowHandle	UMS_screenWin;
    WindowHandle	UMS_fieldWin;
    word		UMS_timerExpired;
} UMState;

static int fails;

static void Check(int ok, int line, const char *what)
{
    if (!ok) {
	fails++;
	printf("FAIL line %d: %s\n", line, what);
    }
}
#define CHECK(c) Check(!!(c), __LINE__, #c)

#define WAIT_FOR(cond) \
    do { int n_; for (n_ = 0; n_ < 500 && !(cond); n_++) TimerSleep(1); \
    } while (0)

#define GEODE	0x2340

/* the standard palette */
#define BLUE	0x0000aaUL
#define GREEN	0x00aa00UL

static unsigned long Pixel(int x, int y)
{
    return ghal_display_read_back(x, y) & 0xffffff;
}

int main(void)
{
    GeodeHandle app, hv;
    GeodeLoadError e;
    UMState *st;
    DriverRegs r;
    void (*step)(word);
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    kernelCurrentGeode = GEODE;
    KernelThreadInit(PRIORITY_STANDARD);
    KernelTimerInit();
    GeodeRegisterImage("ui", ui_geode, ui_geode_size);
    GeodeRegisterImage("uimotif", uimotif_geode, uimotif_geode_size);
    GeodeRegisterImage("motif", motif_geode, motif_geode_size);
    GeodeRegisterImage("hostvid", hostvid_geode, hostvid_geode_size);
    InitFileWriteString("ui", "specific", "motif");
    InitFileWriteInteger("ui", "backgroundcolor", C_BLUE);

    hv = GeodeUseDriver("hostvid", VIDEO_PROTO_MAJOR, VIDEO_PROTO_MINOR,
			&e);
    CHECK(hv != 0);
    if (!hv) {
	printf("GeodeUseDriver error %d\n", (int) e);
	return 1;
    }
    memset(&r, 0, sizeof(r));
    r.DR_dsSi = "Host Display: 320x200";
    CHECK(!DriverCall(GeodeInfoDriver(hv), DRE_SET_DEVICE, &r));
    GeodeSetDefaultDriver(GDDT_VIDEO, hv);

    app = GeodeLoad("uimotif", GA_PROCESS, GA_LIBRARY, 0, 0, &e);
    CHECK(app != 0);
    if (!app) {
	printf("GeodeLoad error %d\n", (int) e);
	return 1;
    }
    st = ProcGetLibraryEntry(app, 0);
    step = (void (*)(word)) ProcGetLibraryEntry(app, 1);
    WAIT_FOR(st->UMS_attached);
    CHECK(st->UMS_attached == 1 && st->UMS_process == app);

    step(1);
    step(2);
    CHECK(st->UMS_screenWin != 0);
    step(3);
    CHECK(st->UMS_fieldWin != 0);
    CHECK(Pixel(0, 0) == BLUE && Pixel(160, 100) == BLUE &&
	  Pixel(319, 199) == BLUE);
    printf("field: %06lx\n", Pixel(160, 100));
    step(4);
    step(5);
    CHECK(Pixel(0, 0) == GREEN && Pixel(319, 199) == GREEN);
    step(6);
    CHECK(Pixel(160, 100) == GREEN);
    step(7);
    step(8);
    WAIT_FOR(st->UMS_timerExpired);
    CHECK(st->UMS_timerExpired == 1);
    step(9);
    step(10);
    step(11);

    printf("geode checks: %u, failed %u\n", st->UMS_checks, st->UMS_fails);
    for (i = 0; i < st->UMS_fails && i < 8; i++)
	printf("  failed: uimotif.goc line %u\n", st->UMS_failLines[i]);
    CHECK(st->UMS_checks >= 108 && !st->UMS_fails);

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
