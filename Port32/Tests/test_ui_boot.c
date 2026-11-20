/*
 * test_ui_boot.c -- the UI booted as the kernel boots it (BootLoadUI)
 * with "[ui] specific = motif": the UI's process starts the system on
 * the host video driver hostvid ("[screen 0]"), its own application and
 * field, the system field, and the application uiboot (Geode/uiboot.goc)
 * of "[ui] execOnStartup", whose primary window holds a display group
 * with a document window.  Where the display is X11, on a screen of its
 * own (Xvfb).
 *
 * The steps:
 *   - booted: the specific UI motif, uiboot up and opened;
 *   1. uiboot's application an OLApplication, its primary an OLBaseWin
 *      on the screen;
 *   - the UI's process detached: uiboot goes, the UI ends its process
 *     and, used by no one, goes with the specific UI.
 */
#include "kernelC.h"
#include <Internal/videoDr.h>
#include <geosdriver.h>
#include <initfile.h>
#include <sysstats.h>
#include "ghal.h"

#include <stdio.h>
#include <string.h>

extern const unsigned char ui_geode[], uiboot_geode[], motif_geode[],
			   hostvid_geode[];
extern const unsigned long ui_geode_size, uiboot_geode_size,
			   motif_geode_size, hostvid_geode_size;

/* as uiboot.goc */
typedef struct {
    GeodeHandle		UBS_process;
    word		UBS_attached;
    word		UBS_opened;
    word		UBS_checks, UBS_fails, UBS_failLine;
    word		UBS_failLines[8];
    WindowHandle	UBS_primaryWin;
    WindowHandle	UBS_groupWin;
    unsigned short	UBS_displayNotices;
    unsigned short	UBS_listNotices;
    unsigned short	UBS_lastNum;
    char		UBS_lastName[16];
    unsigned char	UBS_lastOverlapping;
    short		UBS_gw, UBS_gh;
    short		UBS_dl, UBS_dt, UBS_dw, UBS_dh;
    short		UBS_sys[6];
    optr		UBS_maxIcon, UBS_restoreIcon;
    optr		UBS_pin;
    short		UBS_pinned[4];
    short		UBS_long[4];
    unsigned short	UBS_itemApplies, UBS_itemApply[3];
    unsigned short	UBS_boolApplies, UBS_boolApply[3];
    unsigned short	UBS_radioApplies, UBS_radioSel;
    unsigned short	UBS_checkApplies, UBS_checkSel;
    unsigned short	UBS_sizeApplies, UBS_sizeSel;
    short		UBS_sizeButton[2];
    word		UBS_scrollApplies, UBS_scrollAction;
    dword		UBS_scrollValue;
    short		UBS_scrollBar[3];
    word		UBS_scrollDrag;
} UBState;

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

static GeodeHandle Find(const char *name)
{
    return GeodeFind(name, GEODE_NAME_SIZE, 0, 0);
}

int main(void)
{
    GeodeHandle ui, app;
    UBState *st;
    void (*step)(word);
    struct { word callerID; optr ackOD; } dp = { 0, 0 };
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    kernelCurrentGeode = GEODE;
    KernelThreadInit(PRIORITY_STANDARD);
    KernelTimerInit();
    GeodeRegisterImage("ui", ui_geode, ui_geode_size);
    GeodeRegisterImage("uiboot", uiboot_geode, uiboot_geode_size);
    GeodeRegisterImage("motif", motif_geode, motif_geode_size);
    GeodeRegisterImage("hostvid", hostvid_geode, hostvid_geode_size);
    InitFileWriteString("ui", "generic", "ui");
    InitFileWriteString("ui", "specific", "motif");
    InitFileWriteString("ui", "execOnStartup", "uiboot");
    InitFileWriteString("screen 0", "driver", "hostvid");
    InitFileWriteString("screen 0", "device", "Host Display: 640x480");
    /* a mouse, as it were (there is no mouse driver): not keyboard
     * only, so the system menu has its icons and menus can be pinned */
    InitFileWriteBoolean("input", "keyboardOnly", FALSE);

    /* booted: uiboot of execOnStartup up */
    BootLoadUI();
    ui = (GeodeHandle) SysGetInfo(SGIT_UI_PROCESS);
    CHECK(ui != 0 && ui == Find("ui      "));
    WAIT_FOR(Find("uiboot  "));
    app = Find("uiboot  ");
    CHECK(app != 0);
    CHECK(Find("motif   ") != 0);
    if (!app)
	return 1;
    st = ProcGetLibraryEntry(app, 0);
    step = (void (*)(word)) ProcGetLibraryEntry(app, 1);
    WAIT_FOR(st->UBS_opened);
    CHECK(st->UBS_attached == 1 && st->UBS_opened == 1);

    step(1);
    CHECK(st->UBS_primaryWin != 0);
    printf("group %dx%d, doc1 at %d,%d %dx%d; notices %u, lists %u\n",
	   st->UBS_gw, st->UBS_gh, st->UBS_dl, st->UBS_dt, st->UBS_dw,
	   st->UBS_dh, st->UBS_displayNotices, st->UBS_listNotices);
    /* overlapping, selected, tiled, full sized, Ctrl+F6, not usable;
     * the separators in a menu, the File menu's Exit; the primary's
     * system menu, maximized and restored by its icons; the File menu
     * pinned, opened by its button, closed by its system menu; a menu
     * taller than the screen scrolled by its scrollers and to its focus;
     * the generic item and boolean groups; Motif's radio group and
     * check boxes; a popup list; a scrollbar */
    for (i = 2; i <= 33; i++)
	step(i);
    printf("system menu button at %d,%d %dx%d, title bar %d..%d\n",
	   st->UBS_sys[0], st->UBS_sys[1], st->UBS_sys[2], st->UBS_sys[3],
	   st->UBS_sys[4], st->UBS_sys[5]);
    printf("long menu %d..%d, down scroller at %d, scrolled to %d\n",
	   st->UBS_long[0], st->UBS_long[1], st->UBS_long[2],
	   st->UBS_long[3]);
    printf("radio applied %u times (last %u), check boxes %u (%u)\n",
	   st->UBS_radioApplies, st->UBS_radioSel, st->UBS_checkApplies,
	   st->UBS_checkSel);
    printf("popup list button %dx%d, applied %u times (last %u)\n",
	   st->UBS_sizeButton[0], st->UBS_sizeButton[1],
	   st->UBS_sizeApplies, st->UBS_sizeSel);
    printf("scrollbar %dx%d (scroll area %d), applied %u times, "
	   "dragged to %u\n", st->UBS_scrollBar[0], st->UBS_scrollBar[1],
	   st->UBS_scrollBar[2], st->UBS_scrollApplies, st->UBS_scrollDrag);
    printf("File menu pinned at %d,%d %dx%d\n", st->UBS_pinned[0],
	   st->UBS_pinned[1], st->UBS_pinned[2], st->UBS_pinned[3]);

    printf("geode checks: %u, failed %u (first at uiboot.goc line %u)\n",
	   st->UBS_checks, st->UBS_fails, st->UBS_failLine);
    for (i = 0; i < st->UBS_fails && i < 8; i++)
	printf("  failed: uiboot.goc line %u\n", st->UBS_failLines[i]);
    CHECK(st->UBS_checks >= 90 && !st->UBS_fails);

    /* the UI detached: uiboot goes, the UI's process ends */
    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(ui, 0), KMSG_META_DETACH,
		 &dp, sizeof(dp), NULL, 0);
    WAIT_FOR(!Find("uiboot  ") && !Find("ui      "));
    CHECK(!Find("uiboot  "));
    CHECK(!Find("ui      ") && !Find("motif   "));

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
