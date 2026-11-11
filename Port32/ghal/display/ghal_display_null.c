/*
 * ghal_display_null.c -- ghal_display without a screen: the surface is
 * memory that nobody shows.  For hosts without a display backend yet
 * (dos32 until VESA, Linux builds without X11) and for tests.
 */
#include "ghal.h"

#include <stdlib.h>

static ghal_surface surf;

ghal_status ghal_display_open(unsigned width, unsigned height,
                              unsigned flags, ghal_surface *out)
{
    (void) flags;
    if (surf.pixels || !width || !height || width > 0xffff ||
        height > 0xffff || !out)
        return GHAL_ERR_INVAL;
    surf.pixels = calloc((size_t) width * height, sizeof(uint32_t));
    if (!surf.pixels)
        return GHAL_ERR_NOMEM;
    surf.pitch = width * sizeof(uint32_t);
    surf.width = (uint16_t) width;
    surf.height = (uint16_t) height;
    *out = surf;
    return GHAL_OK;
}

void ghal_display_flush(int x, int y, int w, int h)
{
    (void) x; (void) y; (void) w; (void) h;
}

void ghal_display_close(void)
{
    free(surf.pixels);
    surf.pixels = NULL;
}

int ghal_display_close_requested(void)
{
    return 0;
}

uint32_t ghal_display_read_back(int x, int y)
{
    if (!surf.pixels || x < 0 || y < 0 || x >= surf.width || y >= surf.height)
        return 0;
    return surf.pixels[(size_t) y * (surf.pitch / 4) + x];
}

const char *ghal_display_backend(void)
{
    return "null";
}
