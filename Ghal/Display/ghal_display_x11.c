/*
 * ghal_display_x11.c -- ghal_display on X11 (Linux, and any host with an
 * X server).
 *
 * The surface is an XImage, in shared memory (MIT-SHM) when the server
 * is local, else in the client.  Flushing puts the rectangle into a
 * window: a window of the requested size, or one that covers the
 * screen (GHAL_DISPLAY_FULLSCREEN: _NET_WM_STATE_FULLSCREEN for a window
 * manager, and placed at 0,0 with the screen's size for a server
 * without one, such as Xvnc or Xvfb).
 *
 * A host thread serves the events: Expose repaints from the surface, the
 * window manager's close button sets close_requested, and keys, buttons
 * and the pointer go to ghal_input (ghal_input.c) as HID usages.  Key
 * codes are the evdev ones (Linux KEY_* + 8), which every current X
 * server uses; a key the server repeats comes as presses with
 * GHAL_INPUT_REPEAT (detectable auto repeat), its character from the
 * keysym.  Xlib is used from that thread and from the callers' threads
 * (XInitThreads, and the display lock around each use).
 *
 * The server must have a 24-bit TrueColor visual with the masks
 * 0xff0000, 0xff00, 0xff, as every current server has.
 */
#include "ghal.h"
#include "../Common/ghal_hid.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static Display *dpy;
static Window win;
static GC gc;
static XImage *img;
static XShmSegmentInfo shm;
static int useShm;
static Atom wmDelete;
static pthread_t thread;
static volatile int stop, closeRequested;
static ghal_surface surf;

static unsigned char keyDown[256];

/* the character of a keysym: Latin-1 and Unicode keysyms, and the
 * keys that type control characters */
static uint32_t KeysymText(KeySym ks)
{
    if ((ks >= 0x20 && ks <= 0x7e) || (ks >= 0xa0 && ks <= 0xff))
        return (uint32_t) ks;
    if ((ks & 0xff000000) == 0x01000000)
        return (uint32_t) (ks & 0xffffff);
    switch (ks) {
    case XK_BackSpace: return 8;
    case XK_Tab: case XK_KP_Tab: return 9;
    case XK_Return: case XK_KP_Enter: return 13;
    case XK_Escape: return 27;
    case XK_Delete: return 127;
    default: break;
    }
    if (ks >= XK_KP_0 && ks <= XK_KP_9)
        return (uint32_t) ('0' + (ks - XK_KP_0));
    switch (ks) {
    case XK_KP_Decimal: return '.';
    case XK_KP_Add: return '+';
    case XK_KP_Subtract: return '-';
    case XK_KP_Multiply: return '*';
    case XK_KP_Divide: return '/';
    case XK_KP_Space: return ' ';
    case XK_KP_Equal: return '=';
    default: return 0;
    }
}

static void Key(XKeyEvent *k, int down)
{
    ghal_input_event e;
    KeySym ks = NoSymbol;
    char buf[8];

    memset(&e, 0, sizeof(e));
    e.page = GHAL_HID_PAGE_KEYBOARD;
    e.usage = k->keycode >= 8 ? ghal_hid_from_linux_key(k->keycode - 8) : 0;
    if (!e.usage)
        return;
    e.source = GHAL_INPUT_SOURCE_KEYBOARD;
    e.value = down;
    if (down) {
        if (keyDown[k->keycode & 0xff])
            e.flags = GHAL_INPUT_REPEAT;
        XLookupString(k, buf, sizeof(buf), &ks, NULL);
        e.text = KeysymText(ks);
    }
    keyDown[k->keycode & 0xff] = (unsigned char) down;
    ghal_input_post(&e, 1);
}

static void Button(XButtonEvent *b, int down)
{
    static const uint16_t hidButton[4] = { 0, 1, 3, 2 };
    ghal_input_event e;

    memset(&e, 0, sizeof(e));
    e.source = GHAL_INPUT_SOURCE_POINTER;
    if (b->button >= 1 && b->button <= 3) {
        e.page = GHAL_HID_PAGE_BUTTON;
        e.usage = hidButton[b->button];
        e.value = down;
    } else if (!down) {
        return;                 /* wheels: one step a press */
    } else if (b->button == 4 || b->button == 5) {
        e.page = GHAL_HID_PAGE_DESKTOP;
        e.usage = GHAL_HID_WHEEL;
        e.value = b->button == 4 ? 1 : -1;
    } else if (b->button == 6 || b->button == 7) {
        e.page = GHAL_HID_PAGE_CONSUMER;
        e.usage = GHAL_HID_PAN;
        e.value = b->button == 6 ? -1 : 1;
    } else {
        e.page = GHAL_HID_PAGE_BUTTON;
        e.usage = (uint16_t) (b->button - 4);   /* 8, 9: buttons 4, 5 */
        e.value = 1;
        ghal_input_post(&e, 1);
        e.value = 0;
    }
    ghal_input_post(&e, 1);
}

static void Motion(int x, int y)
{
    ghal_input_event e[2];

    memset(e, 0, sizeof(e));
    e[0].page = e[1].page = GHAL_HID_PAGE_DESKTOP;
    e[0].usage = GHAL_HID_X;
    e[1].usage = GHAL_HID_Y;
    e[0].value = x;
    e[1].value = y;
    e[0].source = e[1].source = GHAL_INPUT_SOURCE_POINTER;
    e[0].flags = e[1].flags = GHAL_INPUT_ABSOLUTE;
    ghal_input_post(e, 2);
}

static int (*oldHandler)(Display *, XErrorEvent *);
static volatile int shmFailed;

static int ShmErrorHandler(Display *d, XErrorEvent *e)
{
    (void) d; (void) e;
    shmFailed = 1;
    return 0;
}

static int ErrorHandler(Display *d, XErrorEvent *e)
{
    char text[128];

    XGetErrorText(d, e->error_code, text, sizeof(text));
    ghal_log(GHAL_LOG_ERROR, "ghal_display_x11: X error %s (request %d)\n", text,
             e->request_code);
    return 0;
}

static int WantFullscreen(unsigned flags)
{
    const char *env = getenv("GEOS_DISPLAY");

    if (env && !strcmp(env, "fullscreen"))
        return 1;
    if (env && !strcmp(env, "window"))
        return 0;
    return (flags & GHAL_DISPLAY_FULLSCREEN) != 0;
}

static void Put(int x, int y, int w, int h)
{
    if (useShm)
        XShmPutImage(dpy, win, gc, img, x, y, x, y, (unsigned) w,
                     (unsigned) h, False);
    else
        XPutImage(dpy, win, gc, img, x, y, x, y, (unsigned) w, (unsigned) h);
}

static void *EventThread(void *arg)
{
    struct pollfd p;
    XEvent ev;

    (void) arg;
    p.fd = ConnectionNumber(dpy);
    p.events = POLLIN;
    while (!stop) {
        XLockDisplay(dpy);
        while (XPending(dpy)) {
            XNextEvent(dpy, &ev);
            switch (ev.type) {
            case Expose:
                Put(ev.xexpose.x, ev.xexpose.y, ev.xexpose.width,
                    ev.xexpose.height);
                break;
            case KeyPress:
            case KeyRelease:
                Key(&ev.xkey, ev.type == KeyPress);
                break;
            case ButtonPress:
            case ButtonRelease:
                Button(&ev.xbutton, ev.type == ButtonPress);
                break;
            case MotionNotify:
                Motion(ev.xmotion.x, ev.xmotion.y);
                break;
            case FocusOut:
                /* keys held when the focus goes are released */
                {
                    XKeyEvent k;
                    unsigned c;
                    memset(&k, 0, sizeof(k));
                    k.display = dpy;
                    for (c = 8; c < 256; c++)
                        if (keyDown[c]) {
                            k.keycode = c;
                            Key(&k, 0);
                        }
                }
                break;
            case ClientMessage:
                if ((Atom) ev.xclient.data.l[0] == wmDelete)
                    closeRequested = 1;
                break;
            default:
                break;
            }
        }
        XFlush(dpy);
        XUnlockDisplay(dpy);
        poll(&p, 1, 50);
    }
    return NULL;
}

static int MakeImage(Visual *vis, unsigned w, unsigned h)
{
    useShm = 0;
    if (XShmQueryExtension(dpy) && !getenv("GHAL_X11_NO_SHM")) {
        img = XShmCreateImage(dpy, vis, 24, ZPixmap, NULL, &shm, w, h);
        if (img) {
            shm.shmid = shmget(IPC_PRIVATE,
                               (size_t) img->bytes_per_line * img->height,
                               IPC_CREAT | 0600);
            if (shm.shmid >= 0) {
                shm.shmaddr = img->data = shmat(shm.shmid, NULL, 0);
                shm.readOnly = False;
                shmFailed = 0;
                oldHandler = XSetErrorHandler(ShmErrorHandler);
                XShmAttach(dpy, &shm);
                XSync(dpy, False);
                XSetErrorHandler(oldHandler);
                /* gone once both sides detach */
                shmctl(shm.shmid, IPC_RMID, NULL);
                if (!shmFailed && img->data != (char *) -1) {
                    useShm = 1;
                    return 1;
                }
                if (img->data != (char *) -1)
                    shmdt(shm.shmaddr);
            }
            img->data = NULL;
            XDestroyImage(img);
            img = NULL;
        }
    }
    img = XCreateImage(dpy, vis, 24, ZPixmap, 0, NULL, w, h, 32, 0);
    if (!img)
        return 0;
    img->data = calloc((size_t) img->bytes_per_line, h);
    if (!img->data) {
        XDestroyImage(img);
        img = NULL;
        return 0;
    }
    return 1;
}

ghal_status ghal_display_open(unsigned width, unsigned height,
                              unsigned flags, ghal_surface *out)
{
    XSetWindowAttributes wa;
    XSizeHints hints;
    XVisualInfo vi;
    XEvent ev;
    int scr, full;
    Atom state, fullAtom;

    if (dpy || !out)
        return GHAL_ERR_INVAL;
    XInitThreads();
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        ghal_log(GHAL_LOG_ERROR, "ghal_display_x11: cannot open the X display\n");
        return GHAL_ERR_HOST;
    }
    XSetErrorHandler(ErrorHandler);
    scr = DefaultScreen(dpy);
    if (!XMatchVisualInfo(dpy, scr, 24, TrueColor, &vi) ||
        vi.red_mask != 0xff0000 || vi.green_mask != 0xff00 ||
        vi.blue_mask != 0xff) {
        ghal_log(GHAL_LOG_ERROR, "ghal_display_x11: no 24-bit RGB visual\n");
        XCloseDisplay(dpy);
        dpy = NULL;
        return GHAL_ERR_UNSUPPORTED;
    }
    full = WantFullscreen(flags);
    if (full) {
        width = (unsigned) DisplayWidth(dpy, scr);
        height = (unsigned) DisplayHeight(dpy, scr);
    }
    if (!width || !height || width > 0xffff || height > 0xffff ||
        !MakeImage(vi.visual, width, height)) {
        XCloseDisplay(dpy);
        dpy = NULL;
        return width && height ? GHAL_ERR_NOMEM : GHAL_ERR_INVAL;
    }

    memset(&wa, 0, sizeof(wa));
    wa.colormap = XCreateColormap(dpy, RootWindow(dpy, scr), vi.visual,
                                  AllocNone);
    wa.background_pixel = 0;
    wa.border_pixel = 0;
    wa.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask |
                    KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                    PointerMotionMask | FocusChangeMask;
    win = XCreateWindow(dpy, RootWindow(dpy, scr), 0, 0, width, height, 0,
                        24, InputOutput, vi.visual,
                        CWColormap | CWBackPixel | CWBorderPixel |
                        CWEventMask, &wa);
    XStoreName(dpy, win, "GEOS");
    memset(&hints, 0, sizeof(hints));
    hints.flags = PPosition | PMinSize | PMaxSize;
    hints.min_width = hints.max_width = (int) width;
    hints.min_height = hints.max_height = (int) height;
    XSetWMNormalHints(dpy, win, &hints);
    wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wmDelete, 1);
    if (full) {
        state = XInternAtom(dpy, "_NET_WM_STATE", False);
        fullAtom = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
        XChangeProperty(dpy, win, state, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *) &fullAtom, 1);
    }
    gc = XCreateGC(dpy, win, 0, NULL);
    XMapWindow(dpy, win);
    do
        XNextEvent(dpy, &ev);
    while (ev.type != MapNotify);
    if (full)                   /* a server without a window manager */
        XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
    XkbSetDetectableAutoRepeat(dpy, True, NULL);
    memset(keyDown, 0, sizeof(keyDown));

    surf.pixels = (uint32_t *) img->data;
    surf.pitch = (uint32_t) img->bytes_per_line;
    surf.width = (uint16_t) width;
    surf.height = (uint16_t) height;
    *out = surf;
    stop = closeRequested = 0;
    if (pthread_create(&thread, NULL, EventThread, NULL)) {
        ghal_display_close();
        return GHAL_ERR_HOST;
    }
    return GHAL_OK;
}

void ghal_display_flush(int x, int y, int w, int h)
{
    if (!dpy)
        return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > surf.width)
        w = surf.width - x;
    if (y + h > surf.height)
        h = surf.height - y;
    if (w <= 0 || h <= 0)
        return;
    XLockDisplay(dpy);
    Put(x, y, w, h);
    XFlush(dpy);
    XUnlockDisplay(dpy);
}

void ghal_display_close(void)
{
    if (!dpy)
        return;
    if (img) {
        stop = 1;
        pthread_join(thread, NULL);
    }
    if (useShm) {
        XShmDetach(dpy, &shm);
        XSync(dpy, False);
        shmdt(shm.shmaddr);
        img->data = NULL;
    }
    if (img)
        XDestroyImage(img);
    img = NULL;
    if (win)
        XDestroyWindow(dpy, win);
    win = 0;
    XCloseDisplay(dpy);
    dpy = NULL;
    memset(&surf, 0, sizeof(surf));
}

int ghal_display_close_requested(void)
{
    return closeRequested;
}

uint32_t ghal_display_read_back(int x, int y)
{
    XImage *one;
    uint32_t v;

    if (!dpy || x < 0 || y < 0 || x >= surf.width || y >= surf.height)
        return 0;
    XLockDisplay(dpy);
    XSync(dpy, False);
    one = XGetImage(dpy, win, x, y, 1, 1, AllPlanes, ZPixmap);
    v = one ? (uint32_t) XGetPixel(one, 0, 0) & 0xffffff : 0;
    if (one)
        XDestroyImage(one);
    XUnlockDisplay(dpy);
    return v;
}

const char *ghal_display_backend(void)
{
    return "x11";
}
