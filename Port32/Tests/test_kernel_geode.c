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

#include <stdio.h>
#include <string.h>

extern const unsigned char kglib_geode[], kgapp_geode[];
extern const unsigned long kglib_geode_size, kgapp_geode_size;

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
    GeodeRegisterImage("kglib", kglib_geode, kglib_geode_size);
    GeodeRegisterImage("kgapp", kgapp_geode, kgapp_geode_size);
    base = HandleCountUsed();

    /* errors */
    Check(!GeodeLoad("nosuch", 0, 0, 0, 0, &e) && e == GLE_FILE_NOT_FOUND,
	  "no such geode");
    Check(!GeodeLoad("kgapp", GA_LIBRARY, 0, 0, 0, &e) &&
	  e == GLE_ATTRIBUTE_MISMATCH, "attribute mismatch");
    Check(!GeodeUseLibrary("nosuch", 0, 0, &e) && e == GLE_LIBRARY_NOT_FOUND,
	  "no such library");
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

    GeodeFreeLibrary(lib);
    Check(!GeodeFind("kglib   ", GEODE_NAME_SIZE, 0, 0), "library freed");
    WAIT_FOR(HandleCountUsed() == base);
    Check(HandleCountUsed() == base, "no handles left");
    if (HandleCountUsed() != base) {
	printf("base %u now %u\n", (unsigned) base, (unsigned) HandleCountUsed());
	HandleEnum(Dump, NULL);
    }

    printf("%s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
