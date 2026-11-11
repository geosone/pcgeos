/*
 * geodesAccess.c -- information about geodes (after geodesAccess.asm and
 * GeodeFind of geodesHandle.asm).
 */
#include "kernelC.h"
#include "geosmod.h"

#include <string.h>

#define INFO(c)	((const GeosModuleInfo *) (c)->GC_info)

/* the geode of the running thread */
GeodeHandle GeodeGetProcessHandle(void)
{
    return HandleEntryOf(KernelThisThread())->HE_owner;
}

static GeodeCore *CoreOrCurrent(GeodeHandle *gh)
{
    GeodeCore *c;

    if (!*gh)
	*gh = GeodeGetProcessHandle();
    c = GeodeCoreOf(*gh);
    if (!c)
	KernelFatal(GEODE_NOT_LOADED);
    return c;
}

/*
 * GGIT_ATTRIBUTES and GGIT_GEODE_REF_COUNT return the value; the others
 * fill buf and return 0.  A module has no release number (zeros) and
 * no type apart from its attributes (GGIT_TYPE returns 0).
 */
word GeodeGetInfo(GeodeHandle gh, GeodeGetInfoType info, void *buf)
{
    GeodeCore *c;
    word r = 0;

    GeodeLock();
    c = CoreOrCurrent(&gh);
    switch (info) {
    case GGIT_ATTRIBUTES:
	r = c->GC_attrs;
	break;
    case GGIT_GEODE_RELEASE:
	memset(buf, 0, sizeof(ReleaseNumber));
	break;
    case GGIT_GEODE_PROTOCOL: {
	ProtocolNumber p;
	p.PN_major = INFO(c)->GM_protoMajor;
	p.PN_minor = INFO(c)->GM_protoMinor;
	memcpy(buf, &p, sizeof(p));
	break;
    }
    case GGIT_TOKEN_ID:
	memcpy(buf, &INFO(c)->GM_token, sizeof(GeodeToken));
	break;
    case GGIT_PERM_NAME_AND_EXT:
	memcpy(buf, c->GC_name, GEODE_NAME_SIZE + GEODE_NAME_EXT_SIZE);
	break;
    case GGIT_PERM_NAME_ONLY:
	memcpy(buf, c->GC_name, GEODE_NAME_SIZE);
	break;
    case GGIT_GEODE_REF_COUNT:
	r = c->GC_refCount;
	break;
    default:
	break;
    }
    GeodeUnlock();
    return r;
}

GeodeHandle GeodeFind(const char *name, word numChars, GeodeAttrs attrMatch,
		      GeodeAttrs attrNoMatch)
{
    GeodeHandle gh;

    GeodeLock();
    gh = GeodeFindLow(name, numChars, attrMatch, attrNoMatch);
    GeodeUnlock();
    return gh;
}

/* the application object ("appobj"), relocated; 0 if there is none */
optr GeodeGetAppObject(GeodeHandle gh)
{
    GeodeCore *c;
    optr o;

    GeodeLock();
    c = CoreOrCurrent(&gh);
    o = INFO(c)->GM_appObj ?
	GocrResolveWith(INFO(c)->GM_appObj, INFO(c)->GM_resourceHandles) : 0;
    GeodeUnlock();
    return o;
}

/* exported entry `entryNumber' of a library (.gp order) */
void *ProcGetLibraryEntry(GeodeHandle library, word entryNumber)
{
    GeodeCore *c;
    const void *a = NULL;

    GeodeLock();
    c = GeodeCoreOf(library);
    if (!c)
	KernelFatal(GEODE_NOT_LOADED);
    if (entryNumber < INFO(c)->GM_exportCount)
	a = INFO(c)->GM_exports[entryNumber].GME_address;
    GeodeUnlock();
    return (void *) a;
}
