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
#include <Internal/im.h>
#include <input.h>
#include <geosdriver.h>
#include <initfile.h>
#include <sysstats.h>
#include <system.h>
#include "ghal.h"
#include <sound.h>
#include <Internal/soundDrv.h>
#include "tsnddrv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char ui_geode[], uiboot_geode[], motif_geode[],
			   hostvid_geode[], vidmem_geode[], berkeley_fnt[],
			   text_geode[], sound_geode[], stream_geode[],
			   tsnddrv_geode[];
extern const unsigned long ui_geode_size, uiboot_geode_size,
			   motif_geode_size, hostvid_geode_size,
			   vidmem_geode_size, berkeley_fnt_size,
			   text_geode_size, sound_geode_size,
			   stream_geode_size, tsnddrv_geode_size;

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
    word		UBS_docDlg[9];
    word		UBS_docGrp[7];
    word		UBS_iacp[14];
    word		UBS_doc[14];
    word		UBS_docAs[7];
    word		UBS_docOb[8];
    word		UBS_fcn[16];
    word		UBS_low[8];
    word		UBS_snd[6];
    word		UBS_stub[24];
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

/* SysNotify: an application of execOnStartupBG that is not there
 * (LoadApplicationWithErrorMessage): go on */
static int notifies;
static char notifyText[160];

static word Notify(word flags, const char *string1, const char *string2)
{
    notifies++;
    snprintf(notifyText, sizeof(notifyText), "%s %s",
	     string1 ? string1 : "", string2 ? string2 : "");
    return (flags & SNF_CONTINUE) ? SNF_CONTINUE : 0;
}

/* a click of the first button at x, y on the screen, through the input
 * manager (as a mouse driver would send it) */
static void ToIM(Message m, word cx, word dx, word bp)
{
    IMParams p;

    p.IMP_cx = cx;
    p.IMP_dx = dx;
    p.IMP_bp = bp;
    p.IMP_si = 0;
    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(ImInfoInputProcess(), 0), m,
		 &p, sizeof(p), NULL, 0);
}

static void Click(word x, word y)
{
    static word now = 1000;

    ToIM(MSG_IM_PTR_CHANGE, x, y, PI_absX | PI_absY);
    TimerSleep(2);
    now += 1000;
    ToIM(MSG_IM_BUTTON_CHANGE, now, 0, BUTTON_0 | BI_PRESS);
    TimerSleep(2);
    now += 10;
    ToIM(MSG_IM_BUTTON_CHANGE, now, 0, BUTTON_0);
}

static GeodeHandle Find(const char *name)
{
    return GeodeFind(name, GEODE_NAME_SIZE, 0, 0);
}

/* the sounds of step 300 at the test driver: the error beep's trombone
 * and its three voices on at 500 Hz, the note at 880 Hz */
static void Sounds(GeodeHandle drv, int from)
{
    TsndLog *log;
    DriverRegs r;
    int i, trombone = 0, beeps = 0, note = 0, t;

    memset(&r, 0, sizeof(r));
    CHECK(drv && !DriverCall(GeodeInfoDriver(drv), TSND_GET_LOG, &r));
    log = r.DR_farPtr;
    if (!log)
	return;
    for (t = 0; t < 120 && beeps < 3; t++) {
	TimerSleep(1);
	trombone = beeps = note = 0;
	for (i = from; i < log->TL_count; i++) {
	    const TsndEvent *ev = &log->TL_ev[i];

	    if (ev->TE_fn == DRE_SOUND_SET_ENVELOPE &&
		ev->TE_si == IP_TROMBONE)
		trombone++;
	    if (ev->TE_fn == DRE_SOUND_VOICE_ON && ev->TE_ax == 500)
		beeps++;
	    if (ev->TE_fn == DRE_SOUND_VOICE_ON && ev->TE_ax == 880)
		note++;
	}
    }
    for (i = from; getenv("UIBOOT_SNDLOG") && i < log->TL_count; i++)
	printf("  ev fn %#x ax %u bx %#x cx %u si %u tick %lu\n",
	       log->TL_ev[i].TE_fn, log->TL_ev[i].TE_ax, log->TL_ev[i].TE_bx,
	       log->TL_ev[i].TE_cx, log->TL_ev[i].TE_si,
	       (unsigned long) log->TL_ev[i].TE_tick);
    printf("sounds: %d driver events, trombone %d, 500 Hz %d, 880 Hz %d\n",
	   log->TL_count - from, trombone, beeps, note);
    CHECK(trombone >= 1 && beeps >= 3 && note == 1);
}

/* the sound of step 301: one no input beep (its 800 Hz) of a content's
 * unwanted press, none of the release */
static void NoInputBeep(GeodeHandle drv, void (*step)(word))
{
    DriverRegs r;
    TsndLog *log;
    int i, from, noInput = 0, t;

    memset(&r, 0, sizeof(r));
    CHECK(drv && !DriverCall(GeodeInfoDriver(drv), TSND_GET_LOG, &r));
    log = r.DR_farPtr;
    if (!log)
	return;
    /* the voices free again (step 300's sounds over) */
    for (t = 0, i = -1; t < 200 && i != (int) log->TL_count; t++) {
	i = log->TL_count;
	TimerSleep(10);
    }
    from = log->TL_count;
    step(301);
    for (t = 0; t < 120 && !noInput; t++) {
	TimerSleep(1);
	for (i = from; i < (int) log->TL_count; i++)
	    if (log->TL_ev[i].TE_fn == DRE_SOUND_VOICE_ON &&
		log->TL_ev[i].TE_ax == 800)
		noInput++;
    }
    TimerSleep(10);
    for (i = from, noInput = 0; i < (int) log->TL_count; i++)
	if (log->TL_ev[i].TE_fn == DRE_SOUND_VOICE_ON &&
	    log->TL_ev[i].TE_ax == 800)
	    noInput++;
    printf("no input beep: %d\n", noInput);
    CHECK(noInput == 1);
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
	"execOnStartupBG = ubnosuch\r\n"
	"[screen 0]\r\ndriver = hostvid\r\n"
	"device = Host Display: 640x480\r\n"
	"[input]\r\nkeyboardOnly = false\r\n"
	"[sound]\r\nsynthDriver = tsnddrv.geo\r\n";
    GeodeLoadError e;
    char *slash;
    GeodeHandle ui, app;
    UBState *st;
    void (*step)(word);
    struct { word callerID; optr ackOD; } dp = { 0, 0 };
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    /* "menuhdr": the menu bar in the header of a maximized window
     * (windowOptions: UIWO_COMBINE_HEADER_AND_MENU_IN_MAXIMIZED_WINDOWS
     * set), steps 319, 320 only */
    int menuHdr = argc > 1 && !strcmp(argv[1], "menuhdr");
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
    /* the sound library on the test driver, which logs what it is told */
    Put("SYSTEM/SOUND", NULL, 0);
    Put("SYSTEM/stream.geo", stream_geode, stream_geode_size);
    Put("SYSTEM/sound.geo", sound_geode, sound_geode_size);
    Put("SYSTEM/SOUND/tsnddrv.geo", tsnddrv_geode, tsnddrv_geode_size);
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
		 "[uiFeatures]\r\nwindowOptions = %s\r\n"
		 "expressOptions = 4041\r\nhelpOptions = 0\r\n",
		 ini, fs, fs,
		 /* (variant 14: the menus in the header when maximized) */
		 atoi(getenv("UIBOOT_REF")) == 14 ? "26720" : "2048");
	Put("geos.ini", refIni, strlen(refIni));
    } else if (menuHdr) {
	static const char hdr[] = "[uiFeatures]\r\nwindowOptions = 8224\r\n";
	static char hdrIni[sizeof(ini) + sizeof(hdr)];

	snprintf(hdrIni, sizeof(hdrIni), "%s%s", ini, hdr);
	Put("geos.ini", hdrIni, strlen(hdrIni));
    } else
	Put("geos.ini", ini, sizeof(ini) - 1);
    GeodeRegisterImage("ui", ui_geode, ui_geode_size);
    GeodeRegisterImage("uiboot", uiboot_geode, uiboot_geode_size);
    GeodeRegisterImage("text", text_geode, text_geode_size);
    GeodeRegisterImage("motif", motif_geode, motif_geode_size);
    GeodeRegisterImage("hostvid", hostvid_geode, hostvid_geode_size);

    /* booted, with the fonts of SP_FONT: uiboot of execOnStartup up */
    kernelSysNotifyHook = Notify;
    CHECK(KernelBoot(top, "", &e) == 0 && e == GLE_OK);
    ui = (GeodeHandle) SysGetInfo(SGIT_UI_PROCESS);
    CHECK(ui != 0 && ui == Find("ui      "));
    WAIT_FOR(Find("uiboot  "));
    app = Find("uiboot  ");
    CHECK(app != 0);
    CHECK(Find("motif   ") != 0);
    if (!app)
	return 1;
    /* the missing one of execOnStartupBG told, the startup gone on */
    WAIT_FOR(notifies);
    printf("notified %d: %s\n", notifies, notifyText);
    CHECK(notifies == 1 &&
	  strstr(notifyText, "Unable to load application") &&
	  strstr(notifyText, "\"ubnosuch\""));
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
    if (menuHdr) {
	step(319);
	TimerSleep(10);
	Shot(319);
	step(320);
	printf("menus in the header: %u (bar at %d,%d, header %d,%d, "
	       "restored at %d)\n", st->UBS_stub[20], (sword) st->UBS_stub[21],
	       (sword) st->UBS_stub[22], (sword) st->UBS_stub[23],
	       (sword) st->UBS_stub[17], (sword) st->UBS_stub[19]);
	CHECK(st->UBS_stub[20] == 15);
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails != 0;
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
    for (i = 2; i <= 100; i++) {
	step(i);
	Shot(i);
    }
    /* (101 to 113 are the reference mode's) */
    for (i = 200; i <= 209; i++) {
	if (i == 209)
	    /* (the close of step 208 goes on on the application's
	     * thread; step 210 counts the documents) */
	    WAIT_FOR((step(210), st->UBS_low[6] == 0));
	step(i);
	Shot(i);
    }
    /* the standard sounds, through sound.geo the UI loaded */
    {
	GeodeHandle drv = Find("tsnddrv ");
	DriverRegs r;
	int from = 0;

	CHECK(Find("sound   ") != 0 && drv != 0);
	memset(&r, 0, sizeof(r));
	if (drv && !DriverCall(GeodeInfoDriver(drv), TSND_GET_LOG, &r))
	    from = ((TsndLog *) r.DR_farPtr)->TL_count;
	step(300);
	Sounds(drv, from);
	NoInputBeep(drv, step);
	/* a visual object's classed event TO_SYS_FOCUS reaches the
	 * application at the system's focus */
	step(302);
	WAIT_FOR(st->UBS_stub[0]);
	printf("classed event to the system: %u\n", st->UBS_stub[0]);
	CHECK(st->UBS_stub[0] == 1);
	/* the application lowered to the bottom loses the focus; a click
	 * into its primary brings it back to the top with the focus */
	step(303);
	CHECK(st->UBS_stub[1]);
	Click(st->UBS_stub[2], st->UBS_stub[3]);
	WAIT_FOR((step(304), st->UBS_stub[4]));
	printf("click: the field's focus %#x lowered, %#x after the click "
	       "at %u,%u\n", st->UBS_stub[5], st->UBS_stub[6],
	       st->UBS_stub[2], st->UBS_stub[3]);
	CHECK(st->UBS_stub[4]);
	/* the next window: the tool box, the primary, the tool box */
	step(305);
	printf("next window: %u %u %u\n", st->UBS_stub[8], st->UBS_stub[9],
	       st->UBS_stub[10]);
	CHECK(st->UBS_stub[8] == 2 && st->UBS_stub[9] == 1 &&
	      st->UBS_stub[10] == 2);
	/* the system menu's accelerators, a menu in stay up mode released */
	step(306);
	step(307);
	TimerSleep(5);
	step(308);
	TimerSleep(5);
	step(309);
	printf("system menu keys: menu up %u maximized %u, Alt+F5 restored "
	       "and menu gone %u, Alt+F10 maximized %u\n", st->UBS_stub[11],
	       st->UBS_stub[12], st->UBS_stub[13], st->UBS_stub[14]);
	CHECK(st->UBS_stub[11] && st->UBS_stub[12] && st->UBS_stub[13] == 3 &&
	      st->UBS_stub[14]);
	/* a dialog's command activates its trigger */
	step(310);
	printf("activate command: trigger actions %u, response %u\n",
	       st->UBS_stub[15], st->UBS_stub[7]);
	CHECK(st->UBS_stub[15] == 1 && st->UBS_stub[7] == 6 /* IC_YES */);
	/* a slider with its GenValue's delayed drag notification */
	step(311);
	printf("delayed slider: %u\n", st->UBS_stub[6]);
	CHECK(st->UBS_stub[6] == 3);
	/* chosen as a task: the minimized primary comes back */
	step(312);
	TimerSleep(5);
	step(313);
	printf("task selected: %u\n", st->UBS_stub[5]);
	CHECK(st->UBS_stub[5] == 3);
	/* the display of a document */
	step(314);
	TimerSleep(5);
	step(315);
	printf("display with a document: %u\n", st->UBS_stub[16]);
	CHECK(st->UBS_stub[16] == 15);
	/* the default action activates a button with the focus */
	step(316);
	TimerSleep(5);
	step(317);
	printf("default action to the focus: %u\n", st->UBS_stub[18]);
	CHECK(st->UBS_stub[18] == 15);
	/* the application's new moniker reaches its primaries' icons */
	step(321);
	printf("icon caption: %u\n", st->UBS_stub[9]);
	CHECK(st->UBS_stub[9] == 3);
	/* a click into a modal dialog reaches its button */
	step(322);
	TimerSleep(60);
	step(323);
	Click(st->UBS_stub[10], st->UBS_stub[11]);
	TimerSleep(20);
	step(324);
	printf("modal click at %u,%u: %u\n", st->UBS_stub[10],
	       st->UBS_stub[11], st->UBS_stub[13]);
	CHECK(st->UBS_stub[13] == 3);
	/* a sub menu changing usable updates its menu's separators */
	step(325);
	printf("sub menu separators: %u\n", st->UBS_stub[14]);
	CHECK(st->UBS_stub[14] == 7);
	/* a trigger with a default moniker: the level icon */
	step(326);
	printf("default moniker: %u\n", st->UBS_stub[17]);
	CHECK(st->UBS_stub[17] == 7);
	printf("standard sounds: counts %u %u %u %u\n", st->UBS_snd[0],
	       st->UBS_snd[1], st->UBS_snd[2], st->UBS_snd[3]);
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
    printf("Document control's dialogs: tasks");
    for (i = 0; i < 9; i++)
	printf(" %u", st->UBS_docDlg[i]);
    printf("\n");
    printf("Document group: attrs %#x, protocol %u, default name on "
	   "disk %u, found %u; file id %#x %#x, of none %#x\n",
	   st->UBS_docGrp[0], st->UBS_docGrp[1], st->UBS_docGrp[2],
	   st->UBS_docGrp[3], st->UBS_docGrp[4], st->UBS_docGrp[5],
	   st->UBS_docGrp[6]);
    printf("IACP:");
    for (i = 0; i < 14; i++)
	printf(" %u", st->UBS_iacp[i]);
    printf("\n");
    printf("Document:");
    for (i = 0; i < 14; i++)
	printf(" %#x", st->UBS_doc[i]);
    printf("\n");
    printf("Save as:");
    for (i = 0; i < 7; i++)
	printf(" %#x", st->UBS_docAs[i]);
    printf("\n");
    printf("Copy, move, type:");
    for (i = 0; i < 8; i++)
	printf(" %#x", st->UBS_docOb[i]);
    printf("\n");
    printf("Low disk:");
    for (i = 0; i < 8; i++)
	printf(" %u", st->UBS_low[i]);
    printf("\n");
    printf("File change:");
    for (i = 0; i < 16; i++)
	printf(" %u", st->UBS_fcn[i]);
    printf("\n");
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
    CHECK(!Find("sound   "));

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
