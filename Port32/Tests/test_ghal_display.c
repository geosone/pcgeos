/*
 * test_ghal_display.c -- ghal_display (ghal.h): open, draw, flush, and
 * read the pixels back as the host shows them.
 *
 * With the x11 backend, ctest runs it on an X server of its own
 * (Port32/tools/geos-xserver.sh, Xvfb), full screen, so the desktop is
 * not touched.  To look at it:
 *     GHAL_DISPLAY_HOLD=10 Port32/tools/geos-xserver.sh -vnc -view \
 *         build/Port32/Tests/test_ghal_display
 * or, in a window on the desktop:
 *     GEOS_DISPLAY=window GHAL_DISPLAY_HOLD=10 build/.../test_ghal_display
 */
#include "ghal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    if (hold) {
        /* until the time is up or the window is closed */
        uint64_t end = ghal_time_monotonic_ns() +
                       (uint64_t) atoi(hold) * 1000000000u;
        while (ghal_time_monotonic_ns() < end &&
               !ghal_display_close_requested())
            ;
    }
    ghal_display_close();
    CHECK(ghal_display_open(320, 200, 0, &s) == GHAL_OK);
    ghal_display_close();

    printf("%s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
