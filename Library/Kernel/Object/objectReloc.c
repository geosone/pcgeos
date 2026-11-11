/*
 * objectReloc.c -- relocating and unrelocating objects (after
 * objectReloc.asm).
 *
 * Unrelocated, the handle in an optr or hptr of an object block holds a
 * resource number of the block's owner with GOCR_UNRELOCATED set (goc
 * -cg writes them so, Port32/ABI.md section 4); relocated, the handle of
 * that resource.  What to relocate comes from the classes:
 *
 *  - Class_relocTable: { type, offset in the class's master part (or
 *    the chunk) } up to RELOC_END_OF_LIST; goc makes entries for optr,
 *    link and composite instance variables and @reloc;
 *  - Class_vdRelocTable: { type | vardata tag, offset in its data }, for
 *    objects marked OCF_VARDATA_RELOC;
 *  - a class with a relocation method (CLASSF_HAS_RELOC, "@method
 *    XClass, _reloc") gets MSG_META_RELOCATE/UNRELOCATE instead and
 *    calls ObjRelocOrUnRelocSuper (goc's @callsuper) for the rest.
 *
 * Entries point to the handle (the handle half of an optr).  Pointers
 * (RELOC_RELOC_ENTRY_POINT: classes, routines) are absolute in a loaded
 * resource: the module loader relocated them.  The "frame" of the
 * relocation methods carries the VMRelocType and whether it unrelocates.
 *
 * Object blocks written to a file (VM files: the VMRT_* types other than
 * the resource ones; the state file: ObjRelocateForFile with the
 * resource types, as the asm kernel passes them) are unrelocated
 * completely, as the asm kernel
 * does: handles to KUR_* references (resources of the owner, saved
 * blocks, blocks of the same VM file, others), and pointers -- the class
 * of every object, variant superclasses, RELOC_RELOC_ENTRY_POINT -- to
 * exported entries of the owner, its libraries or the kernel
 * (kernelC.h, ABI.md section 13d).
 */
#include "kernelC.h"
#include <vm.h>
#include <stdint.h>

#include <string.h>

#define FRAME_UNRELOC	0x8000u
#define VORT_RELOC_TYPE	0x0003u		/* VarObjRelocation: type bits */

/* the resource number an object block was built from (OLMBH_resource,
 * goc -cg), 0 if none or not an object block */
static word OwnResource(MemHandle block)
{
    const ObjLMemBlockHeader *h = MemDeref(block);

    if (!h || h->OLMBH_header.LMBH_lmemType != LMEM_TYPE_OBJ_BLOCK)
	return 0;
    return h->OLMBH_resource;
}

/*
 * relocate or unrelocate a handle of an object in `block'.  The resource
 * the block was built from is the block itself: so a duplicate's
 * references within the template lead to the duplicate (glue's
 * ORS_CURRENT_BLOCK).
 */
/* the VM file a block belongs to, 0 if none */
static Handle VMFileOfBlock(MemHandle mh)
{
    Handle o = HandleEntryOf(mh)->HE_owner;
    HandleEntry *he = HandleEntryPeek(o);

    return he && he->HE_type == SIG_VM ? o : 0;
}

/* the saved block of `owner' with VM ID `id', or the ID of block `mh'
 * (objectDup.c's list) */
static Handle SavedBlock(GeodeHandle owner, MemHandle mh, word id,
			 word *idOut)
{
    GeodeCore *c;
    Handle sb, found = 0;

    GeodeLock();
    c = GeodeCoreOf(owner);
    for (sb = c ? c->GC_savedBlocks : 0; sb && !found;
	 sb = HandleEntryOf(sb)->HE_next) {
	HandleEntry *he = HandleEntryOf(sb);
	if (mh ? he->HE_otherInfo == mh : (word) he->HE_size == id) {
	    found = (Handle) he->HE_otherInfo;
	    if (idOut)
		*idOut = (word) he->HE_size;
	}
    }
    GeodeUnlock();
    return found;
}

static Boolean ToFile(int unreloc)
{
    return unreloc == 2;
}

/*
 * relocate or unrelocate a handle in `block'.  unreloc: 0 relocate, 1
 * unrelocate for a resource (only resource references), 2 unrelocate for
 * a file (any block).  The resource the block was built from is the
 * block itself: so a duplicate's references within the template lead to
 * the duplicate (glue's ORS_CURRENT_BLOCK).
 */
static Boolean DoHandle(MemHandle block, const Handle *src, Handle *dst,
			int unreloc)
{
    GeodeHandle owner = MemOwner(block);
    Handle h = *src;
    word own = OwnResource(block);

    if (!unreloc) {
	if (h & GOCR_UNRELOCATED) {
	    dword idx = KUR_INDEX(h);
	    switch (KUR_SOURCE(h)) {
	    case KUR_RESOURCE:
		h = (own && idx == own) ? block
					: GeodeResourceHandle(owner, (word) idx);
		break;
	    case KUR_SAVED:
		h = SavedBlock(owner, 0, (word) idx, NULL);
		break;
	    case KUR_VM:
		h = VMFileOfBlock(block)
		    ? VMVMBlockToMemBlock(VMFileOfBlock(block),
					  (VMBlockHandle) idx) : 0;
		break;
	    case KUR_UNKNOWN:
		h = idx << 4;
		break;
	    default:
		h = 0;
	    }
	    if (!h)
		return TRUE;
	}
    } else if (h == block && own) {
	h = GOCR_UNRELOCATED | own;
    } else if (h && !(h & GOCR_UNRELOCATED)) {
	sdword res = GeodeHandleToResource(owner, h);
	word id;
	if (res >= 0)
	    h = GOCR_UNRELOCATED | (dword) res;
	else if (!ToFile(unreloc))
	    return TRUE;
	else if (SavedBlock(owner, h, 0, &id))
	    h = KUR_MAKE(KUR_SAVED, id);
	else if (VMFileOfBlock(block) && HandleEntryPeek(h) &&
		 HandleEntryOf(h)->HE_owner == VMFileOfBlock(block)) {
	    VMFileHandle f;
	    VMBlockHandle vb = VMMemBlockToVMBlock(h, &f);
	    if (!vb)
		return TRUE;
	    h = KUR_MAKE(KUR_VM, vb);
	} else
	    h = KUR_MAKE(KUR_UNKNOWN, h >> 4);
    }
    *dst = h;
    return FALSE;
}

/* a pointer to an exported entry (a class, a routine) */
static Boolean DoPointer(MemHandle block, void *at, int unreloc)
{
    void *p;
    dword code;

    memcpy(&p, at, sizeof(p));
    if (!p)
	return FALSE;
    if (!unreloc) {
	const void *a = GeodeRelocToEntry(MemOwner(block),
					  (dword) (uintptr_t) p);
	if (!a)
	    return TRUE;
	p = (void *) a;
    } else {
	if (!GeodeEntryToReloc(MemOwner(block), p, &code))
	    return TRUE;
	p = (void *) (uintptr_t) code;
    }
    memcpy(at, &p, sizeof(p));
    return FALSE;
}

/* while this thread relocates an object block for a file (RelocBlock):
 * also what a relocation method does through ObjDoRelocation */
#define fileReloc (ThreadOf(KernelThisThread())->T_fileReloc)

static Boolean DoReloc(ObjRelocationType type, MemHandle block, void *src,
		       void *dst, int unreloc)
{
    switch (type) {
    case RELOC_RELOC_HANDLE: {
	Handle s, d;
	Boolean err;
	memcpy(&s, src, sizeof(s));
	err = DoHandle(block, &s, &d, unreloc && fileReloc ? 2 : unreloc);
	if (!err)
	    memcpy(dst, &d, sizeof(d));
	return err;
    }
    case RELOC_RELOC_ENTRY_POINT:
	if (src != dst)
	    memcpy(dst, src, sizeof(void *));
	/* absolute in loaded resources; through the exports in files */
	return fileReloc ? DoPointer(block, dst, unreloc) : FALSE;
    default:
	KernelFatal(BAD_RELOCATION_TYPE);
	return TRUE;
    }
}

Boolean ObjDoRelocation(ObjRelocationType type, MemHandle block,
			void *sourceData, void *destData)
{
    return DoReloc(type, block, sourceData, destData, 0);
}

Boolean ObjDoUnRelocation(ObjRelocationType type, MemHandle block,
			  void *sourceData, void *destData)
{
    return DoReloc(type, block, sourceData, destData, 1);
}

/* the tables of one class */
static Boolean RelocClassTables(optr o, ClassStruct *cls, int unreloc)
{
    MemHandle mh = OptrToHandle(o);
    byte *p = LMemDeref(o);
    const ObjRelocation *r = cls->Class_relocTable;
    const VarObjRelocation *v = cls->Class_vdRelocTable;
    word base = 0;

    if (r && cls->Class_masterOffset) {
	base = *(word *) (p + cls->Class_masterOffset);
	if (!base)
	    r = NULL;			/* part not built: nothing in it */
    }
    for (; r && r->OR_type != RELOC_END_OF_LIST; r++) {
	byte *at = p + base + r->OR_offset;
	if (DoReloc(r->OR_type, mh, at, at, unreloc))
	    return TRUE;
    }
    if (!v || !(ObjGetFlags(o) & OCF_VARDATA_RELOC))
	return FALSE;
    for (; v->VOR_type; v++) {
	word tag = (word) (v->VOR_type & VDF_TYPE);
	byte *d = ObjVarFindData(o, tag);
	if (!d)
	    continue;
	if (!(VarDataTypePtr(d) == tag && (*((word *) d - 2) & VDF_EXTRA_DATA))
	    || v->VOR_offset >= VarDataSizePtr(d))
	    KernelFatal(ILLEGAL_VARDATA_RELOCATION_OFFSET);
	if (DoReloc((ObjRelocationType) (v->VOR_type & VORT_RELOC_TYPE), mh,
		    d + v->VOR_offset, d + v->VOR_offset, unreloc))
	    return TRUE;
    }
    return FALSE;
}

/* RelocOrUnRelocClass: a class with a relocation method, or its tables */
static Boolean RelocClass(optr o, ClassStruct *cls, word frame)
{
    if ((cls->Class_flags & CLASSF_HAS_RELOC) && cls->Class_relocMethod) {
	byte *p = LMemDeref(o);
	void *pself = p;
	if (cls->Class_masterOffset)
	    pself = p + *(word *) (p + cls->Class_masterOffset);
	return ((ObjRelocMethod *) (void (*)(void)) cls->Class_relocMethod)(
	    pself, o,
	    (frame & FRAME_UNRELOC) ? KMSG_META_UNRELOCATE : KMSG_META_RELOCATE,
	    (word) (frame & ~FRAME_UNRELOC), frame);
    }
    return ObjRelocOrUnRelocSuper(o, cls, frame);
}

/* The tables of `cls', then its superclasses.  TRUE on error. */
Boolean ObjRelocOrUnRelocSuper(optr oself, ClassStruct *cls, word frame)
{
    ClassStruct *super;
    int unreloc = (frame & FRAME_UNRELOC) != 0;
    byte *at = NULL;

    if (RelocClassTables(oself, cls, unreloc))
	return TRUE;
    /* in a file, the superclass of a variant is an entry like the class */
    if (fileReloc && cls->Class_superClass == OBJ_VARIANT_SUPER) {
	byte *p = LMemDeref(oself);
	word part = *(word *) (p + cls->Class_masterOffset);
	at = part ? p + part : NULL;
	if (at && !unreloc &&
	    DoPointer(OptrToHandle(oself), at, 0))
	    return TRUE;
    }
    super = ObjSuperOf(oself, cls);
    if (at && unreloc && DoPointer(OptrToHandle(oself), at, 1))
	return TRUE;
    return super ? RelocClass(oself, super, frame) : FALSE;
}

/* the VMRelocTypes of files: everything is unrelocated */
static int FileFrame(word frame)
{
    word t = (word) (frame & ~FRAME_UNRELOC);

    return t == VMRT_UNRELOCATE_BEFORE_WRITE || t == VMRT_RELOCATE_AFTER_READ
	|| t == VMRT_RELOCATE_AFTER_WRITE;
}

/* the output of the block, then every object; `file': for a file */
static void RelocBlock(MemHandle mh, word frame, KernelFatalError err,
		       int file)
{
    ObjLMemBlockHeader *h = ObjLockObjBlock(mh);
    int unreloc = (frame & FRAME_UNRELOC) != 0;
    Handle out = OptrToHandle(h->OLMBH_output);
    word i, n, table, was = fileReloc;

    fileReloc = (word) file;
    if (DoHandle(mh, &out, &out, unreloc && fileReloc ? 2 : unreloc))
	KernelFatal(err);
    h->OLMBH_output = ConstructOptr(out, OptrToChunk(h->OLMBH_output));
    n = h->OLMBH_header.LMBH_nHandles;
    table = h->OLMBH_header.LMBH_offset;
    for (i = 1; i < n; i++) {
	ChunkHandle ch = (ChunkHandle) (table + 2 * i);
	word e = *(word *) ((byte *) MemDeref(mh) + ch);
	optr o = ConstructOptr(mh, ch);
	ClassStruct *cls;
	if (e == 0 || e == 0xffff || !(ObjGetFlags(o) & OCF_IS_OBJECT))
	    continue;
	/* in a file, the class is an entry: relocated first, unrelocated
	 * last (RelocOrUnRelocAndLoadClass) */
	if (fileReloc && !unreloc && DoPointer(mh, LMemDeref(o), 0))
	    KernelFatal(err);
	cls = ObjClassOf(o);
	if (RelocClass(o, cls, frame))
	    KernelFatal(err);
	if (fileReloc && unreloc && DoPointer(mh, LMemDeref(o), 1))
	    KernelFatal(err);
    }
    fileReloc = was;
    h = MemDeref(mh);
    if (unreloc)
	h->OLMBH_header.LMBH_flags &= ~LMF_RELOCATED;
    else
	h->OLMBH_header.LMBH_flags |= LMF_RELOCATED;
    MemUnlock(mh);
}

void ObjRelocateObjBlock(MemHandle mh, word vmRelocType)
{
    RelocBlock(mh, (word) (vmRelocType & ~FRAME_UNRELOC), CANNOT_RELOCATE,
	       FileFrame(vmRelocType));
}

/* a block of a state file, with the resource types as the asm kernel
 * passes them (objectFile.c) */
void ObjRelocateForFile(MemHandle mh, word vmRelocType, Boolean unreloc)
{
    if (unreloc)
	RelocBlock(mh, (word) (vmRelocType | FRAME_UNRELOC),
		   CANNOT_UNRELOCATE, 1);
    else
	RelocBlock(mh, (word) (vmRelocType & ~FRAME_UNRELOC),
		   CANNOT_RELOCATE, 1);
}

void ObjUnRelocateObjBlock(MemHandle mh, word vmRelocType)
{
    RelocBlock(mh, (word) (vmRelocType | FRAME_UNRELOC), CANNOT_UNRELOCATE,
	       FileFrame(vmRelocType));
}

/*
 * optrs in the chunks of a resource that are not objects (moniker lists,
 * GCN list elements, chunk data): goc -cg gave such a chunk a shadow, the
 * same data with 0 in the handle half of each unrelocated optr
 * (GocChunk, geosport.h).  Where data and shadow differ lies such a
 * handle; a run of differing bytes ends with its top byte (it holds
 * GOCR_UNRELOCATED), so the handle starts 3 bytes before.  The block
 * still holds the data of the resource.
 */
static void RelocChunkPart(MemHandle mh, byte *dst, const byte *data,
			   const byte *shadow, dword size)
{
    dword j;
    Handle h;

    if (!shadow)
	return;
    for (j = 0; j < size; j++) {
	if (data[j] == shadow[j] ||
	    (j + 1 < size && data[j + 1] != shadow[j + 1]))
	    continue;
	if (j < 3)
	    KernelFatal(CANNOT_RELOCATE);
	memcpy(&h, dst + j - 3, sizeof(h));
	if (DoHandle(mh, &h, &h, 0))
	    KernelFatal(CANNOT_RELOCATE);
	memcpy(dst + j - 3, &h, sizeof(h));
    }
}

void ObjRelocateResourceChunks(MemHandle mh, const GocResource *r)
{
    byte *b = MemLock(mh);
    word table = ((LMemBlockHeader *) b)->LMBH_offset;
    word i;

    for (i = 0; i < r->GR_chunks; i++) {
	const GocChunk *c = &r->GR_chunk[i];
	word e = *(word *) (b + table + 2 * i);

	if ((!c->GC_shadow && !c->GC_shadow2) || e == 0 || e == 0xffff)
	    continue;
	RelocChunkPart(mh, b + e, c->GC_data, c->GC_shadow, c->GC_size);
	RelocChunkPart(mh, b + e + c->GC_size, c->GC_data2, c->GC_shadow2,
		       c->GC_size2);
    }
    MemUnlock(mh);
}
