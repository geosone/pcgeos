/*
 * test_ghal_display.c -- ghal_display (ghal.h): open, draw, flush, and
 * read the pixels back as the host shows them; with X11, the window's
 * key, button and pointer events as ghal_input events.
 *
 * With the x11 backend, ctest runs it on an X server of its own
 * (Port32/tools/geos-xserver.sh, Xvfb), full screen, so the desktop is
 * not touched.  To look at it:
 *     GHAL_DISPLAY_HOLD=10 Port32/tools/geos-xserver.sh -vnc -view \
 *         build/Port32/Tests/test_ghal_display
 * (what you type and click there is printed) or, in a window on the
 * desktop:
 *     GEOS_DISPLAY=window GHAL_DISPLAY_HOLD=10 build/.../test_ghal_display
 */
#include "ghal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef GHAL_TEST_X11
#include <X11/Xlib.h>
#endif

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static uint32_t Pattern(int x, int y, int w, int h)
{
    /* eight colour bars over a gradient, a white frame */
    static const uint32_t bars[8] = {
        0xffffff, 0xffff00, 0x00ffff, 0x00ff00,
        0xff00ff, 0xff0000, 0x0000ff, 0x000000
    };
    if (x < 4 || y < 4 || x >= w - 4 || y >= h - 4)
        return 0xffffff;
    if (y < h * 2 / 3)
        return bars[x * 8 / w];
    return (uint32_t) (x * 255 / w) * 0x010101;
}

#ifdef GHAL_TEST_X11
/* Events as the server would send them, from a second connection to the
 * window named GEOS; then what ghal_input makes of them. */
static int Next(ghal_input_event *e)
{
    uint64_t end = ghal_time_monotonic_ns() + 2000000000u;

    while (ghal_time_monotonic_ns() < end)
        if (ghal_input_read(GHAL_INPUT_ALL, e))
            return 1;
    return 0;
}

static void X11Input(void)
{
    Display *d = XOpenDisplay(NULL);
    Window root, parent, *kids = NULL, win = 0;
    unsigned n, i;
    char *name;
    XEvent ev;
    ghal_input_event e;

    CHECK(d != NULL);
    if (!d)
        return;
    XQueryTree(d, DefaultRootWindow(d), &root, &parent, &kids, &n);
    for (i = 0; i < n; i++)
        if (XFetchName(d, kids[i], &name)) {
            if (!strcmp(name, "GEOS"))
                win = kids[i];
            XFree(name);
        }
    if (kids)
        XFree(kids);
    CHECK(win != 0);

    memset(&ev, 0, sizeof(ev));
    ev.xkey.type = KeyPress;
    ev.xkey.display = d;
    ev.xkey.window = win;
    ev.xkey.root = DefaultRootWindow(d);
    ev.xkey.keycode = 30 + 8;           /* KEY_A */
    ev.xkey.same_screen = True;
    XSendEvent(d, win, False, KeyPressMask, &ev);
    XSendEvent(d, win, False, KeyPressMask, &ev);       /* repeat */
    ev.xkey.type = KeyRelease;
    XSendEvent(d, win, False, KeyReleaseMask, &ev);
    ev.xkey.type = KeyPress;
    ev.xkey.keycode = 1 + 8;            /* KEY_ESC */
    XSendEvent(d, win, False, KeyPressMask, &ev);

    memset(&ev, 0, sizeof(ev));
    ev.xbutton.type = ButtonPress;
    ev.xbutton.window = win;
    ev.xbutton.button = 3;
    ev.xbutton.same_screen = True;
    XSendEvent(d, win, False, ButtonPressMask, &ev);
    ev.xbutton.button = 5;
    XSendEvent(d, win, False, ButtonPressMask, &ev);
    memset(&ev, 0, sizeof(ev));
    ev.xmotion.type = MotionNotify;
    ev.xmotion.window = win;
    ev.xmotion.x = 100;
    ev.xmotion.y = 50;
    ev.xmotion.same_screen = True;
    XSendEvent(d, win, False, PointerMotionMask, &ev);
    XFlush(d);

    CHECK(Next(&e) && e.page == GHAL_HID_PAGE_KEYBOARD && e.usage == 0x04 &&
          e.value == 1 && !e.flags && e.text == 'a' &&
          e.source == GHAL_INPUT_SOURCE_KEYBOARD);
    CHECK(Next(&e) && e.usage == 0x04 && e.value == 1 &&
          e.flags == GHAL_INPUT_REPEAT);
    CHECK(Next(&e) && e.usage == 0x04 && e.value == 0);
    CHECK(Next(&e) && e.usage == 0x29 && e.value == 1 && e.text == 27);
    CHECK(Next(&e) && e.page == GHAL_HID_PAGE_BUTTON && e.usage == 2 &&
          e.value == 1 && e.source == GHAL_INPUT_SOURCE_POINTER);
    CHECK(Next(&e) && e.page == GHAL_HID_PAGE_DESKTOP &&
          e.usage == GHAL_HID_WHEEL && e.value == -1);
    CHECK(Next(&e) && e.usage == GHAL_HID_X && e.value == 100 &&
          e.flags == GHAL_INPUT_ABSOLUTE);
    CHECK(Next(&e) && e.usage == GHAL_HID_Y && e.value == 50);
    XCloseDisplay(d);
}
#endif

int main(void)
{
    ghal_surface s;
    const char *hold = getenv("GHAL_DISPLAY_HOLD");
    int x, y;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (ghal_display_open(640, 480, GHAL_DISPLAY_FULLSCREEN, &s) != GHAL_OK) {
        printf("ghal_display_open failed (%s)\n", ghal_display_backend());
        return 1;
    }
    printf("display %s, %ux%u\n", ghal_display_backend(), s.width, s.height);
    CHECK(s.width >= 320 && s.height >= 200 && s.pitch >= s.width * 4u);
    CHECK(ghal_display_open(640, 480, 0, &s) == GHAL_ERR_INVAL);

    for (y = 0; y < s.height; y++)
        for (x = 0; x < s.width; x++)
            s.pixels[(size_t) y * (s.pitch / 4) + x] =
                Pattern(x, y, s.width, s.height);
    ghal_display_flush(0, 0, s.width, s.height);
    CHECK(ghal_display_read_back(0, 0) == 0xffffff);
    CHECK(ghal_display_read_back(s.width / 16 + 8, s.height / 3) == 0xffffff);
    CHECK(ghal_display_read_back(s.width * 11 / 16, s.height / 3) == 0xff0000);
    CHECK(ghal_display_read_back(s.width * 13 / 16, s.height / 3) == 0x0000ff);

    /* a flushed rectangle shows; the rest keeps the old pixels */
    for (y = 10; y < 30; y++)
        for (x = 10; x < 30; x++)
            s.pixels[(size_t) y * (s.pitch / 4) + x] = 0x123456;
    s.pixels[(size_t) 40 * (s.pitch / 4) + 40] = 0x654321;
    ghal_display_flush(10, 10, 20, 20);
    ghal_display_flush(-5, -5, 2, 2);		/* clipped away */
    CHECK(ghal_display_read_back(20, 20) == 0x123456);
    if (strcmp(ghal_display_backend(), "null"))
        CHECK(ghal_display_read_back(40, 40) == Pattern(40, 40, s.width,
                                                        s.height));

#ifdef GHAL_TEST_X11
    if (!strcmp(ghal_display_backend(), "x11"))
        X11Input();
#endif
    if (hold) {
        /* until the time is up or the window is closed */
        uint64_t end = ghal_time_monotonic_ns() +
                       (uint64_t) atoi(hold) * 1000000000u;
        ghal_input_event e;
        while (ghal_time_monotonic_ns() < end &&
               !ghal_display_close_requested())
            while (ghal_input_read(GHAL_INPUT_ALL, &e))
                printf("input page %02x usage %02x value %d flags %u "
                       "text %u\n", e.page, e.usage, (int) e.value,
                       e.flags, (unsigned) e.text);
    }
    ghal_display_close();
    CHECK(ghal_display_open(320, 200, 0, &s) == GHAL_OK);
    ghal_display_close();

    printf("%s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
