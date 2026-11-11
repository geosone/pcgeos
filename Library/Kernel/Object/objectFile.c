/*
 * objectFile.c -- the state file of a process (after objectFile.asm).
 *
 * ObjAssocVMFile gives the running process a state file.  A new file
 * gets the protocols of the process (FEA_PROTOCOL, FEA_RELEASE) and of
 * its libraries (the map block); an old one must match them, and then
 * what it holds comes back:
 *
 *  - the saved blocks (VM IDs VM_SAVED_UI and VM_SAVED_APP: duplicated
 *    resources, ObjSaveBlock) as blocks of the process, on the
 *    saved-block list with their VM block handle as VM ID;
 *  - the chunks saved of each detachable object resource (VM ID: the
 *    resource number), merged into the resource (MergeObjBlock);
 *  - the extra block (VM_EXTRA_DATA, ObjSaveExtraStateBlock).
 *
 * The asm kernel did this when a block was first locked (FullObjLock),
 * because its resources came from the geode file then.  Port32 builds
 * the resources when the geode is loaded, so it is done here, at once:
 * the saved blocks first (their handles are needed to relocate), then
 * the resources.  A resource is unrelocated, the saved chunks merged
 * into it, and it is relocated again.
 *
 * ObjDisassocVMFile writes the detachable object resources and the
 * saved blocks to the file: each as a copy, compacted (CompactObjBlock:
 * clean resource chunks become empty, chunks that are neither dirty nor
 * from the resource go, master parts of DISCARD_ON_SAVE classes above a
 * variant go, and variable data without VDF_SAVE_TO_STATE) and
 * unrelocated for the file (objectReloc.c, Port32/ABI.md section 13d).
 * The blocks themselves stay as they are: the process is about to exit.
 * Then ObjCloseVMFile closes the file.
 */
#include "kernelC.h"
#include "../VMem/vmemInt.h"

#include <string.h>

/* VM user IDs of the state file (objectConstant.def) */
#define VM_SAVED_ATTACHED	0xffff	/* block is in memory */
#define VM_SAVED_UI		0xfffe	/* run by the UI thread */
#define VM_SAVED_APP		0xfffd	/* run by a thread of the process */
#define VM_EXTRA_DATA		0xfffc	/* ObjSaveExtraStateBlock */

/* ---------------------------------------------------------------------
 * The process and its state file
 * ------------------------------------------------------------------- */

static GeodeHandle Process(void)
{
    return GeodeGetProcessHandle();
}

static GeodeCore *CoreOfProcess(void)
{
    GeodeCore *c = GeodeCoreOf(Process());

    if (!c)
	KernelFatal(GEODE_NOT_LOADED);
    return c;
}

/* the first thread of `gh' (ProcInfo): the core block names it */
static ThreadHandle FirstThread(GeodeHandle gh)
{
    return (ThreadHandle) MemGetInfo(gh, MGIT_OTHER_INFO);
}

VMFileHandle ObjStateFile(GeodeHandle gh)
{
    GeodeCore *c = GeodeCoreOf(gh);

    return c ? c->GC_vmHandle : 0;
}

/* GetSavedBlockVMUserID: who runs the block */
word ObjSavedBlockUserID(MemHandle mh)
{
    return (ThreadHandle) MemGetInfo(mh, MGIT_OTHER_INFO) ==
	   FirstThread(MemOwner(mh)) ? VM_SAVED_APP : VM_SAVED_UI;
}

/* ---------------------------------------------------------------------
 * Protocols
 * ------------------------------------------------------------------- */

static void SetProtocol(VMFileHandle file)
{
    GeodeCore *c = CoreOfProcess();
    ReleaseNumber rel;
    ProtocolNumber proto;
    FileExtAttrDesc attrs[2];
    VMBlockHandle map;
    MemHandle mh;
    byte *p;
    word i;

    GeodeGetInfo(0, GGIT_GEODE_RELEASE, &rel);
    GeodeGetInfo(0, GGIT_GEODE_PROTOCOL, &proto);
    attrs[0].FEAD_attr = FEA_RELEASE;
    attrs[0].FEAD_value = &rel;
    attrs[0].FEAD_size = sizeof(rel);
    attrs[0].FEAD_name = NULL;
    attrs[1].FEAD_attr = FEA_PROTOCOL;
    attrs[1].FEAD_value = &proto;
    attrs[1].FEAD_size = sizeof(proto);
    attrs[1].FEAD_name = NULL;
    FileSetHandleExtAttributes(VMFileOf(file)->VF_file, FEA_MULTIPLE, attrs,
			       2);
    /* the map block: the count, then each library's protocol */
    map = VMAlloc(file, (word) (2 + c->GC_libraryCount * 4), 0);
    VMSetMapBlock(file, map);
    p = VMLock(file, map, &mh);
    p[0] = (byte) c->GC_libraryCount;
    p[1] = (byte) (c->GC_libraryCount >> 8);
    for (i = 0; i < c->GC_libraryCount; i++) {
	GeodeGetInfo(c->GC_libraries[i], GGIT_GEODE_PROTOCOL, &proto);
	p[2 + 4 * i] = (byte) proto.PN_major;
	p[3 + 4 * i] = (byte) (proto.PN_major >> 8);
	p[4 + 4 * i] = (byte) proto.PN_minor;
	p[5 + 4 * i] = (byte) (proto.PN_minor >> 8);
    }
    VMDirty(mh);
    VMUnlock(mh);
}

/* CompareProto: the same major number, a minor number not newer */
static Boolean BadProto(word major, word minor, const ProtocolNumber *now)
{
    return major != now->PN_major || minor > now->PN_minor;
}

/* TRUE if the file does not fit the process and its libraries */
static Boolean CheckProtocol(VMFileHandle file)
{
    GeodeCore *c = CoreOfProcess();
    ProtocolNumber fileProto, proto;
    MemHandle mh;
    const byte *p;
    Boolean bad = FALSE;
    word i;

    memset(&fileProto, 0, sizeof(fileProto));
    FileGetHandleExtAttributes(VMFileOf(file)->VF_file, FEA_PROTOCOL,
			       &fileProto, sizeof(fileProto));
    GeodeGetInfo(0, GGIT_GEODE_PROTOCOL, &proto);
    if (BadProto(fileProto.PN_major, fileProto.PN_minor, &proto))
	return TRUE;
    p = VMLock(file, VMGetMapBlock(file), &mh);
    if (!p)
	return TRUE;
    if ((word) (p[0] | (p[1] << 8)) != c->GC_libraryCount)
	bad = TRUE;
    for (i = 0; !bad && i < c->GC_libraryCount; i++) {
	GeodeGetInfo(c->GC_libraries[i], GGIT_GEODE_PROTOCOL, &proto);
	bad = BadProto((word) (p[2 + 4 * i] | (p[3 + 4 * i] << 8)),
		       (word) (p[4 + 4 * i] | (p[5 + 4 * i] << 8)), &proto);
    }
    VMUnlock(mh);
    return bad;
}

/* ---------------------------------------------------------------------
 * Restoring
 * ------------------------------------------------------------------- */

/* the contents of VM block `vb', given to the process (0: none) */
static MemHandle TakeBlock(VMFileHandle file, VMBlockHandle vb)
{
    VMInfoStruct info;

    if (!VMInfo(file, vb, &info) || !info.size)
	return 0;			/* written before a crash */
    return VMDetach(file, vb, Process());
}

/* AllocAllSavedBlocks: the saved blocks of one thread */
static void RestoreSaved(VMFileHandle file, word uid, ThreadHandle thread)
{
    VMBlockHandle vb;
    MemHandle mh;

    while ((vb = VMFind(file, 0, uid)) != 0) {
	VMModifyUserID(file, vb, VM_SAVED_ATTACHED);
	mh = TakeBlock(file, vb);
	if (!mh)
	    continue;
	MemModifyOtherInfo(mh, thread);
	ObjAddToSavedList(mh, vb);
    }
}

/* MergeObjBlock: the chunks of the state block `sb' into resource `mh';
 * both unrelocated, with the same chunk handles */
static void MergeObjBlock(MemHandle mh, MemHandle sb)
{
    const byte *s = MemLock(sb);
    const LMemBlockHeader *sh = (const LMemBlockHeader *) s;
    word n = sh->LMBH_nHandles, i;
    ChunkHandle table = sh->LMBH_offset;
    word flagsAt = *(const word *) (s + table);

    MemLock(mh);
    for (i = 1; i < n; i++) {
	ChunkHandle ch = (ChunkHandle) (table + 2 * i);
	word e = *(const word *) (s + ch);
	byte fl = s[flagsAt + i];
	optr o = ConstructOptr(mh, ch);
	word size;

	if (e == 0)
	    continue;
	size = e == 0xffff ? 0 : (word) (*(const word *) (s + e - 2) - 2);
	if (e == 0xffff && (fl & OCF_IN_RESOURCE)) {
	    /* empty in the state: from the resource, unless it was dirty
	     * (a chunk made empty) */
	    if (!(fl & OCF_DIRTY) || (fl & OCF_IGNORE_DIRTY))
		continue;
	    LMemReAlloc(o, 0);
	} else if (fl & OCF_IN_RESOURCE) {
	    if (LMemReAlloc(o, size))
		KernelFatal(OBJ_LOAD_ERROR);
	} else {
	    /* made on the fly: the same handle again */
	    if (ch < ((LMemBlockHeader *) MemDeref(mh))->LMBH_offset +
		     2 * ((LMemBlockHeader *) MemDeref(mh))->LMBH_nHandles &&
		*(word *) ((byte *) MemDeref(mh) + ch)) {
		if (LMemReAlloc(o, size))
		    KernelFatal(OBJ_LOAD_ERROR);
	    } else if (!LMemAllocAt(mh, ch, size))
		KernelFatal(CANNOT_ALLOCATE_CORRECT_CHUNK_HANDLE);
	}
	s = MemDeref(sb);
	if (size)
	    memcpy(LMemDeref(o), s + e, size);
	/* the flags of the state block: OCF_VARDATA_RELOC among them */
	ObjSetFlags(o, fl, (ObjChunkFlags) ~fl);
    }
    MemUnlock(mh);
    MemUnlock(sb);
}

/* the detachable object resources of the process */
static Boolean Detachable(MemHandle mh)
{
    LMemBlockHeader *h;
    Boolean yes;

    if (!(MemGetInfo(mh, MGIT_FLAGS_AND_LOCK_COUNT) & HF_LMEM))
	return FALSE;
    h = MemLock(mh);
    yes = h->LMBH_lmemType == LMEM_TYPE_OBJ_BLOCK &&
	  (h->LMBH_flags & LMF_DETACHABLE);
    MemUnlock(mh);
    return yes;
}

static void RestoreResources(VMFileHandle file)
{
    GeodeHandle gh = Process();
    MemHandle mh, sb;
    VMBlockHandle vb;
    word res;

    for (res = 1; res < GeodeResourceCount(gh); res++) {
	mh = GeodeResourceHandle(gh, res);
	if (!mh || !Detachable(mh) || !(vb = VMFind(file, 0, res)))
	    continue;
	sb = TakeBlock(file, vb);
	if (!sb)
	    continue;
	ObjRelocateForFile(mh, VMRT_UNRELOCATE_FROM_RESOURCE, TRUE);
	MergeObjBlock(mh, sb);
	MemFree(sb);
	ObjRelocateForFile(mh, VMRT_RELOCATE_FROM_RESOURCE, FALSE);
    }
}

Boolean ObjAssocVMFile(VMFileHandle file, MemHandle *extra)
{
    GeodeCore *c = CoreOfProcess();
    ThreadHandle app = FirstThread(Process());
    VMBlockHandle vb;
    Handle sb;

    if (extra)
	*extra = 0;
    c->GC_vmHandle = file;
    if (!VMGetMapBlock(file))
	SetProtocol(file);
    else if (CheckProtocol(file)) {
	c->GC_vmHandle = 0;
	return TRUE;
    }
    /* the saved blocks: run by the UI thread, then by the process */
    RestoreSaved(file, VM_SAVED_UI, c->GC_uiThread ? c->GC_uiThread : app);
    RestoreSaved(file, VM_SAVED_APP, app);
    /* their IDs back to who runs them (so the file stays usable if the
     * header is written), and VM blocks for the saved blocks made
     * before (odd IDs) */
    GeodeLock();
    for (sb = c->GC_savedBlocks; sb; sb = HandleEntryOf(sb)->HE_next) {
	HandleEntry *he = HandleEntryOf(sb);
	MemHandle mh = (MemHandle) he->HE_otherInfo;
	if (he->HE_size & 1)
	    he->HE_size = VMAlloc(file, 0, ObjSavedBlockUserID(mh));
	else
	    VMModifyUserID(file, (VMBlockHandle) he->HE_size,
			   ObjSavedBlockUserID(mh));
    }
    GeodeUnlock();
    /* relocated once all of them have handles */
    for (sb = c->GC_savedBlocks; sb; sb = HandleEntryOf(sb)->HE_next) {
	MemHandle mh = (MemHandle) HandleEntryOf(sb)->HE_otherInfo;
	ObjLMemBlockHeader *h = MemLock(mh);
	Boolean restored = !(h->OLMBH_header.LMBH_flags & LMF_RELOCATED);
	if (restored) {
	    h->OLMBH_inUseCount = 0;
	    h->OLMBH_interactibleCount = 0;
	    h->OLMBH_header.LMBH_handle = mh;
	}
	MemUnlock(mh);
	if (restored)
	    ObjRelocateForFile(mh, VMRT_RELOCATE_FROM_RESOURCE, FALSE);
    }
    RestoreResources(file);
    vb = VMFind(file, 0, VM_EXTRA_DATA);
    if (vb && extra)
	*extra = TakeBlock(file, vb);
    return FALSE;
}

/* ---------------------------------------------------------------------
 * Saving
 * ------------------------------------------------------------------- */

/* the variable data range of a master level (0: MetaClass) */
static void LevelRange(word masterOffset, word *start, word *end)
{
    if (!masterOffset) {
	*start = 0;
	*end = FIRST_MASTER_MESSAGE - 1;
	return;
    }
    *start = (word) (FIRST_MASTER_MESSAGE +
		     ((masterOffset - sizeof(ClassStruct *)) / 2) *
		     DEFAULT_MASTER_MESSAGES);
    *end = (word) (*start + DEFAULT_MASTER_MESSAGES - 1);
}

/* the part of CompactObjBlock for an object that is saved */
static void CompactObject(optr o)
{
    ClassStruct *cls = ObjClassOf(o);
    Boolean prevVariant = FALSE;

    while (cls) {
	ClassStruct *super;
	word mo, start, end;
	Boolean discard;

	while (cls->Class_superClass &&
	       !(cls->Class_flags & CLASSF_MASTER_CLASS))
	    cls = cls->Class_superClass;
	mo = cls->Class_masterOffset;
	discard = prevVariant && (cls->Class_flags & CLASSF_DISCARD_ON_SAVE);
	if (cls->Class_flags & CLASSF_VARIANT_CLASS) {
	    word part = *(word *) ((byte *) LMemDeref(o) + mo);
	    if (!part)
		return;			/* nothing built here */
	    super = *(ClassStruct **) ((byte *) LMemDeref(o) + part);
	    prevVariant = TRUE;
	} else {
	    super = cls->Class_superClass;
	    prevVariant = FALSE;
	}
	LevelRange(mo, &start, &end);
	if (discard && mo) {
	    word below;
	    ObjResizeMaster(o, mo, 0);
	    *(word *) ((byte *) LMemDeref(o) + mo) = 0;
	    /* the variant below loses its superclass */
	    below = *(word *) ((byte *) LMemDeref(o) + mo + sizeof(word));
	    if (below)
		*(ClassStruct **) ((byte *) LMemDeref(o) + below) = NULL;
	    ObjVarDeleteDataRange(o, start, end, FALSE);
	} else
	    ObjVarDeleteDataRange(o, start, end, TRUE);
	cls = super;
    }
}

/* CompactObjBlock (COBTable): what each chunk becomes in the state */
static void CompactObjBlock(MemHandle mh)
{
    LMemBlockHeader *h = MemLock(mh);
    Boolean dup = (h->LMBH_flags & LMF_DUPLICATED) != 0;
    word n = h->LMBH_nHandles, i;

    for (i = 1; i < n; i++) {
	byte *b = MemDeref(mh);
	ChunkHandle ch = (ChunkHandle) (((LMemBlockHeader *) b)->LMBH_offset +
					2 * i);
	optr o = ConstructOptr(mh, ch);
	word e = *(word *) (b + ch);
	ObjChunkFlags f;
	Boolean dirty, ignore, inRes;

	if (e == 0 || e == 0xffff)
	    continue;
	f = ObjGetFlags(o);
	dirty = (f & OCF_DIRTY) != 0;
	ignore = (f & OCF_IGNORE_DIRTY) != 0;
	inRes = (f & OCF_IN_RESOURCE) != 0;
	if (inRes && !dup && (!dirty || ignore)) {
	    LMemReAlloc(o, 0);		/* from the resource again */
	    ObjSetFlags(o, 0, OCF_DIRTY);
	} else if ((dirty && !ignore) || (inRes && dup && !ignore)) {
	    if (f & OCF_IS_OBJECT)
		CompactObject(o);
	} else
	    LMemFree(o);
    }
    MemUnlock(mh);
}

/* a copy of `mh' to write: compacted, unrelocated */
static MemHandle StateCopy(MemHandle mh)
{
    word size = (word) MemGetInfo(mh, MGIT_SIZE);
    MemHandle cp = MemAllocSetOwner(MemOwner(mh), size,
				    HF_SWAPABLE | HF_LMEM, 0);
    LMemBlockHeader *h;

    if (!cp)
	KernelFatal(OBJ_LOAD_ERROR);
    memcpy(MemLock(cp), MemLock(mh), size);
    MemUnlock(mh);
    h = MemDeref(cp);
    h->LMBH_handle = cp;
    MemModifyOtherInfo(cp, MemGetInfo(mh, MGIT_OTHER_INFO));
    MemUnlock(cp);
    CompactObjBlock(cp);
    ObjRelocateForFile(cp, VMRT_UNRELOCATE_FROM_RESOURCE, TRUE);
    return cp;
}

/* DetachObjBlock + TransferToVM */
static void Transfer(VMFileHandle file, MemHandle mh, VMBlockHandle vb,
		     word uid)
{
    MemHandle cp = StateCopy(mh);

    vb = VMAttach(file, vb, cp);
    VMModifyUserID(file, vb, uid);
}

void ObjDisassocVMFile(void)
{
    GeodeCore *c = CoreOfProcess();
    VMFileHandle file = c->GC_vmHandle;
    GeodeHandle gh = Process();
    MemHandle mh;
    Handle sb;
    word res;

    if (!file)
	return;
    for (res = 1; res < GeodeResourceCount(gh); res++)
	if ((mh = GeodeResourceHandle(gh, res)) != 0 && Detachable(mh))
	    Transfer(file, mh, VMFind(file, 0, res), res);
    for (sb = c->GC_savedBlocks; sb; sb = HandleEntryOf(sb)->HE_next) {
	HandleEntry *he = HandleEntryOf(sb);
	mh = (MemHandle) he->HE_otherInfo;
	Transfer(file, mh, (VMBlockHandle) he->HE_size,
		 ObjSavedBlockUserID(mh));
    }
    ObjCloseVMFile();
}

void ObjCloseVMFile(void)
{
    GeodeCore *c = CoreOfProcess();
    VMFileHandle file;

    GeodeLock();
    while (c->GC_savedBlocks) {
	Handle sb = c->GC_savedBlocks;
	c->GC_savedBlocks = HandleEntryOf(sb)->HE_next;
	HandleFree(sb);
    }
    file = c->GC_vmHandle;
    c->GC_vmHandle = 0;
    GeodeUnlock();
    if (file)
	VMClose(file, FALSE);
}

void ObjSaveExtraStateBlock(MemHandle mh)
{
    VMFileHandle file;
    VMBlockHandle vb;

    if (!mh)
	return;
    file = CoreOfProcess()->GC_vmHandle;
    if (!file) {
	MemFree(mh);
	return;
    }
    vb = VMAttach(file, VMFind(file, 0, VM_EXTRA_DATA), mh);
    VMModifyUserID(file, vb, VM_EXTRA_DATA);
}

/* ---------------------------------------------------------------------
 * Saved blocks and their VM blocks
 * ------------------------------------------------------------------- */

VMBlockHandle ObjMapSavedToState(MemHandle mh)
{
    GeodeCore *c;
    Handle sb;
    VMBlockHandle vb = 0;

    GeodeLock();
    c = GeodeCoreOf(MemOwner(mh));
    for (sb = c ? c->GC_savedBlocks : 0; sb; sb = HandleEntryOf(sb)->HE_next)
	if (HandleEntryOf(sb)->HE_otherInfo == mh) {
	    vb = (VMBlockHandle) HandleEntryOf(sb)->HE_size;
	    break;
	}
    GeodeUnlock();
    return vb;
}

MemHandle ObjMapStateToSaved(VMBlockHandle vmbh, GeodeHandle gh)
{
    GeodeCore *c;
    Handle sb;
    MemHandle mh = 0;

    GeodeLock();
    c = GeodeCoreOf(gh ? gh : Process());
    for (sb = c ? c->GC_savedBlocks : 0; sb; sb = HandleEntryOf(sb)->HE_next)
	if ((VMBlockHandle) HandleEntryOf(sb)->HE_size == vmbh) {
	    mh = (MemHandle) HandleEntryOf(sb)->HE_otherInfo;
	    break;
	}
    GeodeUnlock();
    return mh;
}
