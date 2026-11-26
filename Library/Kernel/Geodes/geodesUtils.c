/*
 * geodesUtils.c -- after geodesUtils.asm: GeodeRemoveReference.
 */
#include "kernelC.h"
#include "geosmod.h"
#include <vm.h>
#include "ghal.h"
#include <string.h>

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
