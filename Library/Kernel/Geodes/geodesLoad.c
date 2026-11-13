/*
 * geodesLoad.c -- loading and freeing geodes (after geodesLoad.asm,
 * geodesProcess.asm and the exit part of geodesHandle.asm).
 *
 * A geode is a module of the host, loaded by ghal_module (Port32/ABI.md
 * section 7): what glue put into the geode header is the module's
 * __geos_module (CInclude/geosmod.h).  GeodeLoad
 *
 *   - loads the module once with dummy imports to read the libraries it
 *     uses (a module is self-describing only after it is loaded), and
 *     loads or references those libraries;
 *   - loads the module for good; imports are resolved by name, from the
 *     kernel's export table (kernelExports.c, Port32/tools/kexports.py)
 *     and from the exports of the libraries;
 *   - allocates the core block, builds the LMem resources made by goc
 *     (LMemCreateResourceBlock), registers the resource handles,
 *     relocates the optrs in chunks that are not objects and the object
 *     blocks;
 *   - calls the library entry with LCT_NEW_CLIENT for each library and,
 *     for a library, its own with LCT_ATTACH;
 *   - for a process, creates the event queue, queues MSG_META_ATTACH
 *     and starts the process thread with the process class.
 *
 * A library is freed when its reference count drops to 0, a process when
 * its last thread exits.
 *
 * The image is one the program registered (GeodeRegisterImage) under the
 * name, or the contents of the file of that name (from the current path;
 * a library's from SP_SYSTEM, geodesLibrary.c).
 *
 * A driver gets DR_INIT once it is loaded and DR_EXIT before it is
 * freed (geodesDriver.c).
 *
 * Not yet: "noload" libraries, resources loaded on demand (RESF_DISCARDED: all are built
 * at once), heapspace and the UI thread.
 */
#include "kernelC.h"
#include "geosmod.h"
#include <vm.h>
#include "ghal.h"

#include <string.h>

typedef struct {
    const char	*name;
    const void	*addr;
    word	entry;		/* its entry number in geos.gp */
} KernelExport;
extern const KernelExport kernelExports[];
extern const unsigned kernelExportCount;

#define CORE(gh)	((GeodeCore *) MemDeref(gh))
#define INFO(c)		((const GeosModuleInfo *) (c)->GC_info)
#define NAME_LEN	(GEODE_NAME_SIZE + GEODE_NAME_EXT_SIZE)

/* ---------------------------------------------------------------------
 * The geode list and its lock (geodeSem)
 * ------------------------------------------------------------------- */

static GeodeHandle geodeList;
static KSem geodeSem = { 1, 0 };
static ThreadHandle geodeLockOwner;
static word geodeLockNesting;

void GeodeLock(void)
{
    ThreadHandle me = KernelThisThread();

    if (geodeLockOwner == me) {
	geodeLockNesting++;
	return;
    }
    KSemP(&geodeSem, KSEM_FOREVER);
    geodeLockOwner = me;
    geodeLockNesting = 1;
}

void GeodeUnlock(void)
{
    if (--geodeLockNesting)
	return;
    geodeLockOwner = 0;
    KSemV(&geodeSem);
}

GeodeCore *GeodeCoreOf(GeodeHandle gh)
{
    GeodeHandle g;

    if (!gh)
	return NULL;
    for (g = geodeList; g; g = CORE(g)->GC_next)
	if (g == gh)
	    return CORE(g);
    return NULL;
}

GeodeHandle GeodeFindLow(const char *name, word n, GeodeAttrs attrMatch,
			 GeodeAttrs attrNoMatch)
{
    GeodeHandle g;
    GeodeCore *c;

    if (n > NAME_LEN)
	n = NAME_LEN;
    for (g = geodeList; g; g = c->GC_next) {
	c = CORE(g);
	if (memcmp(c->GC_name, name, n) == 0 &&
	    (c->GC_attrs & attrMatch) == attrMatch &&
	    !(c->GC_attrs & attrNoMatch))
	    return g;
    }
    return 0;
}

/* ---------------------------------------------------------------------
 * Images: registered by the program, or read from the geode's file
 * ------------------------------------------------------------------- */

#define MAX_IMAGES	64

static struct {
    const char	*name;
    const void	*image;
    dword	size;
} images[MAX_IMAGES];

void GeodeRegisterImage(const char *name, const void *image, dword size)
{
    int i;

    for (i = 0; i < MAX_IMAGES; i++)
	if (!images[i].name || strcmp(images[i].name, name) == 0) {
	    images[i].name = name;
	    images[i].image = image;
	    images[i].size = size;
	    return;
	}
    KernelFatal(HANDLE_TABLE_FULL);
}

static int FindImage(const char *name)
{
    int i;

    for (i = 0; i < MAX_IMAGES && images[i].name; i++)
	if (strcmp(images[i].name, name) == 0)
	    return images[i].image ? i : -1;
    return -1;
}

Boolean GeodeHasImage(const char *name)
{
    return FindImage(name) >= 0;
}

/* the contents of a geode file (ghal_mem), after the open of
 * LoadGeodeLow; the module keeps nothing of it, so it is read whole and
 * the file is closed again (DOS16 kept it open for its resources) */
static void *ReadGeodeFile(const char *fileName, dword *size,
			   GeodeLoadError *err)
{
    FileHandle fh;
    byte *buf;
    dword n, done;
    word chunk, got;

    fh = FileOpen(fileName, FILE_ACCESS_R);
    if (!fh) {
	word e = ThreadGetError();
	*err = e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ||
	       e == ERROR_INVALID_VOLUME ? GLE_FILE_NOT_FOUND
					   : GLE_FILE_READ_ERROR;
	return NULL;
    }
    n = FileSize(fh);
    buf = n ? ghal_mem_alloc(n, 0) : NULL;
    if (!buf) {
	FileClose(fh, TRUE);
	*err = n ? GLE_MEMORY_ALLOCATION_ERROR : GLE_NOT_GEOS_FILE;
	return NULL;
    }
    for (done = 0; done < n; done += got) {
	chunk = n - done > 0x8000 ? 0x8000 : (word) (n - done);
	got = FileRead(fh, buf + done, chunk, TRUE);
	if (got != chunk) {
	    FileClose(fh, TRUE);
	    ghal_mem_free(buf);
	    *err = GLE_FILE_READ_ERROR;
	    return NULL;
	}
    }
    FileClose(fh, TRUE);
    *size = n;
    return buf;
}

/* ---------------------------------------------------------------------
 * Imports
 * ------------------------------------------------------------------- */

static const void *KernelSymbol(const char *name)
{
    unsigned lo = 0, hi = kernelExportCount;

    while (lo < hi) {
	unsigned mid = (lo + hi) / 2;
	int c = strcmp(name, kernelExports[mid].name);
	if (!c)
	    return kernelExports[mid].addr;
	if (c < 0)
	    hi = mid;
	else
	    lo = mid + 1;
    }
    return NULL;
}

/* libgcc's division by zero on ARM: raise(SIGFPE) */
static int GeodeRaise(int sig)
{
    (void) sig;
    KernelFatal(DIVIDE_BY_ZERO);
    return -1;
}

#if defined(GEOS_HOST_DOS32) && defined(__GNUC__)
/* a DXE3 with unwind tables registers them with the kernel's libgcc */
extern void __register_frame_info(const void *, void *);
extern void *__deregister_frame_info(const void *);
#endif

static const KernelExport hostExports[] = {
    { "raise", (const void *) GeodeRaise, 0 },
#if defined(GEOS_HOST_DOS32) && defined(__GNUC__)
    { "__register_frame_info", (const void *) __register_frame_info, 0 },
    { "__deregister_frame_info", (const void *) __deregister_frame_info, 0 },
#endif
};

/* what library `lib' exports by that name: an export "as" the name, or
 * a symbol of the module that is one of its exported entries */
static const void *LibrarySymbol(GeodeHandle lib, const char *name)
{
    const GeosModuleInfo *info = INFO(CORE(lib));
    const void *a;
    word i;

    for (i = 0; i < info->GM_exportCount; i++)
	if (info->GM_exports[i].GME_name &&
	    strcmp(info->GM_exports[i].GME_name, name) == 0)
	    return info->GM_exports[i].GME_address;
    a = ghal_module_symbol(CORE(lib)->GC_module, name);
    if (a)
	for (i = 0; i < info->GM_exportCount; i++)
	    if (info->GM_exports[i].GME_address == a)
		return a;
    return NULL;
}

typedef struct {
    const GeodeHandle	*libs;
    word		count;
} ImportCtx;

static void *Import(void *ctx, const char *name)
{
    const ImportCtx *ic = ctx;
    const void *a = KernelSymbol(name);
    word i;

    for (i = 0; !a && i < sizeof(hostExports) / sizeof(hostExports[0]); i++)
	if (strcmp(hostExports[i].name, name) == 0)
	    a = hostExports[i].addr;
    for (i = 0; !a && i < ic->count; i++)
	if (ic->libs[i])
	    a = LibrarySymbol(ic->libs[i], name);
    return (void *) a;
}

/* the first load, only to read __geos_module: every import is valid.
 * Loading runs the module's constructors, and those of a DXE3 with
 * unwind tables (64-bit division from libgcc, say) call
 * __register_frame_info at once: the kernel's and the host's own
 * routines are the real ones, only library imports are a dummy. */
static void *ProbeImport(void *ctx, const char *name)
{
    static dword dummy[4];
    const void *a = KernelSymbol(name);
    word i;

    (void) ctx;
    for (i = 0; !a && i < sizeof(hostExports) / sizeof(hostExports[0]); i++)
	if (strcmp(hostExports[i].name, name) == 0)
	    a = hostExports[i].addr;
    return a ? (void *) a : dummy;
}

static const GeosModuleInfo *ModuleInfo(ghal_module *m)
{
    const GeosModuleInfo *info = ghal_module_symbol(m, "__geos_module");

    if (!info || info->GM_magic != GEOS_MODULE_MAGIC ||
	info->GM_version != GEOS_MODULE_VERSION)
	return NULL;
    return info;
}

/* ---------------------------------------------------------------------
 * Resources
 * ------------------------------------------------------------------- */

static void ResourceFlags(word resf, HeapFlags *hf, HeapAllocFlags *haf)
{
    *hf = 0;
    *haf = 0;
    if (resf & RESF_FIXED)
	*hf |= HF_FIXED;
    if (resf & RESF_SHARED)
	*hf |= HF_SHARABLE;
    if (resf & RESF_DISCARDABLE)
	*hf |= HF_DISCARDABLE;
    if (resf & RESF_SWAPABLE)
	*hf |= HF_SWAPABLE;
    if (resf & RESF_UI)
	*haf |= HAF_UI;
    if (resf & RESF_READ_ONLY)
	*haf |= HAF_READ_ONLY;
    if (resf & RESF_OBJECT)
	*haf |= HAF_OBJECT_RESOURCE;
}

/* build the LMem resources made by goc; FALSE if there is no memory */
static Boolean CreateResources(GeodeHandle gh, const GeosModuleInfo *info)
{
    Handle *handles = info->GM_resourceHandles;
    word r;

    handles[0] = gh;
    for (r = 2; r < info->GM_resourceCount; r++) {
	const GeosModuleResource *res = &info->GM_resources[r];
	HeapFlags hf;
	HeapAllocFlags haf;

	handles[r] = 0;
	if (!res->GMR_resource)
	    continue;			/* code or data: part of the module */
	ResourceFlags(res->GMR_flags, &hf, &haf);
	handles[r] = LMemCreateResourceBlock(res->GMR_resource, gh, hf, haf);
	if (!handles[r])
	    return FALSE;
    }
    GeodeSetResourceHandles(gh, handles, info->GM_resourceCount);
    /* optrs in chunks that are not objects; objects are relocated when
     * their exec thread is known */
    for (r = 2; r < info->GM_resourceCount; r++)
	if (handles[r])
	    ObjRelocateResourceChunks(handles[r],
				      info->GM_resources[r].GMR_resource);
    return TRUE;
}

/* the thread that runs the object blocks; then they are relocated */
static void RelocateObjectBlocks(const GeosModuleInfo *info,
				 ThreadHandle burden)
{
    word r;

    for (r = 2; r < info->GM_resourceCount; r++)
	if (info->GM_resourceHandles[r] &&
	    (info->GM_resources[r].GMR_flags & RESF_OBJECT)) {
	    MemModifyOtherInfo(info->GM_resourceHandles[r], burden);
	    ObjRelocateObjBlock(info->GM_resourceHandles[r],
				VMRT_RELOCATE_FROM_RESOURCE);
	}
}

/* ---------------------------------------------------------------------
 * Freeing
 * ------------------------------------------------------------------- */

static int OwnedHandle(Handle h, HandleEntry *he, void *ctx)
{
    GeodeHandle gh = *(GeodeHandle *) ctx;

    if (he->HE_owner == gh && h != gh &&
	(he->HE_type == HT_MEM || he->HE_type == SIG_QUEUE ||
	 he->HE_type == SIG_SAVED_BLOCK || he->HE_type == SIG_FILE ||
	 he->HE_type == SIG_VM))
	return (int) h;
    return 0;
}

/* the blocks, queues, saved-block entries, open files and VM files
 * the geode owns
 * (not its core block) */
static void FreeOwned(GeodeHandle gh)
{
    Handle h;

    CORE(gh)->GC_savedBlocks = 0;
    while ((h = (Handle) HandleEnum(OwnedHandle, &gh)) != 0) {
	switch (HandleEntryOf(h)->HE_type) {
	case SIG_QUEUE:
	    GeodeFreeQueue(h);
	    break;
	case SIG_SAVED_BLOCK:
	    HandleFree(h);
	    break;
	case SIG_FILE:
	    FileFreeHandle(h);
	    break;
	case SIG_VM:
	    VMFreeHandle(h);
	    break;
	default:
	    MemFree(h);
	}
    }
}

const GocResource *GeodeResourceImage(GeodeHandle gh, word res, word *resf)
{
    const GocResource *r = NULL;
    const GeosModuleInfo *info;
    GeodeCore *c;

    GeodeLock();
    c = GeodeCoreOf(gh);
    if (c) {
	info = INFO(c);
	if (res >= 2 && res < info->GM_resourceCount) {
	    r = info->GM_resources[res].GMR_resource;
	    *resf = info->GM_resources[res].GMR_flags;
	}
    }
    GeodeUnlock();
    return r;
}

static void Unlink(GeodeHandle gh)
{
    GeodeHandle *p = &geodeList;

    while (*p && *p != gh)
	p = &CORE(*p)->GC_next;
    if (*p)
	*p = CORE(gh)->GC_next;
}

static GeodeLibraryEntry *EntryOf(GeodeHandle gh)
{
    GeodeCore *c = GeodeCoreOf(gh);

    return c ? (GeodeLibraryEntry *) INFO(c)->GM_entry : NULL;
}

/* tell the libraries of `gh' that a client comes or goes */
static void CallLibraries(GeodeHandle gh, LibraryCallType ty)
{
    GeodeCore *c = CORE(gh);
    word i;

    for (i = 0; i < c->GC_libraryCount; i++) {
	GeodeLibraryEntry *e = EntryOf(c->GC_libraries[i]);
	if (e)
	    e(ty, gh);
    }
}

void GeodeFreeLow(GeodeHandle gh)
{
    GeodeCore *c;
    GeodeHandle libs[64];
    word n, i;

    GeodeLock();
    c = GeodeCoreOf(gh);
    if (!c)
	KernelFatal(GEODE_NOT_LOADED);
    if (c->GC_attrs & GA_LIBRARY_INITIALIZED) {
	GeodeLibraryEntry *e = EntryOf(gh);
	if (e)
	    e(LCT_DETACH, 0);
    }
    if (c->GC_attrs & GA_DRIVER_INITIALIZED)
	GeodeExitDriver(gh);
    if (c->GC_attrs & GA_GEODE_INITIALIZED)
	CallLibraries(gh, LCT_CLIENT_EXIT);
    GeodePrivExit(gh);
    ThreadPrivExit(gh);
    Unlink(gh);
    GeodeSetResourceHandles(gh, NULL, 0);
    FreeOwned(gh);
    c = CORE(gh);
    ghal_module_unload(c->GC_module);
    n = c->GC_libraryCount;
    if (n > sizeof(libs) / sizeof(libs[0]))
	n = sizeof(libs) / sizeof(libs[0]);
    memcpy(libs, c->GC_libraries, n * sizeof(libs[0]));
    MemFree(gh);
    for (i = 0; i < n; i++)
	if (libs[i])
	    GeodeFreeLibraryLow(libs[i]);
    GeodeUnlock();
}

/* ---------------------------------------------------------------------
 * Threads
 * ------------------------------------------------------------------- */

void GeodeCount(word *total, word *processes, word *libraries,
		word *drivers)
{
    GeodeHandle g;

    *total = *processes = *libraries = *drivers = 0;
    GeodeLock();
    for (g = geodeList; g; g = CORE(g)->GC_next) {
	GeodeAttrs a = CORE(g)->GC_attrs;
	(*total)++;
	if (a & GA_PROCESS)
	    (*processes)++;
	if (a & GA_LIBRARY)
	    (*libraries)++;
	if (a & GA_DRIVER)
	    (*drivers)++;
    }
    GeodeUnlock();
}

/* ---------------------------------------------------------------------
 * References (geodesLoad.asm GeodeAddReference, geodesUtils.asm
 * GeodeRemoveReference)
 * ------------------------------------------------------------------- */

void GeodeAddReference(GeodeHandle gh)
{
    GeodeCore *c;

    GeodeLock();
    c = GeodeCoreOf(gh);
    if (!c)
	KernelFatal(GEODE_NOT_LOADED);
    c->GC_refCount++;
    GeodeUnlock();
}

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

void GeodeAddThread(GeodeHandle gh)
{
    GeodeCore *c;

    GeodeLock();
    c = GeodeCoreOf(gh);
    if (c)
	c->GC_threadCount++;
    GeodeUnlock();
}

void GeodeRemoveThread(GeodeHandle gh)
{
    GeodeCore *c;

    GeodeLock();
    c = GeodeCoreOf(gh);
    if (c && c->GC_threadCount && !--c->GC_threadCount &&
	(c->GC_attrs & GA_PROCESS))
	GeodeFreeLow(gh);
    GeodeUnlock();
}

/* ---------------------------------------------------------------------
 * Loading
 * ------------------------------------------------------------------- */

/* the libraries the module uses, read from a first load */
typedef struct {
    char	name[GEODE_NAME_SIZE + 1];
    word	protoMajor, protoMinor, flags;
} LibraryNeed;

#define MAX_LIBRARIES	32

static GeodeLoadError Probe(const void *image, dword size, char *name,
			    GeodeAttrs *attrs, LibraryNeed *libs, word *nlibs)
{
    ghal_module *m;
    const GeosModuleInfo *info;
    ghal_status st;
    word i;

    st = ghal_module_load(image, size, ProbeImport, NULL, &m);
    if (st != GHAL_OK)
	return st == GHAL_ERR_NOMEM ? GLE_MEMORY_ALLOCATION_ERROR
				    : GLE_NOT_GEOS_FILE;
    info = ModuleInfo(m);
    if (!info || info->GM_libraryCount > MAX_LIBRARIES) {
	ghal_module_unload(m);
	return GLE_NOT_GEOS_EXECUTABLE_FILE;
    }
    memcpy(name, info->GM_name, GEODE_NAME_SIZE);
    memcpy(name + GEODE_NAME_SIZE, info->GM_ext, GEODE_NAME_EXT_SIZE);
    *attrs = info->GM_attrs;
    *nlibs = info->GM_libraryCount;
    for (i = 0; i < *nlibs; i++) {
	const GeosModuleLibrary *l = &info->GM_libraries[i];
	strncpy(libs[i].name, l->GML_name, GEODE_NAME_SIZE);
	libs[i].name[GEODE_NAME_SIZE] = 0;
	libs[i].protoMajor = l->GML_protoMajor;
	libs[i].protoMinor = l->GML_protoMinor;
	libs[i].flags = l->GML_flags;
    }
    ghal_module_unload(m);
    return GLE_OK;
}

static ThreadHandle StartProcess(GeodeHandle gh, const GeosModuleInfo *info,
				 word priority, dword appInfo)
{
    struct { word value1, value2; } p;
    QueueHandle q;
    ThreadHandle th;

    q = GeodeAllocQueue();
    HandleEntryOf(q)->HE_owner = gh;
    /* MSG_META_ATTACH(cx, dx): appInfo high and low */
    p.value1 = (word) (appInfo >> 16);
    p.value2 = (word) appInfo;
    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(q, 0), KMSG_META_ATTACH,
		 &p, sizeof(p), NULL, 0);
    th = KernelEventThreadCreate(q, info->GM_processClass,
				 (ThreadPriority) (priority ? priority
							    : PRIORITY_STANDARD),
				 info->GM_stackSize, gh);
    if (!th)
	return 0;
    MemModifyOtherInfo(gh, th);
    RelocateObjectBlocks(info, th);
    return th;
}

GeodeHandle GeodeLoadLow(const char *fileName, GeodeAttrs attrMatch,
			 GeodeAttrs attrNoMatch, word priority, dword appInfo,
			 GeodeLoadError *err)
{
    LibraryNeed need[MAX_LIBRARIES];
    GeodeHandle libs[MAX_LIBRARIES], gh;
    char name[NAME_LEN];
    GeodeAttrs attrs;
    word nlibs = 0, i;
    int img;
    const void *image;
    void *file = NULL;
    dword size;
    ghal_status st;
    ghal_module *m;
    const GeosModuleInfo *info;
    GeodeCore *c;
    ImportCtx ic;
    ThreadHandle th = 0;

    memset(libs, 0, sizeof(libs));
    *err = GLE_OK;
    img = FindImage(fileName);
    if (img >= 0) {
	image = images[img].image;
	size = images[img].size;
    } else {
	image = file = ReadGeodeFile(fileName, &size, err);
	if (!file)
	    return 0;
    }
    *err = Probe(image, size, name, &attrs, need, &nlibs);
    if (*err != GLE_OK)
	goto freeFile;
    if ((attrs & attrMatch) != attrMatch || (attrs & attrNoMatch)) {
	*err = GLE_ATTRIBUTE_MISMATCH;
	goto freeFile;
    }

    /* loaded already: a library is shared, a process may run again only
     * if it is multi-launchable */
    gh = GeodeFindLow(name, NAME_LEN, 0, 0);
    if (gh) {
	if (!(attrs & GA_PROCESS)) {
	    CORE(gh)->GC_refCount++;
	    if (file)
		ghal_mem_free(file);
	    return gh;
	}
	if (!(attrs & GA_MULTI_LAUNCHABLE)) {
	    *err = GLE_NOT_MULTI_LAUNCHABLE;
	    goto freeFile;
	}
    }

    /* the libraries first: the module imports from them */
    for (i = 0; i < nlibs; i++) {
	if (strcmp(need[i].name, "geos") == 0 || (need[i].flags & GML_NOLOAD))
	    continue;
	libs[i] = GeodeUseLibraryLow(need[i].name, need[i].protoMajor,
				     need[i].protoMinor, err);
	if (!libs[i])
	    goto freeLibs;
    }

    ic.libs = libs;
    ic.count = nlibs;
    st = ghal_module_load(image, size, Import, &ic, &m);
    if (file) {
	ghal_mem_free(file);
	file = NULL;
    }
    switch (st) {
    case GHAL_OK:
	break;
    case GHAL_ERR_UNRESOLVED:
	*err = GLE_LIBRARY_NOT_FOUND;
	goto freeLibs;
    case GHAL_ERR_NOMEM:
	*err = GLE_MEMORY_ALLOCATION_ERROR;
	goto freeLibs;
    default:
	*err = GLE_NOT_GEOS_FILE;
	goto freeLibs;
    }
    info = ModuleInfo(m);

    /* the core block: fixed, owns itself */
    gh = MemAllocSetOwner(kernelCurrentGeode,
			  (word) (sizeof(GeodeCore) +
				  nlibs * sizeof(GeodeHandle)),
			  HF_FIXED, HAF_ZERO_INIT);
    if (!gh) {
	ghal_module_unload(m);
	*err = GLE_MEMORY_ALLOCATION_ERROR;
	goto freeLibs;
    }
    HandleEntryOf(gh)->HE_owner = gh;
    c = CORE(gh);
    c->GC_handle = gh;
    c->GC_module = m;
    c->GC_info = info;
    memcpy(c->GC_name, name, NAME_LEN);
    c->GC_attrs = attrs;
    c->GC_refCount = 2;			/* one for the duration of the load */
    c->GC_libraryCount = nlibs;
    memcpy(c->GC_libraries, libs, nlibs * sizeof(GeodeHandle));
    nlibs = 0;				/* the core block has them now */
    c->GC_next = geodeList;
    geodeList = gh;

    if (!CreateResources(gh, info)) {
	*err = GLE_MEMORY_ALLOCATION_ERROR;
	goto freeGeode;
    }
    CORE(gh)->GC_attrs |= GA_GEODE_INITIALIZED;
    CallLibraries(gh, LCT_NEW_CLIENT);

    if (!(attrs & GA_PROCESS))
	RelocateObjectBlocks(info, KernelThisThread());
    if (attrs & GA_DRIVER) {
	*err = GeodeInitDriver(gh, appInfo);
	if (*err != GLE_OK)
	    goto freeGeode;
    }
    if (attrs & GA_LIBRARY) {
	GeodeLibraryEntry *e = (GeodeLibraryEntry *) info->GM_entry;
	if (e && e(LCT_ATTACH, 0)) {
	    *err = GLE_LIBRARY_INIT_ERROR;
	    goto freeGeode;
	}
	CORE(gh)->GC_attrs |= GA_LIBRARY_INITIALIZED;
    }
    if (attrs & GA_PROCESS) {
	th = StartProcess(gh, info, priority, appInfo);
	if (!th) {
	    *err = GLE_MEMORY_ALLOCATION_ERROR;
	    goto freeGeode;
	}
    }

    CORE(gh)->GC_refCount--;
    if (th)
	KernelThreadRun(th);
    return gh;

freeGeode:
    GeodeFreeLow(gh);
    return 0;
freeLibs:
    for (i = 0; i < nlibs; i++)
	if (libs[i])
	    GeodeFreeLibraryLow(libs[i]);
freeFile:
    if (file)
	ghal_mem_free(file);
    return 0;
}

GeodeHandle GeodeLoad(const char *name, GeodeAttrs attrMatch,
		      GeodeAttrs attrNoMatch, word priority, dword appInfo,
		      GeodeLoadError *err)
{
    GeodeLoadError e;
    GeodeHandle gh;

    GeodeLock();
    gh = GeodeLoadLow(name, attrMatch, attrNoMatch, priority, appInfo, &e);
    GeodeUnlock();
    if (err)
	*err = e;
    return gh;
}

/* ---------------------------------------------------------------------
 * Entries as unrelocated references (classes in saved object blocks)
 * ------------------------------------------------------------------- */

/* the kernel's classes where it has no export table (Open Watcom), with
 * their entry numbers in geos.gp (kexports.py) */
extern ClassStruct MetaClass, ProcessClass;
static const struct {
    const void	*addr;
    word	entry;
} kernelClasses[] = { { &MetaClass, 0x2c6 }, { &ProcessClass, 0x2c7 } };
#define N_KERNEL_CLASSES (sizeof(kernelClasses) / sizeof(kernelClasses[0]))

static Boolean InExports(const GeosModuleInfo *info, const void *addr,
			 dword *entry)
{
    word i;

    for (i = 0; info && i < info->GM_exportCount; i++)
	if (info->GM_exports[i].GME_address == addr) {
	    *entry = i;
	    return TRUE;
	}
    return FALSE;
}

Boolean GeodeEntryToReloc(GeodeHandle owner, const void *addr, dword *code)
{
    GeodeCore *c;
    dword e;
    word i;
    Boolean found = FALSE;

    GeodeLock();
    c = GeodeCoreOf(owner);
    if (c && InExports(INFO(c), addr, &e)) {
	*code = KUR_MAKE(KUR_ENTRY_OWNER, e);
	found = TRUE;
    }
    for (i = 0; c && !found && i < c->GC_libraryCount; i++) {
	GeodeCore *l = GeodeCoreOf(c->GC_libraries[i]);
	if (l && InExports(INFO(l), addr, &e) && e <= 0xffff) {
	    *code = KUR_MAKE(KUR_ENTRY_LIBRARY, ((dword) i << 16) | e);
	    found = TRUE;
	}
    }
    GeodeUnlock();
    if (found)
	return TRUE;
    for (i = 0; i < kernelExportCount; i++)
	if (kernelExports[i].addr == addr) {
	    *code = KUR_MAKE(KUR_ENTRY_KERNEL, kernelExports[i].entry);
	    return TRUE;
	}
    if (!kernelExportCount)
	for (i = 0; i < N_KERNEL_CLASSES; i++)
	    if (kernelClasses[i].addr == addr) {
		*code = KUR_MAKE(KUR_ENTRY_KERNEL, kernelClasses[i].entry);
		return TRUE;
	    }
    return FALSE;
}

const void *GeodeRelocToEntry(GeodeHandle owner, dword code)
{
    dword idx = KUR_INDEX(code);
    const void *a = NULL;
    GeodeCore *c;

    switch (KUR_SOURCE(code)) {
    case KUR_ENTRY_OWNER:
    case KUR_ENTRY_LIBRARY:
	GeodeLock();
	c = GeodeCoreOf(owner);
	if (c && KUR_SOURCE(code) == KUR_ENTRY_LIBRARY) {
	    word lib = (word) (idx >> 16);
	    c = lib < c->GC_libraryCount ? GeodeCoreOf(c->GC_libraries[lib])
					 : NULL;
	    idx &= 0xffff;
	}
	if (c && INFO(c) && idx < INFO(c)->GM_exportCount)
	    a = INFO(c)->GM_exports[idx].GME_address;
	GeodeUnlock();
	return a;
    case KUR_ENTRY_KERNEL:
	if (kernelExportCount) {
	    unsigned i;
	    for (i = 0; i < kernelExportCount; i++)
		if (kernelExports[i].entry == idx)
		    return kernelExports[i].addr;
	    return NULL;
	}
	{
	    unsigned i;
	    for (i = 0; i < N_KERNEL_CLASSES; i++)
		if (kernelClasses[i].entry == idx)
		    return kernelClasses[i].addr;
	}
	return NULL;
    default:
	return NULL;
    }
}
