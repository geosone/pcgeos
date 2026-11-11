/*
 * geodesDriver.c -- drivers (after geodesDriver.asm, the driver parts of
 * geodesLoad.asm and geodesUtils.asm).
 *
 * A driver is a geode of type "driver" with a DriverInfoStruct, the
 * variable DriverTable (GM_driverTable).  Its strategy routine is a C
 * function (Port32/include/geosdriver.h).
 *
 *   - Once a driver is loaded, its strategy routine gets DR_INIT with
 *     the geode handle in bx and the two words of appInfo in cx and dx
 *     (valueToPass1/2).  A carry fails the load with
 *     GLE_DRIVER_INIT_ERROR.  A driver that is also a system geode
 *     (GA_SYSTEM) joins the system driver list.
 *   - Before it is freed, it gets DR_EXIT, if it had DR_INIT.
 *   - GeodeExitSystemDrivers gives DR_EXIT to the system drivers, the
 *     last first (ExitSystemDrivers, for the shutdown).
 *
 * GeodeUseDriver loads a driver from its file (or uses it if it is
 * loaded); GeodeUseDriverPermName only uses a loaded one
 * (UseLibraryDriverCommon, geodesLibrary.c).
 *
 * Not yet: the strategy shortcuts of the default video, task and
 * keyboard drivers (defaultVideoStrategy and so on).
 */
#include "kernelC.h"
#include "geosmod.h"
#include "geosdriver.h"

#include <string.h>

#define MAX_SYSTEM_DRIVERS	16

static GeodeHandle systemDrivers[MAX_SYSTEM_DRIVERS];
static word systemDriverCount;
static GeodeHandle defaultDrivers[GDDT_TASK / 2 + 1];

static DriverInfoStruct *TableOf(GeodeHandle gh)
{
    GeodeCore *c = GeodeCoreOf(gh);

    return c ? (DriverInfoStruct *)
	       ((const GeosModuleInfo *) c->GC_info)->GM_driverTable : NULL;
}

GeodeLoadError GeodeInitDriver(GeodeHandle gh, dword appInfo)
{
    DriverInfoStruct *dis = TableOf(gh);
    GeodeCore *c = GeodeCoreOf(gh);
    DriverRegs r;

    if (!dis || !dis->DIS_strategy)
	return GLE_NOT_GEOS_EXECUTABLE_FILE;
    memset(&r, 0, sizeof(r));
    r.DR_bx = gh;
    r.DR_cx = appInfo >> 16;
    r.DR_dx = appInfo & 0xffff;
    if (DriverCall(dis, DR_INIT, &r))
	return GLE_DRIVER_INIT_ERROR;
    c = GeodeCoreOf(gh);
    c->GC_attrs |= GA_DRIVER_INITIALIZED;
    if ((c->GC_attrs & GA_SYSTEM) && systemDriverCount < MAX_SYSTEM_DRIVERS)
	systemDrivers[systemDriverCount++] = gh;
    return GLE_OK;
}

void GeodeExitDriver(GeodeHandle gh)
{
    GeodeCore *c = GeodeCoreOf(gh);
    DriverInfoStruct *dis = TableOf(gh);
    DriverRegs r;
    word i;

    if (!c || !(c->GC_attrs & GA_DRIVER_INITIALIZED))
	return;
    c->GC_attrs &= ~GA_DRIVER_INITIALIZED;
    for (i = 0; i < systemDriverCount; i++)
	if (systemDrivers[i] == gh) {
	    memmove(&systemDrivers[i], &systemDrivers[i + 1],
		    (systemDriverCount - i - 1) * sizeof(GeodeHandle));
	    systemDriverCount--;
	    break;
	}
    for (i = 0; i < sizeof(defaultDrivers) / sizeof(defaultDrivers[0]); i++)
	if (defaultDrivers[i] == gh)
	    defaultDrivers[i] = 0;
    memset(&r, 0, sizeof(r));
    DriverCall(dis, DR_EXIT, &r);
}

void GeodeExitSystemDrivers(void)
{
    GeodeLock();
    while (systemDriverCount)
	GeodeExitDriver(systemDrivers[systemDriverCount - 1]);
    GeodeUnlock();
}

GeodeHandle GeodeUseDriver(const char *name, word protoMajor,
			   word protoMinor, GeodeLoadError *err)
{
    return GeodeUseCommon(name, FALSE, GA_DRIVER, protoMajor, protoMinor,
			  err);
}

GeodeHandle GeodeUseDriverPermName(const char *pname, word protoMajor,
				   word protoMinor, GeodeLoadError *err)
{
    return GeodeUseCommon(pname, TRUE, GA_DRIVER, protoMajor, protoMinor,
			  err);
}

void GeodeFreeDriver(GeodeHandle gh)
{
    GeodeCore *c;

    GeodeLock();
    c = GeodeCoreOf(gh);
    if (!c)
	KernelFatal(GEODE_NOT_LOADED);
    if (!(c->GC_attrs & GA_DRIVER))
	KernelFatal(GEODE_FREE_DRIVER_NOT_DRIVER);
    if (!--c->GC_refCount)
	GeodeFreeLow(gh);
    GeodeUnlock();
}

DriverInfoStruct *GeodeInfoDriver(GeodeHandle gh)
{
    return TableOf(gh);
}

GeodeHandle GeodeGetDefaultDriver(GeodeDefaultDriverType type)
{
    if ((type & 1) || type > GDDT_TASK)
	KernelFatal(GEODE_GET_DEF_DRIVER_BAD_TYPE);
    return defaultDrivers[type / 2];
}

void GeodeSetDefaultDriver(GeodeDefaultDriverType type, GeodeHandle gh)
{
    if ((type & 1) || type > GDDT_TASK)
	KernelFatal(GEODE_SET_DEF_DRIVER_BAD_TYPE);
    defaultDrivers[type / 2] = gh;
}
