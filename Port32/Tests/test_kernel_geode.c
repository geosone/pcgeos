/*
 * test_kernel_geode.c -- the C kernel loads geodes (Geodes/geodesLoad.c,
 * Port32/ABI.md section 7).
 *
 * Two geodes built by goc -cg and gpgeode.py in the module format of the
 * host (Port32/Tests/Geode): the library kglib (a routine, a class, an
 * entry that counts its calls; protocol 2.3) and the process kgapp,
 * which uses kglib and has an object of kglib's class in its object
 * resource.  The test checks
 *
 *   - load errors: no such geode, attributes, protocol, a library that
 *     is not there, a second launch of a single-launchable process;
 *   - GeodeUseLibrary: LCT_ATTACH and LCT_NEW_CLIENT, GeodeGetInfo,
 *     GeodeFind, ProcGetLibraryEntry;
 *   - GeodeLoad of the process: its imports from the kernel and from
 *     kglib, the object resource built and relocated, optrs in chunks
 *     that are not objects (an optr, a structure, a chunk array, a
 *     static GCN list) relocated, MSG_META_ATTACH
 *     with the appInfo words on the process thread, GeodeGetAppObject;
 *   - ObjDuplicateResource of a template (references within it lead to
 *     the copy, others to the resources; owner and thread given or taken
 *     from the source), the saved-block list, ObjFreeDuplicate;
 *   - MSG_META_DETACH: the thread exits and frees the geode, which
 *     drops its reference to kglib (LCT_CLIENT_EXIT); GeodeFreeLibrary
 *     frees kglib (LCT_DETACH); no handle is left over.
 */
#include "kernelC.h"
#include "ghal.h"
#include "geosdriver.h"

#include <stdio.h>
#include <string.h>

extern const unsigned char kglib_geode[], kgapp_geode[], kgdrv_geode[];
extern const unsigned long kglib_geode_size, kgapp_geode_size,
			   kgdrv_geode_size;

/* as kgdrv.c */
typedef struct {
    word	KGDC_init, KGDC_exit;
    GeodeHandle	KGDC_handle;
    word	KGDC_value1, KGDC_value2;
} KGDrvCounts;
#define KGDRV_ADD	100

/* as kglib.goh and kgapp.goc */
typedef struct {
    word	KGLC_calls[LCT_CLIENT_EXIT + 1];
    GeodeHandle	KGLC_lastClient;
} KGLibCallCounts;

typedef struct {
    word	KGAS_attached;
    word	KGAS_value1, KGAS_value2;
    int		KGAS_sum;
    word	KGAS_count;
    GeodeHandle	KGAS_process;
    optr	KGAS_appObj;
    ThreadHandle KGAS_thread;
    word	KGAS_chunkRefs;
    Boolean	KGAS_gcnFound;
    MemHandle	KGAS_dup;
    word	KGAS_dupChecks;
    word	KGAS_vmChecks;
    word	KGAS_restored;
    word	KGAS_stateChecks;
} KGAppState;

#define GEODE	0x2340

static int fails;

static void Check(int ok, const char *what)
{
    if (!ok) {
	fails++;
	printf("FAIL: %s\n", what);
    }
}

#define WAIT_FOR(cond) \
    do { int n_; for (n_ = 0; n_ < 500 && !(cond); n_++) TimerSleep(1); \
    } while (0)

/* the handles left over (on failure) */
static int Dump(Handle h, HandleEntry *he, void *ctx)
{
    (void) ctx;
    printf("%x type %x owner %x size %u\n", (unsigned) h, he->HE_type, (unsigned) he->HE_owner, he->HE_size);
    return 0;
}

/* a native file of the top directory */
static int WriteFile(const char *name, const void *data, unsigned long size)
{
    FileHandle fh = FileCreate(name, FILE_CREATE_TRUNCATE | FCF_NATIVE |
				     FILE_ACCESS_RW, 0);
    const unsigned char *p = data;
    unsigned long done;
    word n;

    if (!fh)
	return 0;
    for (done = 0; done < size; done += n) {
	n = size - done > 0x4000 ? 0x4000 : (word) (size - done);
	if (FileWrite(fh, p + done, n, FALSE) != n)
	    break;
    }
    FileClose(fh, FALSE);
    return done >= size;
}

int main(int argc, char **argv)
{
    char top[260], *slash;

    ThreadHandle me;
    GeodeHandle lib, app;
    GeodeLoadError e;
    KGLibCallCounts *calls;
    KGAppState *st;
    int (*add)(int, int);
    char name[GEODE_NAME_SIZE + GEODE_NAME_EXT_SIZE];
    ProtocolNumber pn;
    struct { word callerID; optr ackOD; } dp = { 0, 0 };
    dword base;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);	/* all of it, should DOS crash */
    /* the GEOS top directory GGTOP next to the program (the VM file) */
    strncpy(top, argv[0], sizeof(top) - 8);
    top[sizeof(top) - 8] = 0;
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash)
	strcpy(slash + 1, "GGTOP");
    else
	strcpy(top, "GGTOP");
    ghal_fs_mkdir(top);
    FileSetTopHostPath(top);
    kernelCurrentGeode = GEODE;
    me = KernelThreadInit(PRIORITY_STANDARD);
    KernelTimerInit();
    FileDelete("KGSTATE.VM");		/* none from an earlier run */
    GeodeRegisterImage("kglib", kglib_geode, kglib_geode_size);
    GeodeRegisterImage("kgapp", kgapp_geode, kgapp_geode_size);
    base = HandleCountUsed();

    /* errors */
    Check(!GeodeLoad("nosuch", 0, 0, 0, 0, &e) && e == GLE_FILE_NOT_FOUND,
	  "no such geode");
    Check(!GeodeLoad("kgapp", GA_LIBRARY, 0, 0, 0, &e) &&
	  e == GLE_ATTRIBUTE_MISMATCH, "attribute mismatch");
    Check(!GeodeUseLibrary("nosuch", 0, 0, &e) && e == GLE_FILE_NOT_FOUND,
	  "no such library file");
    Check(!GeodeUseLibraryPermName("kglib", 0, 0, &e) &&
	  e == GLE_LIBRARY_NOT_FOUND, "a permanent name must be loaded");
    Check(!GeodeUseLibrary("kglib", 3, 0, &e) &&
	  e == GLE_PROTOCOL_IMPORTER_TOO_RECENT, "protocol: major too new");
    Check(!GeodeUseLibrary("kglib", 1, 9, &e) &&
	  e == GLE_PROTOCOL_IMPORTER_TOO_OLD, "protocol: major too old");
    Check(!GeodeUseLibrary("kglib", 2, 4, &e) &&
	  e == GLE_PROTOCOL_IMPORTER_TOO_RECENT, "protocol: minor too new");
    Check(!GeodeFind("kglib   ", GEODE_NAME_SIZE, 0, 0),
	  "library freed after a protocol error");
    Check(HandleCountUsed() == base, "no handles left after the errors");

    /* the library on its own */
    lib = GeodeUseLibrary("kglib", 2, 3, &e);
    Check(lib != 0, "GeodeUseLibrary");
    if (!lib)
	return 1;
    Check(GeodeFind("kglib   ", GEODE_NAME_SIZE, GA_LIBRARY, 0) == lib,
	  "GeodeFind library");
    Check(GeodeFind("kglib   lib ", 12, 0, GA_PROCESS) == lib,
	  "GeodeFind name and extension");
    Check((GeodeGetInfo(lib, GGIT_ATTRIBUTES, NULL) &
	   (GA_LIBRARY | GA_LIBRARY_INITIALIZED | GA_PROCESS)) ==
	  (GA_LIBRARY | GA_LIBRARY_INITIALIZED), "library attributes");
    GeodeGetInfo(lib, GGIT_GEODE_PROTOCOL, &pn);
    Check(pn.PN_major == 2 && pn.PN_minor == 3, "library protocol");
    GeodeGetInfo(lib, GGIT_PERM_NAME_AND_EXT, name);
    Check(memcmp(name, "kglib   lib ", sizeof(name)) == 0, "permanent name");
    Check(GeodeGetInfo(lib, GGIT_GEODE_REF_COUNT, NULL) == 1, "ref count 1");
    add = (int (*)(int, int)) ProcGetLibraryEntry(lib, 0);
    Check(add && add(2, 3) == 5, "exported routine");
    calls = ((KGLibCallCounts *(*)(void)) ProcGetLibraryEntry(lib, 1))();
    Check(calls->KGLC_calls[LCT_ATTACH] == 1, "LCT_ATTACH");
    Check(calls->KGLC_calls[LCT_NEW_CLIENT] == 1 &&
	  calls->KGLC_lastClient == GEODE, "LCT_NEW_CLIENT for the caller");

    /* the process */
    app = GeodeLoad("kgapp", GA_PROCESS, GA_LIBRARY, 0, 0x00070009, &e);
    Check(app != 0, "GeodeLoad kgapp");
    if (!app) {
	printf("GeodeLoad error %d\n", (int) e);
	return 1;
    }
    Check(GeodeGetInfo(lib, GGIT_GEODE_REF_COUNT, NULL) == 2,
	  "the process uses the library");
    Check(calls->KGLC_calls[LCT_NEW_CLIENT] == 2 &&
	  calls->KGLC_lastClient == app, "LCT_NEW_CLIENT for the process");
    Check(!GeodeLoad("kgapp", 0, 0, 0, 0, &e) &&
	  e == GLE_NOT_MULTI_LAUNCHABLE, "single launchable");
    st = ProcGetLibraryEntry(app, 0);
    WAIT_FOR(st->KGAS_attached);
    Check(st->KGAS_attached == 1, "MSG_META_ATTACH");
    Check(st->KGAS_value1 == 7 && st->KGAS_value2 == 9, "appInfo words");
    Check(st->KGAS_sum == 16, "routine of the library");
    Check(st->KGAS_count == 12, "object of the library's class");
    Check(st->KGAS_process == app, "GeodeGetProcessHandle");
    Check(st->KGAS_thread && st->KGAS_thread != me, "process thread");
    Check((ThreadHandle) MemGetInfo(app, MGIT_OTHER_INFO) == st->KGAS_thread,
	  "the core block names the thread");
    Check(OptrToHandle(st->KGAS_appObj) != 0 &&
	  !GOCR_IS_UNRELOCATED(st->KGAS_appObj) &&
	  st->KGAS_appObj == GeodeGetAppObject(app), "GeodeGetAppObject");
    Check(st->KGAS_chunkRefs == 3,
	  "optrs in chunk data relocated (optr, structure, chunk array)");
    Check(st->KGAS_gcnFound, "static GCN list relocated");
    Check(st->KGAS_dupChecks == 8, "ObjDuplicateResource, ObjFreeDuplicate");
    Check(st->KGAS_vmChecks == 7, "object blocks in a VM file");
    Check(!st->KGAS_restored && st->KGAS_stateChecks == 1,
	  "a new state file");
    if (st->KGAS_vmChecks != 7)
	printf("vm checks %u\n", st->KGAS_vmChecks);
    {
	GeodeCore *c = GeodeCoreOf(app);
	Handle sb = c ? c->GC_savedBlocks : 0;
	Check(sb && HandleEntryOf(sb)->HE_otherInfo == st->KGAS_dup &&
	      !HandleEntryOf(sb)->HE_next,
	      "the copy alone on the saved-block list");
    }
    Check(MemOwner(OptrToHandle(st->KGAS_appObj)) == app,
	  "the process owns its resource");

    /* detach: the thread exits and frees the process */
    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(app, 0), KMSG_META_DETACH,
		 &dp, sizeof(dp), NULL, 0);
    WAIT_FOR(!GeodeFind("kgapp   ", GEODE_NAME_SIZE, 0, 0));
    Check(!GeodeFind("kgapp   ", GEODE_NAME_SIZE, 0, 0), "process freed");
    Check(calls->KGLC_calls[LCT_CLIENT_EXIT] == 1 &&
	  calls->KGLC_lastClient == app, "LCT_CLIENT_EXIT for the process");
    Check(GeodeGetInfo(lib, GGIT_GEODE_REF_COUNT, NULL) == 1,
	  "the process dropped the library");
    Check(calls->KGLC_calls[LCT_DETACH] == 0, "library still attached");

    /* again: from the state file it saved */
    app = GeodeLoad("kgapp", GA_PROCESS, GA_LIBRARY, 0, 0x00070009, &e);
    Check(app != 0, "GeodeLoad kgapp again");
    st = (KGAppState *) ProcGetLibraryEntry(app, 0);
    WAIT_FOR(st->KGAS_attached);
    Check(st->KGAS_restored && st->KGAS_stateChecks == 7,
	  "restored from the state file");
    if (st->KGAS_stateChecks != 7)
	printf("state checks %u\n", st->KGAS_stateChecks);
    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(app, 0), KMSG_META_DETACH,
		 &dp, sizeof(dp), NULL, 0);
    WAIT_FOR(!GeodeFind("kgapp   ", GEODE_NAME_SIZE, 0, 0));
    Check(!GeodeFind("kgapp   ", GEODE_NAME_SIZE, 0, 0),
	  "process freed again");

    GeodeFreeLibrary(lib);
    Check(!GeodeFind("kglib   ", GEODE_NAME_SIZE, 0, 0), "library freed");

    /* from geode files: the process from WORLD, its library from SYSTEM */
    GeodeRegisterImage("kglib", NULL, 0);
    GeodeRegisterImage("kgapp", NULL, 0);
    FileSetStandardPath(SP_TOP);
    FileCreateDir("SYSTEM");
    FileCreateDir("WORLD");
    Check(WriteFile("SYSTEM\\kglib.geo", kglib_geode, kglib_geode_size) &&
	  WriteFile("WORLD\\kgapp.geo", kgapp_geode, kgapp_geode_size),
	  "geode files written");
    FileSetStandardPath(SP_APPLICATION);
    Check(!GeodeLoad("nosuch.geo", 0, 0, 0, 0, &e) &&
	  e == GLE_FILE_NOT_FOUND, "no such geode file");
    FileSetStandardPath(SP_SYSTEM);
    FileRename("kglib.geo", "kglib.old");
    FileSetStandardPath(SP_APPLICATION);
    Check(!GeodeLoad("kgapp.geo", GA_PROCESS, 0, 0, 0, &e) &&
	  e == GLE_LIBRARY_NOT_FOUND, "no library file");
    FileSetStandardPath(SP_SYSTEM);
    FileRename("kglib.old", "kglib.geo");
    FileSetStandardPath(SP_APPLICATION);
    app = GeodeLoad("kgapp.geo", GA_PROCESS, GA_LIBRARY, 0, 0x00070009, &e);
    Check(app != 0, "GeodeLoad from a file");
    if (app) {
	lib = GeodeFind("kglib   ", GEODE_NAME_SIZE, GA_LIBRARY, 0);
	Check(lib != 0, "the library from its file in SYSTEM");
	st = (KGAppState *) ProcGetLibraryEntry(app, 0);
	WAIT_FOR(st->KGAS_attached);
	Check(st->KGAS_attached == 1 && st->KGAS_sum == 16,
	      "the process from a file runs");
	CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(app, 0), KMSG_META_DETACH,
		     &dp, sizeof(dp), NULL, 0);
	WAIT_FOR(!GeodeFind("kglib   ", GEODE_NAME_SIZE, 0, 0));
	Check(!GeodeFind("kgapp   ", GEODE_NAME_SIZE, 0, 0) &&
	      !GeodeFind("kglib   ", GEODE_NAME_SIZE, 0, 0),
	      "process and library from files freed");
    } else
	printf("GeodeLoad error %d\n", (int) e);
    FileSetStandardPath(SP_TOP);

    /* a driver: DR_INIT, its strategy routine, DR_EXIT */
    Check(WriteFile("SYSTEM\\kgdrv.geo", kgdrv_geode, kgdrv_geode_size),
	  "driver file written");
    FileSetStandardPath(SP_SYSTEM);
    Check(!GeodeUseDriverPermName("kgdrv", 0, 0, &e) &&
	  e == GLE_LIBRARY_NOT_FOUND, "a permanent name must be loaded");
    Check(!GeodeUseLibrary("kgdrv.geo", 0, 0, &e) &&
	  e == GLE_ATTRIBUTE_MISMATCH, "a driver is no library");
    Check(!GeodeLoad("kgdrv.geo", GA_DRIVER, 0, 0, 0xdead0000, &e) &&
	  e == GLE_DRIVER_INIT_ERROR, "DR_INIT fails the load");
    Check(!GeodeFind("kgdrv   ", GEODE_NAME_SIZE, 0, 0),
	  "freed after GLE_DRIVER_INIT_ERROR");
    {
	GeodeHandle drv, drv2;
	DriverInfoStruct *dis;
	KGDrvCounts *dc;
	DriverRegs r;

	drv = GeodeUseDriver("kgdrv.geo", 0, 0, &e);
	Check(drv != 0, "GeodeUseDriver");
	if (drv) {
	    dc = ((KGDrvCounts *(*)(void)) ProcGetLibraryEntry(drv, 0))();
	    Check(dc->KGDC_init == 1 && dc->KGDC_exit == 0 &&
		  dc->KGDC_handle == drv, "DR_INIT with the geode handle");
	    Check((GeodeGetInfo(drv, GGIT_ATTRIBUTES, NULL) &
		   (GA_DRIVER | GA_DRIVER_INITIALIZED)) ==
		  (GA_DRIVER | GA_DRIVER_INITIALIZED), "driver attributes");
	    dis = GeodeInfoDriver(drv);
	    Check(dis && dis->DIS_driverType == DRIVER_TYPE_OTHER_PROCESSOR,
		  "GeodeInfoDriver");
	    memset(&r, 0, sizeof(r));
	    r.DR_ax = 40;
	    r.DR_bx = 2;
	    Check(dis && !DriverCall(dis, KGDRV_ADD, &r) && r.DR_cx == 42,
		  "the strategy routine");
	    Check(dis && DriverCall(dis, 999, &r), "carry for an unknown call");
	    drv2 = GeodeUseDriverPermName("kgdrv", 0, 0, &e);
	    Check(drv2 == drv && dc->KGDC_init == 1 &&
		  GeodeGetInfo(drv, GGIT_GEODE_REF_COUNT, NULL) == 2,
		  "GeodeUseDriverPermName: the loaded driver");
	    GeodeSetDefaultDriver(GDDT_POWER_MANAGEMENT, drv);
	    Check(GeodeGetDefaultDriver(GDDT_POWER_MANAGEMENT) == drv,
		  "default driver");
	    GeodeFreeDriver(drv2);
	    Check(dc->KGDC_exit == 0, "still in use");
	    GeodeFreeDriver(drv);
	    Check(!GeodeFind("kgdrv   ", GEODE_NAME_SIZE, 0, 0),
		  "driver freed");
	    Check(GeodeGetDefaultDriver(GDDT_POWER_MANAGEMENT) == 0,
		  "no default driver once freed");
	}
	/* a system driver gets DR_EXIT from GeodeExitSystemDrivers */
	drv = GeodeUseDriver("kgdrv.geo", 0, 0, &e);
	if (drv) {
	    dc = ((KGDrvCounts *(*)(void)) ProcGetLibraryEntry(drv, 0))();
	    GeodeExitSystemDrivers();
	    Check(dc->KGDC_exit == 1 &&
		  !(GeodeGetInfo(drv, GGIT_ATTRIBUTES, NULL) &
		    GA_DRIVER_INITIALIZED), "GeodeExitSystemDrivers");
	    GeodeFreeDriver(drv);
	    Check(!GeodeFind("kgdrv   ", GEODE_NAME_SIZE, 0, 0),
		  "no second DR_EXIT, driver freed");
	}
    }
    FileSetStandardPath(SP_TOP);
    WAIT_FOR(HandleCountUsed() == base);
    Check(HandleCountUsed() == base, "no handles left");
    if (HandleCountUsed() != base) {
	printf("base %u now %u\n", (unsigned) base, (unsigned) HandleCountUsed());
	HandleEnum(Dump, NULL);
    }

    printf("%s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
