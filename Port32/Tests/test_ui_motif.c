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
 *      it gives its slot back;
 *  12. OLBaseWin: a GenPrimary under the application (OLBuildPrimary):
 *      its attributes and position flags; usable, its window on the
 *      field's, its gadget area holding a group and a title bar group
 *      a toolbox; maximized (the whole field) and restored, minimized
 *      (closed) and restored, not usable;
 *  13. OLDialogWin: a command dialog and a modal notification under the
 *      application (OLBuildInteraction): built twice, centered, closed
 *      until initiated; the gadget area, a title bar group and the reply
 *      bar (its own, or one the dialog makes for a group seeking it);
 *      dismissed and initiated again, complete (a single usage notice
 *      closes), not usable;
 *  14. OLButton: GenTriggers in a dialog (OLBuildTrigger): in the gadget
 *      area, bordered, sized by the moniker (the window mark wider); a
 *      dismiss trigger seeking the reply bar there, the dialog's
 *      standard trigger; pressed (depressed, the mouse grabbed), moved
 *      out and back, released (activated); MSG_GEN_ACTIVATE (the action
 *      data); disabled it does nothing;
 *  15. the actions arrived (the test waits for them); the trigger's
 *      action message and destination; the dismiss trigger closes the
 *      dialog at once (ATTR_GEN_TRIGGER_IMMEDIATE_ACTION);
 *  16. the standard triggers dialogs add to their reply bars, with the
 *      standard monikers: Close of a command dialog (gone when not
 *      usable), OK of a notice, Yes and No of an affirmation (Yes sent
 *      completes and closes it), Apply, Reset (disabled) and Close of
 *      delayed complex properties;
 *  17. a dialog the user brings up, in a dialog: its OLButton (the
 *      window mark) in the host's gadget area, activated;
 *  18. the dialog came up; dismissed it closes; the host not usable,
 *      the button goes;
 *  19. menus: a primary's menu bar (first in it) with the buttons of
 *      two menus (one named by its group type); pressed, a menu comes
 *      up with its item, released on the button it stays up; the other
 *      button closes it and opens its own; a press outside closes a
 *      menu in stay up mode (replayed); activated, up in stay up mode;
 *      complete, down, the button as it was;
 *  20. the mouse in a window: a press starts its grab and, on the
 *      title, a move (a click moves nothing, dragged and released it
 *      moves); on the bottom right corner a resize; all buttons up the
 *      grab ends; the pointer in and out of it; a sub menu's button
 *      with its right mark, its pixels checked here;
 *  21. the menu down, the primary not usable;
 *  22. the keyboard in the menus: Alt alone puts the focus on the first
 *      menu button (cursored); right and left along the bar; down opens
 *      the menu (its first item the focus); down and up through the
 *      items; right on a sub menu's button opens it, left closes it
 *      back; right goes on to the next menu of the bar; Escape closes;
 *  23. mnemonics: Alt+V opens the View menu; Escape;
 *  24. the focus back (queued): V alone does nothing, with the menu
 *      bar's focus (Alt alone) it opens the menu;
 *  25. in the Edit menu C activates Copy (the action arrives), M opens
 *      the sub menu; Escape there closes both;
 *  26. the focus out of the menu bar; not usable.
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
    word		UMS_triggered;
    optr		UMS_trigger;
    word		UMS_trigData;
    short		UMS_markX, UMS_markY;
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
#define DARK_GREY	0x555555UL
#define WHITE	0xffffffUL

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
    step(12);
    step(13);
    step(14);
    WAIT_FOR(st->UMS_triggered >= 2);
    step(15);
    step(16);
    step(17);
    step(18);
    step(19);
    step(20);
    /* the sub menu's right mark: the dark part (row 4, columns 9 and
     * 10), the light part in white (row 1, columns 3 and 4) */
    WAIT_FOR(Pixel(st->UMS_markX + 3, st->UMS_markY) == WHITE ||
	     Pixel(st->UMS_markX + 3, st->UMS_markY + 1) == WHITE);
    /* the sub menu's right mark: the dark part (row 4, columns 9 and
     * 10) in the dark grey, the light part (row 1, columns 3 and 4) in
     * white; the row found within one of the center */
    for (i = -1; i <= 1; i++)
	if (Pixel(st->UMS_markX + 9, st->UMS_markY + 4 + i) == DARK_GREY)
	    break;
    printf("mark at %d,%d (row %+d)\n", st->UMS_markX, st->UMS_markY, i);
    CHECK(i <= 1);
    CHECK(Pixel(st->UMS_markX + 9, st->UMS_markY + 4 + i) == DARK_GREY &&
	  Pixel(st->UMS_markX + 10, st->UMS_markY + 4 + i) == DARK_GREY &&
	  Pixel(st->UMS_markX + 8, st->UMS_markY + 4 + i) != DARK_GREY);
    CHECK(Pixel(st->UMS_markX + 3, st->UMS_markY + 1 + i) == WHITE &&
	  Pixel(st->UMS_markX + 4, st->UMS_markY + 1 + i) == WHITE &&
	  Pixel(st->UMS_markX + 5, st->UMS_markY + 1 + i) != WHITE);
    step(21);
    step(22);
    step(23);
    step(24);
    i = st->UMS_triggered;
    step(25);
    WAIT_FOR(st->UMS_triggered > i);
    CHECK(st->UMS_triggered == i + 1);
    step(26);

    printf("geode checks: %u, failed %u\n", st->UMS_checks, st->UMS_fails);
    for (i = 0; i < st->UMS_fails && i < 8; i++)
	printf("  failed: uimotif.goc line %u\n", st->UMS_failLines[i]);
    CHECK(st->UMS_checks >= 337 && !st->UMS_fails);

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
