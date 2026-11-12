/*
 * host.h -- inside the host video driver (hostVideo.c, hostPointer.c).
 */
#ifndef HOST_H
#define HOST_H

#include "vidcom.h"
#include <ghal.h>

extern ghal_surface hostSurf;

#define HostPixel(x, y) \
    (hostSurf.pixels + (size_t) (y) * (hostSurf.pitch / 4) + (x))

/* a rectangle changed (inclusive, within the surface) */
void HostDirty(sword left, sword top, sword right, sword bottom);
/* the pointer: reset with the device */
void HostPtrReset(void);

#endif /* HOST_H */
