/*
 * geodesResource.c -- the resource handles of geodes.
 *
 * Unrelocated optrs in resources hold the resource number (Port32/ABI.md
 * section 4); object relocation turns it into the handle of that
 * resource of the block's owner, and back.  GeodeLoad will keep the
 * handles in the geode (GM_resourceHandles); until it exists, whoever
 * builds a geode's resources registers them here.
 */
#include "kernelC.h"

#define MAX_GEODES	32

static struct {
    GeodeHandle		gh;
    const Handle	*handles;
    word		count;
} geodes[MAX_GEODES];

void GeodeSetResourceHandles(GeodeHandle gh, const Handle *handles,
			     word count)
{
    int i, free = -1;

    for (i = 0; i < MAX_GEODES; i++) {
	if (geodes[i].gh == gh) {
	    free = i;
	    break;
	}
	if (!geodes[i].gh && free < 0)
	    free = i;
    }
    if (free < 0)
	KernelFatal(HANDLE_TABLE_FULL);
    geodes[free].gh = handles ? gh : 0;
    geodes[free].handles = handles;
    geodes[free].count = count;
}

static int Find(GeodeHandle gh)
{
    int i;

    for (i = 0; i < MAX_GEODES; i++)
	if (gh && geodes[i].gh == gh)
	    return i;
    return -1;
}

Handle GeodeResourceHandle(GeodeHandle gh, word res)
{
    int i = Find(gh);

    if (i < 0)
	return 0;
    if (res == GOCR_PROCESS_RES)
	res = 0;
    return res < geodes[i].count ? geodes[i].handles[res] : 0;
}

sdword GeodeHandleToResource(GeodeHandle gh, Handle h)
{
    int i = Find(gh);
    word r;

    if (i < 0 || !h)
	return -1;
    for (r = 0; r < geodes[i].count; r++)
	if (geodes[i].handles[r] == h)
	    return r ? r : GOCR_PROCESS_RES;
    return -1;
}
