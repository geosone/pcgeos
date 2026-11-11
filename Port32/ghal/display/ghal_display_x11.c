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
 * window manager's close button sets close_requested.  Xlib is used from
 * that thread and from the callers' threads (XInitThreads, and the
 * display lock around each use).
 *
 * The server must have a 24-bit TrueColor visual with the masks
 * 0xff0000, 0xff00, 0xff, as every current server has.
 */
#include "ghal.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
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
    wa.event_mask = ExposureMask | StructureNotifyMask;
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
