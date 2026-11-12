/*
 * test_kbd.c -- the C keyboard driver (Driver/Keyboard) in the C kernel.
 *
 * The boot loads kbd.geo (US) as "[keyboard] driver" from
 * SP_KEYBOARD_DRIVERS, the default keyboard driver; the test loads
 * kbde_gr.geo (German extended) itself.  It makes a queue their output, posts HID key events through
 * ghal_input and reads the scan codes (MSG_IM_KBD_SCAN) from the queue.
 * Each scan code goes through DR_KBD_XLATE_SCAN, as the input manager
 * does it:
 *   - US: letters, <Shift>, <Caps Lock>, <Ctrl>, the cursor keys (0xe0),
 *     the keypad with and without <Num Lock>, <Pause> (0xe1), the state
 *     returned by DR_KBD_GET_KBD_STATE;
 *   - the input manager: with the output the IM's thread, the driver's
 *     monitor turns the scan codes into MSG_META_KBD_CHAR, which the
 *     IM sends to its output (ImGrabInput);
 *   - German with keyboardAltGr: z and y swapped, the dead accent (the
 *     acute and e give e acute), <Alt Gr>-q gives @.
 */
#include "kernelC.h"
#include "ghal.h"
#include <geosdriver.h>
#include <Internal/kbdDr.h>
#include <Internal/im.h>
#include <initfile.h>
#include <input.h>
#include <char.h>
#include <timer.h>

#include <stdio.h>
#include <string.h>

extern const unsigned char kbd_geode[], kbde_gr_geode[];
extern const unsigned long kbd_geode_size, kbde_gr_geode_size;

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static QueueHandle q;
static DriverInfoStruct *dis;

static char top[260], sep = '/';

/* a file of the top, written by the host before the boot */
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

/* the next scan code in the queue; 0 if none */
static word NextScan(void)
{
    EventHandle ev;
    const byte *p;
    dword size;
    Message m;
    word w;

    if (!GeodeInfoQueue(q))
	return 0;
    ev = QueueGetMessage(q);
    m = ObjGetMessageInfo(ev, NULL);
    p = KernelEventParams(ev, &size);
    w = size >= 2 ? *(const word *) p : 0xffff;
    ObjFreeMessage(ev);
    CHECK(m == MSG_IM_KBD_SCAN);
    return w;
}

typedef struct {
    word scan;		/* as the queue gave it */
    Boolean data;	/* a character */
    word ch;		/* <CharacterSet><char> */
    byte flags;		/* CharFlags */
    byte shift;		/* ShiftState */
    byte toggle;	/* ToggleState */
} Key;

/* DR_KBD_XLATE_SCAN of the next scan code */
static Key Xlate(void)
{
    DriverRegs r;
    Key k;

    memset(&r, 0, sizeof(r));
    memset(&k, 0, sizeof(k));
    k.scan = NextScan();
    r.DR_cx = k.scan;
    CHECK(!DriverCall(dis, DR_KBD_XLATE_SCAN, &r));
    k.data = (r.DR_ax & 0xff) != 0;
    if (k.data)
	CHECK(r.DR_di == IM_META_KBD_CHAR);
    k.ch = (word) r.DR_cx;
    k.flags = (byte) r.DR_dx;
    k.shift = (byte) (r.DR_dx >> 8);
    k.toggle = (byte) r.DR_bp;
    return k;
}

/* post a HID key event */
static void Hid(word usage, int down)
{
    ghal_input_event e;

    memset(&e, 0, sizeof(e));
    e.page = GHAL_HID_PAGE_KEYBOARD;
    e.usage = usage;
    e.value = down;
    e.source = GHAL_INPUT_SOURCE_KEYBOARD;
    CHECK(ghal_input_post(&e, 1) == GHAL_OK);
}

/* a key pressed and released: the character of the press */
static Key Tap(word usage)
{
    Key k, up;

    Hid(usage, 1);
    Hid(usage, 0);
    k = Xlate();
    up = Xlate();
    CHECK(k.flags & CF_FIRST_PRESS);
    CHECK(up.flags & CF_RELEASE);
    CHECK((up.scan & 0xff) == ((k.scan & 0xff) | 0x80));
    return k;
}

/* HID usages */
#define H_A	0x04
#define H_E	0x08
#define H_Q	0x14
#define H_Y	0x1c
#define H_Z	0x1d
#define H_1	0x1e
#define H_EQUAL	0x2e	/* the acute on a German keyboard */
#define H_CAPS	0x39
#define H_PAUSE	0x48
#define H_DEL	0x4c
#define H_UP	0x52
#define H_NUM	0x53
#define H_KP8	0x60
#define H_LCTRL	0xe0
#define H_LSHIFT 0xe1
#define H_RALT	0xe6

static GeodeHandle Load(const char *file)
{
    GeodeLoadError e = GLE_OK;
    GeodeHandle drv;
    DriverRegs r;

    if (file) {
	FileSetStandardPath(SP_KEYBOARD_DRIVERS);
	drv = GeodeUseDriver(file, 0, 0, &e);
    } else
	drv = GeodeGetDefaultDriver(GDDT_KEYBOARD);

    CHECK(drv != 0);
    if (!drv) {
	printf("%s: GeodeLoadError %d\n", file ? file : "boot", (int) e);
	return 0;
    }
    dis = GeodeInfoDriver(drv);
    CHECK(dis && dis->DIS_driverType == DRIVER_TYPE_INPUT);
    memset(&r, 0, sizeof(r));
    r.DR_bx = q;
    CHECK(!DriverCall(dis, DR_KBD_CHANGE_OUTPUT, &r));
    CHECK(r.DR_bx == ImInfoInputProcess());	/* the IM's thread */
    return drv;
}

static void TestUS(void)
{
    GeodeHandle drv = Load(NULL);	/* the boot's */
    DriverRegs r;
    Key k;

    if (!drv)
	return;
    k = Tap(H_A);
    CHECK(k.scan == 0x1e && k.data && k.ch == 'a' && !k.shift);
    k = Tap(H_1);
    CHECK(k.scan == 0x02 && k.ch == '1');

    /* <Shift>: a modifier, then A without <Shift> in the state */
    Hid(H_LSHIFT, 1);
    k = Xlate();
    CHECK(k.scan == 0x2a && (k.flags & CF_STATE_KEY));
    k = Tap(H_A);
    CHECK(k.ch == 'A' && !(k.shift & SS_LSHIFT));
    k = Tap(H_1);
    CHECK(k.ch == '!');
    Hid(H_LSHIFT, 0);
    k = Xlate();
    CHECK(k.scan == 0xaa && (k.flags & CF_RELEASE));
    CHECK(Tap(H_A).ch == 'a');

    /* <Caps Lock> */
    k = Tap(H_CAPS);
    CHECK(k.toggle & TS_CAPSLOCK);
    k = Tap(H_A);
    CHECK(k.ch == 'A' && (k.toggle & TS_CAPSLOCK));
    CHECK(Tap(H_1).ch == '1');
    Tap(H_CAPS);
    CHECK(Tap(H_A).ch == 'a');

    /* <Ctrl>-a: the character a with <Ctrl> in the state */
    Hid(H_LCTRL, 1);
    Xlate();
    k = Tap(H_A);
    CHECK(k.ch == 'a' && (k.shift & SS_LCTRL));
    memset(&r, 0, sizeof(r));
    CHECK(!DriverCall(dis, DR_KBD_GET_KBD_STATE, &r));
    CHECK((r.DR_ax & 0xff) == SS_LCTRL && r.DR_bx == q);
    Hid(H_LCTRL, 0);
    Xlate();

    /* the cursor keys of the extended keyboard and <Del> */
    k = Tap(H_UP);
    CHECK(k.scan == 0xe048 && k.ch == ((CS_CONTROL << 8) | VC_UP) &&
	  (k.flags & CF_EXTENDED));
    k = Tap(H_DEL);
    CHECK(k.scan == 0xe053 && k.ch == ((CS_CONTROL << 8) | VC_DEL));

    /* the keypad: a cursor key, a digit with <Num Lock> */
    k = Tap(H_KP8);
    CHECK(k.scan == 0x48 && k.ch == ((CS_CONTROL << 8) | VC_UP));
    k = Tap(H_NUM);
    CHECK(k.toggle & TS_NUMLOCK);
    k = Tap(H_KP8);
    CHECK((k.ch & 0xff) == '8');
    Tap(H_NUM);

    /* <Pause>: e1 1d 45 e1 9d c5, nothing on the release */
    Hid(H_PAUSE, 1);
    Hid(H_PAUSE, 0);
    CHECK(NextScan() == 0xe11d);
    CHECK(NextScan() == 0x0045);
    CHECK(NextScan() == 0xe19d);
    CHECK(NextScan() == 0x00c5);
    CHECK(!GeodeInfoQueue(q));

    /* no output: no scan codes */
    memset(&r, 0, sizeof(r));
    r.DR_bx = 0xffff;
    CHECK(!DriverCall(dis, DR_KBD_CHANGE_OUTPUT, &r) && r.DR_bx == q);
    Hid(H_A, 1);
    Hid(H_A, 0);
    CHECK(!GeodeInfoQueue(q));
    memset(&r, 0, sizeof(r));
    r.DR_bx = ImInfoInputProcess();	/* back to the IM */
    DriverCall(dis, DR_KBD_CHANGE_OUTPUT, &r);
}

/* the next event of the queue, waiting for the IM's thread */
static Message NextEvent(word *cx, word *dx)
{
    EventHandle ev;
    const word *p;
    dword size;
    Message m;
    int n;

    for (n = 0; n < 200 && !GeodeInfoQueue(q); n++)
	TimerSleep(1);
    if (!GeodeInfoQueue(q))
	return 0;
    ev = QueueGetMessage(q);
    m = ObjGetMessageInfo(ev, NULL);
    p = KernelEventParams(ev, &size);
    *cx = size >= 2 ? p[0] : 0;
    *dx = size >= 4 ? p[1] : 0;
    ObjFreeMessage(ev);
    return m;
}

/* the whole way: HID event, scan code to the IM, the driver's monitor,
 * MSG_META_KBD_CHAR to the output of the IM */
static void TestIM(void)
{
    word cx, dx;

    CHECK(ImGrabInput(ConstructOptr(q, 0)));
    CHECK(!ImGrabInput(ConstructOptr(q, 1)));	/* taken */
    Hid(H_A, 1);
    Hid(H_A, 0);
    CHECK(NextEvent(&cx, &dx) == IM_META_KBD_CHAR && cx == 'a' &&
	  (dx & CF_FIRST_PRESS));
    CHECK(NextEvent(&cx, &dx) == IM_META_KBD_CHAR && cx == 'a' &&
	  (dx & CF_RELEASE));
    Hid(H_LSHIFT, 1);
    Hid(H_1, 1);
    Hid(H_1, 0);
    Hid(H_LSHIFT, 0);
    CHECK(NextEvent(&cx, &dx) == IM_META_KBD_CHAR && (dx & CF_STATE_KEY));
    CHECK(NextEvent(&cx, &dx) == IM_META_KBD_CHAR && cx == '!');
    CHECK(NextEvent(&cx, &dx) == IM_META_KBD_CHAR && cx == '!');
    CHECK(NextEvent(&cx, &dx) == IM_META_KBD_CHAR && (dx & CF_RELEASE));
    ImReleaseInput(ConstructOptr(q, 0));
    Hid(H_A, 1);
    Hid(H_A, 0);
    TimerSleep(5);
    CHECK(!GeodeInfoQueue(q));		/* no output */
}

static void TestGerman(void)
{
    GeodeHandle drv;
    Key k;

    InitFileWriteBoolean("keyboard", "keyboardAltGr", TRUE);
    drv = Load("kbde_gr.geo");
    if (!drv)
	return;
    /* z and y change places */
    k = Tap(H_Z);
    CHECK(k.scan == 0x2c && k.ch == 'y');
    k = Tap(H_Y);
    CHECK(k.scan == 0x15 && k.ch == 'z');

    /* the dead acute, then e: e acute */
    k = Tap(H_EQUAL);
    CHECK(k.scan == 0x0d && (k.flags & CF_TEMP_ACCENT));
    k = Tap(H_E);
    CHECK(k.ch == C_LE_ACUTE);
    CHECK(Tap(H_E).ch == 'e');

    /* <Alt Gr>-q: @ */
    Hid(H_RALT, 1);
    k = Xlate();
    CHECK(k.scan == 0xe038 && (k.flags & CF_STATE_KEY));
    k = Tap(H_Q);
    CHECK(k.ch == C_AT_SIGN);
    Hid(H_RALT, 0);
    Xlate();
    CHECK(Tap(H_Q).ch == 'q');
    GeodeFreeDriver(drv);
    InitFileDeleteEntry("keyboard", "keyboardAltGr");
}

int main(int argc, char **argv)
{
    char *slash;
    GeodeLoadError e;
    static const char ini[] = "[keyboard]\r\ndriver = kbd.geo\r\n";

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    strncpy(top, argv[0], sizeof(top) - 8);
    top[sizeof(top) - 8] = 0;
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash) {
	sep = *slash;
	strcpy(slash + 1, "GKBTOP");
    } else
	strcpy(top, "GKBTOP");
    ghal_fs_mkdir(top);
    Put("SYSTEM", NULL, 0);
    Put("SYSTEM/KBD", NULL, 0);
    Put("SYSTEM/KBD/kbd.geo", kbd_geode, kbd_geode_size);
    Put("SYSTEM/KBD/kbde_gr.geo", kbde_gr_geode, kbde_gr_geode_size);
    Put("geos.ini", ini, sizeof(ini) - 1);
    CHECK(KernelBoot(top, "", &e) == 0 && e == GLE_OK);
    CHECK(GeodeGetDefaultDriver(GDDT_KEYBOARD) != 0);
    q = GeodeAllocQueue();
    TestUS();
    TestGerman();
    TestIM();
    GeodeFreeQueue(q);
    printf("%s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
