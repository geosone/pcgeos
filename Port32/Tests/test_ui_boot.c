/*
 * test_ui_boot.c -- the UI booted as the kernel boots it (BootLoadUI)
 * from a GEOS top (GUBTOP, with Berkeley in USERDATA/FONT) with
 * "[ui] specific = motif": the UI's process starts the system on
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
#include <stdlib.h>
#include <string.h>

extern const unsigned char ui_geode[], uiboot_geode[], motif_geode[],
			   hostvid_geode[], vidmem_geode[], berkeley_fnt[],
			   text_geode[];
extern const unsigned long ui_geode_size, uiboot_geode_size,
			   motif_geode_size, hostvid_geode_size,
			   vidmem_geode_size, berkeley_fnt_size,
			   text_geode_size;

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
    short		UBS_view[4];
    short		UBS_slist[5];
    unsigned short	UBS_dlist[4];
    short		UBS_glyph[5];
    word		UBS_text[6];
    word		UBS_text2[8];
    word		UBS_text3[8];
    word		UBS_text4[6];
    word		UBS_gtext[16];
    char		UBS_name[12];
    short		UBS_scroll[4];
    word		UBS_gadget[4];
    word		UBS_dlg[4];
    word		UBS_std[5];
    char		UBS_stdText[32];
    word		UBS_count[3];
    char		UBS_countText[8];
    word		UBS_fsel[3];
    word		UBS_fselSel[6][2];
    char		UBS_fselName[6][36];
    char		UBS_fselPath[2][64];
    word		UBS_ctl[6];
    word		UBS_ctlAt[8][4];
    word		UBS_tgrp[3];
    word		UBS_docCtl[5];
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

static char top[260], sep = '/';

/* a directory (no data) or a file of the GEOS top */
static void Put(const char *rel, const void *data, unsigned long size)
{
    char path[300], *p;
    ghal_file *f;
    size_t done = 0;

    snprintf(path, sizeof(path), "%s%c%s", top, sep, rel);
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

static GeodeHandle Find(const char *name)
{
    return GeodeFind(name, GEODE_NAME_SIZE, 0, 0);
}

/*
 * UIBOOT_SHOT=file: the screen (as GEOS drew it, read back from the
 * display) written as a PPM after step UIBOOT_SHOT_AT (default the
 * last), a second later or UIBOOT_SHOT_TICKS (a flashing cursor is
 * on for 40 ticks, then off for 40; Port32/tools/uiboot-shot.sh)
 */
static void Shot(int step)
{
    const char *f = getenv("UIBOOT_SHOT"), *at = getenv("UIBOOT_SHOT_AT");
    FILE *o;
    int x, y;

    /* (step < 0: the reference mode, whatever the step) */
    if (!f || (step >= 0 && step != (at ? atoi(at) : 81)))
	return;
    TimerSleep((word) (getenv("UIBOOT_SHOT_TICKS")
		       ? atoi(getenv("UIBOOT_SHOT_TICKS")) : 60));
    o = fopen(f, "wb");
    if (!o)
	return;
    fprintf(o, "P6\n640 480\n255\n");
    for (y = 0; y < 480; y++)
	for (x = 0; x < 640; x++) {
	    unsigned long p = ghal_display_read_back(x, y);

	    fputc((int) (p >> 16) & 0xff, o);
	    fputc((int) (p >> 8) & 0xff, o);
	    fputc((int) p & 0xff, o);
	}
    fclose(o);
}

int main(int argc, char **argv)
{
    /* a mouse, as it were (there is no mouse driver): not keyboard
     * only, so the system menu has its icons and menus can be pinned */
    static const char ini[] =
	"[ui]\r\ngeneric = ui\r\nspecific = motif\r\n"
	"execOnStartup = uiboot\r\n"
	"[screen 0]\r\ndriver = hostvid\r\n"
	"device = Host Display: 640x480\r\n"
	"[input]\r\nkeyboardOnly = false\r\n";
    GeodeLoadError e;
    char *slash;
    GeodeHandle ui, app;
    UBState *st;
    void (*step)(word);
    struct { word callerID; optr ackOD; } dp = { 0, 0 };
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    (void) argc;
    strncpy(top, argv[0], sizeof(top) - 8);
    top[sizeof(top) - 8] = 0;
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash) {
	sep = *slash;
	strcpy(slash + 1, "GUBTOP");
    } else
	strcpy(top, "GUBTOP");
    ghal_fs_mkdir(top);
    /* the test ends without a shutdown */
    {
	char path[300];

	snprintf(path, sizeof(path), "%s%cGEOS_ACT.IVE", top, sep);
	remove(path);
    }
    Put("SYSTEM", NULL, 0);
    Put("SYSTEM/VIDEO", NULL, 0);
    Put("SYSTEM/VIDEO/vidmem.geo", vidmem_geode, vidmem_geode_size);
    Put("USERDATA", NULL, 0);
    Put("USERDATA/FONT", NULL, 0);
    Put("USERDATA/FONT/Berkeley.fnt", berkeley_fnt, berkeley_fnt_size);
    /* for the file selector */
    Put("DOCUMENT", NULL, 0);
    Put("DOCUMENT/FSTEST", NULL, 0);
    Put("DOCUMENT/FSTEST/LETTERS", NULL, 0);
    Put("DOCUMENT/FSTEST/LETTERS/NOTE.TXT", "note\r\n", 6);
    Put("DOCUMENT/FSTEST/ALPHA.TXT", "alpha\r\n", 7);
    Put("DOCUMENT/FSTEST/BETA.TXT", "beta\r\n", 6);
    /* UIBOOT_REF=variant: the reference mode (see below) with the
     * product's font size, as Port32/tools/uiref-gen.py's application
     * shows it in the original GEOS */
    if (getenv("UIBOOT_REF")) {
	static char refIni[sizeof(ini) + 400];
	const char *fs = getenv("UIBOOT_FONTSIZE");

	if (!fs)
	    fs = "10";
	snprintf(refIni, sizeof(refIni), "%s[ui]\r\nfontid = berkeley\r\n"
		 "fontsize = %s\r\neditableTextFontID = berkeley\r\n"
		 "editableTextFontsize = %s\r\n"
		 "[motif options]\r\nbuttonInvertDelay = 6\r\n"
		 "activetitlebarcolor = 8\r\ngadgetRepeatDelay = 18\r\n"
		 "scrollArrowSize = 12\r\nselectColor = 135\r\n"
		 "[uiFeatures]\r\nwindowOptions = 2048\r\n"
		 "expressOptions = 4041\r\nhelpOptions = 0\r\n",
		 ini, fs, fs);
	Put("geos.ini", refIni, strlen(refIni));
    } else
	Put("geos.ini", ini, sizeof(ini) - 1);
    GeodeRegisterImage("ui", ui_geode, ui_geode_size);
    GeodeRegisterImage("uiboot", uiboot_geode, uiboot_geode_size);
    GeodeRegisterImage("text", text_geode, text_geode_size);
    GeodeRegisterImage("motif", motif_geode, motif_geode_size);
    GeodeRegisterImage("hostvid", hostvid_geode, hostvid_geode_size);

    /* booted, with the fonts of SP_FONT: uiboot of execOnStartup up */
    CHECK(KernelBoot(top, "", &e) == 0 && e == GLE_OK);
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

    /* the reference mode: no test, the objects as they come up in
     * variant UIBOOT_REF (steps 101...), then the screen */
    if (getenv("UIBOOT_REF")) {
	step((word) (100 + atoi(getenv("UIBOOT_REF"))));
	TimerSleep(60);
	Shot(-1);
	return 0;
    }
    step(1);
    Shot(1);
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
     * check boxes; a popup list; a scrollbar; a view; a scrolling list; a dynamic list; a glyph and
     * a separator; ... ; the sliders */
    for (i = 2; i <= 81; i++) {
	step(i);
	Shot(i);
    }
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
    printf("view %dx%d, its page %dx%d\n", st->UBS_view[2], st->UBS_view[3],
	   st->UBS_view[0], st->UBS_view[1]);
    printf("scrolling list: items %d high, %d shown in %dx%d, paged to "
	   "top item %d\n", st->UBS_slist[0], st->UBS_slist[1],
	   st->UBS_slist[2], st->UBS_slist[3], st->UBS_slist[4]);
    printf("dynamic list: %u monikers asked for, %u items, scrolled to "
	   "top item %u (first item %u)\n", st->UBS_dlist[0],
	   st->UBS_dlist[1], st->UBS_dlist[2], st->UBS_dlist[3]);
    printf("glyph %dx%d, with a longer text %d wide; separator %dx%d\n",
	   st->UBS_glyph[0], st->UBS_glyph[1], st->UBS_glyph[4],
	   st->UBS_glyph[2], st->UBS_glyph[3]);
    printf("text: lines %u, height %u, attrs %#x, line 2 at %u; then %u; view x was %u\n",
	   st->UBS_text[0], st->UBS_text[1], st->UBS_text[2],
	   st->UBS_text[3], st->UBS_text[4], st->UBS_text[5]);
    printf("text focus: sel flags %#x, flags %#x, cursor %d,%d; at 5: %d,%d; "
	   "typed: %d,%d\n",
	   st->UBS_text2[0], st->UBS_text2[1], (short) st->UBS_text2[2],
	   (short) st->UBS_text2[3], (short) st->UBS_text2[4],
	   (short) st->UBS_text2[5], (short) st->UBS_text2[6],
	   (short) st->UBS_text2[7]);
    printf("text keys: line 2 at %u, selected %u-%u, End %u, word %u, "
	   "down %u, height with Enter %u, end %u\n",
	   st->UBS_text3[0], st->UBS_text3[1], st->UBS_text3[2],
	   st->UBS_text3[3], st->UBS_text3[4], st->UBS_text3[5],
	   st->UBS_text3[6], st->UBS_text3[7]);
    printf("text mouse: press at %u (x %d), dragged to %u, back to %u; "
	   "word %u-%u\n",
	   st->UBS_text4[0], (short) st->UBS_text4[1], st->UBS_text4[2],
	   st->UBS_text4[3], st->UBS_text4[4], st->UBS_text4[5]);
    printf("GenText: state %#x %#x, %ux%u at %u in its composite; note "
	   "%ux%u (text %u), grown %u (text %u)\n",
	   st->UBS_gtext[0], st->UBS_gtext[1], st->UBS_gtext[2],
	   st->UBS_gtext[3], st->UBS_gtext[4], st->UBS_gtext[5],
	   st->UBS_gtext[6], st->UBS_gtext[7], st->UBS_gtext[14],
	   st->UBS_gtext[15]);
    printf("GenText: focus %#x, typed \"%s\", status %u (%#x), applied %u "
	   "(%#x), disabled %#x\n",
	   st->UBS_gtext[12], st->UBS_name, st->UBS_gtext[10],
	   st->UBS_gtext[11], st->UBS_gtext[8], st->UBS_gtext[9],
	   st->UBS_gtext[13]);
    printf("GenText: one line scrolled to %d (cursor at %d), Home %d (%d)\n",
	   st->UBS_scroll[0], st->UBS_scroll[1], st->UBS_scroll[2],
	   st->UBS_scroll[3]);
    printf("GenGadget: drawn %u, %ux%u, font %#x\n", st->UBS_gadget[0],
	   st->UBS_gadget[1], st->UBS_gadget[2], st->UBS_gadget[3]);
    printf("UserDoDialog: response %u, open %u, held %u then %u\n",
	   st->UBS_dlg[0], st->UBS_dlg[1], st->UBS_dlg[2], st->UBS_dlg[3]);
    printf("Standard dialog: response %u, open %u, children %u, focus %u; "
	   "sent %#x, \"%s\"\n", st->UBS_std[0], st->UBS_std[1],
	   st->UBS_std[2], st->UBS_std[3], st->UBS_std[4], st->UBS_stdText);
    printf("Spin gadget: applied %u times, last %u (flags %#x), shows \"%s\"\n",
	   st->UBS_count[0], st->UBS_count[1], st->UBS_count[2],
	   st->UBS_countText);
    printf("File selector: %u notifications (entry %u, flags %#x)\n",
	   st->UBS_fsel[0], st->UBS_fsel[1], st->UBS_fsel[2]);
    for (i = 0; i < 6; i++)
	printf("  selection %d: entry %u, flags %#x, \"%s\"\n", i,
	       st->UBS_fselSel[i][0], st->UBS_fselSel[i][1],
	       st->UBS_fselName[i]);
    printf("  paths \"%s\", \"%s\"\n", st->UBS_fselPath[0],
	   st->UBS_fselPath[1]);
    printf("Controller: %u updates (features %#x, tools %#x, data %u), "
	   "%u at the output (%#x)\n", st->UBS_ctl[0], st->UBS_ctl[1],
	   st->UBS_ctl[2], st->UBS_ctl[3], st->UBS_ctl[4], st->UBS_ctl[5]);
    for (i = 0; i < 8; i++)
	printf("  check %d: features %#x, interactable %#x, enabled %#x, "
	       "children %#x\n", i, st->UBS_ctlAt[i][0], st->UBS_ctlAt[i][1],
	       st->UBS_ctlAt[i][2], st->UBS_ctlAt[i][3]);
    printf("Tool group: children %#x, enabled %#x, highlighted %#x\n",
	   st->UBS_tgrp[0], st->UBS_tgrp[1], st->UBS_tgrp[2]);
    printf("Document control: %d children, usable %#x; enabled %#x, with "
	   "a document %#x, a dirty one %#x\n", st->UBS_docCtl[0],
	   st->UBS_docCtl[1], st->UBS_docCtl[2], st->UBS_docCtl[3],
	   st->UBS_docCtl[4]);
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
