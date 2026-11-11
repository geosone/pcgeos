/*
 * geodesLibrary.c -- using and freeing libraries (after
 * geodesLibrary.asm).
 *
 * A library is found by its permanent name among the loaded geodes, or
 * loaded from the image of that name (GeodeRegisterImage), or from the
 * file of that name with ".geo" in SP_SYSTEM (TryOpenLib of
 * geodesLibrary.asm; there are no "ec.geo" files to try first: the C
 * kernel has no separate error-checking geodes).  The protocol
 * the client was built with must match the library's: the same major
 * number, and a minor number not above the library's
 * (TestProtocolNumbers of geodesLoad.asm).  0.0 takes any protocol:
 * gpgeode.py does not know yet what a geode needs of its libraries
 * (glue took it from the library's .ldf file).
 */
#include "kernelC.h"
#include "geosmod.h"

#include <string.h>

static GeodeLoadError TestProtocol(word wantMajor, word wantMinor,
				   word haveMajor, word haveMinor)
{
    if (!wantMajor && !wantMinor)
	return GLE_OK;
    if (wantMajor > haveMajor)
	return GLE_PROTOCOL_IMPORTER_TOO_RECENT;
    if (wantMajor < haveMajor)
	return GLE_PROTOCOL_IMPORTER_TOO_OLD;
    if (wantMinor > haveMinor)
	return GLE_PROTOCOL_IMPORTER_TOO_RECENT;
    return GLE_OK;
}

/* the permanent name, up to its first blank, plus ".geo" in SP_SYSTEM */
static GeodeHandle LoadLibrary(const char *pname, size_t n,
			       GeodeLoadError *err)
{
    char file[GEODE_NAME_SIZE + 5];
    size_t i;
    GeodeHandle gh;

    if (GeodeHasImage(pname))
	return GeodeLoadLow(pname, GA_LIBRARY, 0, 0, 0, err);
    for (i = 0; i < n && pname[i] && pname[i] != ' '; i++)
	file[i] = pname[i];
    strcpy(file + i, ".geo");
    FilePushDir();
    FileSetStandardPath(SP_SYSTEM);
    gh = GeodeLoadLow(file, GA_LIBRARY, 0, 0, 0, err);
    FilePopDir();
    return gh;
}

GeodeHandle GeodeUseLibraryLow(const char *pname, word protoMajor,
			       word protoMinor, GeodeLoadError *err)
{
    char name[GEODE_NAME_SIZE];
    size_t n = strlen(pname);
    const GeosModuleInfo *info;
    GeodeHandle gh;

    /* permanent names are space padded */
    if (n > GEODE_NAME_SIZE)
	n = GEODE_NAME_SIZE;
    memset(name, ' ', sizeof(name));
    memcpy(name, pname, n);

    GeodeLock();
    gh = GeodeFindLow(name, GEODE_NAME_SIZE, GA_LIBRARY, 0);
    if (gh)
	GeodeCoreOf(gh)->GC_refCount++;
    else {
	gh = LoadLibrary(pname, n, err);
	if (!gh) {
	    if (*err == GLE_FILE_NOT_FOUND)
		*err = GLE_LIBRARY_NOT_FOUND;
	    GeodeUnlock();
	    return 0;
	}
    }
    info = GeodeCoreOf(gh)->GC_info;
    *err = TestProtocol(protoMajor, protoMinor, info->GM_protoMajor,
			info->GM_protoMinor);
    if (*err != GLE_OK) {
	GeodeFreeLibraryLow(gh);
	gh = 0;
    }
    GeodeUnlock();
    return gh;
}

void GeodeFreeLibraryLow(GeodeHandle gh)
{
    GeodeCore *c;

    GeodeLock();
    c = GeodeCoreOf(gh);
    if (!c)
	KernelFatal(GEODE_NOT_LOADED);
    if (!(c->GC_attrs & GA_LIBRARY))
	KernelFatal(GEODE_FREE_LIBRARY_NOT_LIBRARY);
    if (!--c->GC_refCount)
	GeodeFreeLow(gh);
    GeodeUnlock();
}

/* the library's entry with a client call type for the running geode */
static void TellLibrary(GeodeHandle gh, LibraryCallType ty)
{
    GeodeCore *c = GeodeCoreOf(gh);
    GeodeLibraryEntry *e;

    if (!c)
	return;
    e = (GeodeLibraryEntry *) ((const GeosModuleInfo *) c->GC_info)->GM_entry;
    if (e)
	e(ty, GeodeGetProcessHandle());
}

GeodeHandle GeodeUseLibrary(const char *name, word protoMajor,
			    word protoMinor, GeodeLoadError *err)
{
    GeodeLoadError e;
    GeodeHandle gh;

    GeodeLock();
    gh = GeodeUseLibraryLow(name, protoMajor, protoMinor, &e);
    if (gh)
	TellLibrary(gh, LCT_NEW_CLIENT);
    GeodeUnlock();
    if (err)
	*err = e;
    return gh;
}

GeodeHandle GeodeUseLibraryPermName(const char *pname, word protoMajor,
				    word protoMinor, GeodeLoadError *err)
{
    return GeodeUseLibrary(pname, protoMajor, protoMinor, err);
}

void GeodeFreeLibrary(GeodeHandle gh)
{
    GeodeLock();
    TellLibrary(gh, LCT_CLIENT_EXIT);
    GeodeFreeLibraryLow(gh);
    GeodeUnlock();
}
