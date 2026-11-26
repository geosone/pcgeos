/*
 * geodesUtils.c -- after geodesUtils.asm: GeodeRemoveReference.
 */
#include "kernelC.h"
#include "geosmod.h"
#include <vm.h>
#include "ghal.h"
#include <string.h>
#include <system.h>
#include "gapi.h"

/* TRUE if that was the last reference and the geode is gone */
Boolean GeodeRemoveReference(GeodeHandle gh)
{
    GeodeCore *c;
    Boolean gone = FALSE;

    GeodeLock();
    c = GeodeCoreOf(gh);
    if (!c)
	KernelFatal(GEODE_NOT_LOADED);
    if (!--c->GC_refCount) {
	GeodeFreeLow(gh);
	gone = TRUE;
    }
    GeodeUnlock();
    return gone;
}

/* RemoveGeodes: DR_EXIT to the drivers that are not system drivers */
void RemoveGeodes(void)
{
    GeodeHandle drivers[64], gh;
    word n = 0;

    GeodeLock();
    for (gh = GeodeFindLow("", 0, 0, 0); gh; gh = GeodeCoreOf(gh)->GC_next)
	if ((GeodeCoreOf(gh)->GC_attrs & (GA_DRIVER | GA_SYSTEM)) == GA_DRIVER
	    && n < 64)
	    drivers[n++] = gh;
    while (n)
	GeodeExitDriver(drivers[--n]);
    GeodeUnlock();
}
