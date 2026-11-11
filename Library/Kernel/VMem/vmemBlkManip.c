/*
 * vmemBlkManip.c -- the block table and the space of a VM file (after
 * vmemBlkManip.asm).
 *
 * The block table has three kinds of entries, as in 16-bit GEOS:
 *
 *  - used blocks (VMBH_sig odd), with their place in the file;
 *  - assigned free entries: free space of the file, in a list sorted by
 *    position (VMH_assignedPtr ... VMH_lastAssigned), neighbours merged;
 *  - unassigned entries: no space, in a list from VMH_unassignedPtr.
 *
 * A block is written in place if it still fits, else to the first free
 * space that is large enough, the free space at the end of the file, or
 * the end of the file.  Its size in the file is rounded up to 16 bytes,
 * as the 16-bit kernel wrote whole paragraphs.
 *
 * Not yet: backup mode (VMBT_DUP/BACKUP/ZOMBIE), compression, compaction.
 */
#include "vmemInt.h"
#include <stdlib.h>
#include "ghal.h"

#include <string.h>

KVMFile *VMFileOf(VMFileHandle vm)
{
    HandleEntry *he = HandleEntryPeek(vm);

    if (!he || he->HE_type != SIG_VM)
	KernelFatal(VM_BAD_FILE_HANDLE);
    return he->HE_addr;
}

Boolean VMIsUsed(const KVMFile *f, VMBlockHandle h)
{
    byte sig;

    if (h <= VM_HEADER_BLOCK || h >= VHGet16(f, VMH_LAST_HANDLE) ||
	(h - VMH_BLOCK_TABLE) % VMBH_SIZE)
	return FALSE;
    sig = f->VF_hdr[h + VMBH_SIG];
    return (sig & VM_IN_USE_BIT) && sig >= VMBT_DUP;
}

void VMCheckBlock(const KVMFile *f, VMBlockHandle h)
{
    if (!VMIsUsed(f, h))
	KernelFatal(VM_BAD_BLOCK_HANDLE);
}

/* ---------------------------------------------------------------------
 * Unassigned entries and the size of the table
 * ------------------------------------------------------------------- */

void VMUnassignBlk(KVMFile *f, VMBlockHandle h)
{
    memset(f->VF_hdr + h, 0, VMBH_SIZE);
    VHSet16(f, h + VMFBH_NEXT, VHGet16(f, VMH_UNASSIGNED_PTR));
    VHSet16(f, VMH_UNASSIGNED_PTR, h);
    VHSet16(f, VMH_NUM_UNASSIGNED,
	    (word) (VHGet16(f, VMH_NUM_UNASSIGNED) + 1));
}

static void *Resize(void *p, size_t size)
{
    return p ? ghal_mem_resize(p, size) : ghal_mem_alloc(size, 0);
}

/* room in VF_hdr, VF_mem and VF_dirty for a table of `size' bytes */
void VMReserveHeader(KVMFile *f, dword size)
{
    word n, old;
    byte *hdr, *dirty;
    MemHandle *mem;

    if (size > 0xfff0)
	KernelFatal(VM_HEADER_FULL);
    if (size <= f->VF_hdrAlloc)
	return;
    old = f->VF_hdrAlloc;
    n = (word) VM_INDEX(size) + 1;
    hdr = Resize(f->VF_hdr, size);
    mem = Resize(f->VF_mem, n * sizeof(MemHandle));
    dirty = Resize(f->VF_dirty, n);
    if (!hdr || !mem || !dirty)
	KernelFatal(VM_ALLOC_FAILED);
    memset(hdr + old, 0, size - old);
    if (old) {
	word o = (word) VM_INDEX(old) + 1;
	memset(mem + o, 0, (n - o) * sizeof(MemHandle));
	memset(dirty + o, 0, n - o);
    } else {
	memset(mem, 0, n * sizeof(MemHandle));
	memset(dirty, 0, n);
    }
    f->VF_hdr = hdr;
    f->VF_mem = mem;
    f->VF_dirty = dirty;
    f->VF_hdrAlloc = (word) size;
}

/* `count' new unassigned entries at the end of the table */
void VMLinkFree(KVMFile *f, word count)
{
    word h = VHGet16(f, VMH_LAST_HANDLE);

    VMReserveHeader(f, (dword) h + (dword) count * VMBH_SIZE);
    while (count--) {
	VMUnassignBlk(f, h);
	h += VMBH_SIZE;
    }
    VHSet16(f, VMH_LAST_HANDLE, h);
}

VMBlockHandle VMGetUnassignedBlk(KVMFile *f)
{
    VMBlockHandle h = VHGet16(f, VMH_UNASSIGNED_PTR);

    if (!h) {
	VMLinkFree(f, VM_EXTEND_NUM_BLKS);
	h = VHGet16(f, VMH_UNASSIGNED_PTR);
    }
    VHSet16(f, VMH_UNASSIGNED_PTR, VHGet16(f, h + VMFBH_NEXT));
    VHSet16(f, h + VMFBH_NEXT, 0);
    VHSet16(f, VMH_NUM_UNASSIGNED,
	    (word) (VHGet16(f, VMH_NUM_UNASSIGNED) - 1));
    return h;
}

void VMMarkBlkUsed(KVMFile *f, VMBlockHandle h)
{
    memset(f->VF_hdr + h, 0, VMBH_SIZE);
    VHSet8(f, h + VMBH_SIG, VMBT_USED);
    VHSet16(f, VMH_NUM_USED, (word) (VHGet16(f, VMH_NUM_USED) + 1));
}

/* ---------------------------------------------------------------------
 * Free space: the assigned list
 * ------------------------------------------------------------------- */

#define FPOS(h)		VHGet32(f, (word) ((h) + VMFBH_FILE_POS))
#define FSIZE(h)	VHGet32(f, (word) ((h) + VMFBH_FILE_SIZE))
#define FNEXT(h)	VHGet16(f, (word) ((h) + VMFBH_NEXT))
#define FPREV(h)	VHGet16(f, (word) ((h) + VMFBH_PREV))

static void Detach(KVMFile *f, word h)
{
    word next = FNEXT(h), prev = FPREV(h);

    if (prev)
	VHSet16(f, prev + VMFBH_NEXT, next);
    else
	VHSet16(f, VMH_ASSIGNED_PTR, next);
    if (next)
	VHSet16(f, next + VMFBH_PREV, prev);
    else
	VHSet16(f, VMH_LAST_ASSIGNED, prev);
    VHSet16(f, VMH_NUM_ASSIGNED, (word) (VHGet16(f, VMH_NUM_ASSIGNED) - 1));
}

static void Insert(KVMFile *f, word h)
{
    dword pos = FPOS(h);
    word prev = 0, cur = VHGet16(f, VMH_ASSIGNED_PTR);

    while (cur && FPOS(cur) < pos) {
	prev = cur;
	cur = FNEXT(cur);
    }
    VHSet16(f, h + VMFBH_NEXT, cur);
    VHSet16(f, h + VMFBH_PREV, prev);
    if (prev)
	VHSet16(f, prev + VMFBH_NEXT, h);
    else
	VHSet16(f, VMH_ASSIGNED_PTR, h);
    if (cur)
	VHSet16(f, cur + VMFBH_PREV, h);
    else
	VHSet16(f, VMH_LAST_ASSIGNED, h);
    VHSet16(f, VMH_NUM_ASSIGNED, (word) (VHGet16(f, VMH_NUM_ASSIGNED) + 1));
}

/* free space [pos, pos + size) (VMFreeBlkHandle, VMCoalesceIfPossible) */
void VMReleaseSpace(KVMFile *f, dword pos, dword size)
{
    word h, n, p;

    if (!size)
	return;
    h = VMGetUnassignedBlk(f);
    memset(f->VF_hdr + h, 0, VMBH_SIZE);
    VHSet32(f, h + VMFBH_FILE_SIZE, size);
    VHSet32(f, h + VMFBH_FILE_POS, pos);
    Insert(f, h);
    p = FPREV(h);
    if (p && FPOS(p) + FSIZE(p) == FPOS(h)) {
	VHSet32(f, p + VMFBH_FILE_SIZE, FSIZE(p) + FSIZE(h));
	Detach(f, h);
	VMUnassignBlk(f, h);
	h = p;
    }
    n = FNEXT(h);
    if (n && FPOS(h) + FSIZE(h) == FPOS(n)) {
	VHSet32(f, h + VMFBH_FILE_SIZE, FSIZE(h) + FSIZE(n));
	Detach(f, n);
	VMUnassignBlk(f, n);
    }
}

/* a place for `size' bytes (VMUseBlk) */
dword VMUseSpace(KVMFile *f, word size)
{
    word h, last;
    dword pos, eof;

    for (h = VHGet16(f, VMH_ASSIGNED_PTR); h; h = FNEXT(h)) {
	dword s = FSIZE(h);
	if (s < size)
	    continue;
	pos = FPOS(h);
	if (s == size) {
	    Detach(f, h);
	    VMUnassignBlk(f, h);
	} else {
	    VHSet32(f, h + VMFBH_FILE_POS, pos + size);
	    VHSet32(f, h + VMFBH_FILE_SIZE, s - size);
	}
	return pos;
    }
    eof = FileSize(f->VF_file);
    if (eof < VMFH_SIZE)
	eof = VMFH_SIZE;
    /* free space at the end grows into the end of the file */
    last = VHGet16(f, VMH_LAST_ASSIGNED);
    if (last && FPOS(last) + FSIZE(last) >= eof) {
	pos = FPOS(last);
	Detach(f, last);
	VMUnassignBlk(f, last);
	return pos;
    }
    return eof;
}

/* ---------------------------------------------------------------------
 * Reading and writing blocks
 * ------------------------------------------------------------------- */

/*
 * CallVMReloc: an object block of a file with VMA_OBJECT_RELOC is
 * relocated by the kernel (VMObjRelocOrUnReloc), any other block by the
 * routine of VMSetReloc.  The block is not locked.
 */
static void CallReloc(KVMFile *f, VMBlockHandle h, MemHandle mh,
		      VMRelocType type)
{
    byte attrs = f->VF_hdr[VMH_ATTRIBUTES];
    byte *p = MemLock(mh);
    ObjLMemBlockHeader *o = (ObjLMemBlockHeader *) p;

    if ((MemGetInfo(mh, MGIT_FLAGS_AND_LOCK_COUNT) & HF_LMEM) &&
	(attrs & VMA_OBJECT_RELOC) &&
	o->OLMBH_header.LMBH_lmemType == LMEM_TYPE_OBJ_BLOCK) {
	if (type == VMRT_RELOCATE_AFTER_READ) {
	    o->OLMBH_inUseCount = 0;
	    o->OLMBH_interactibleCount = 0;
	}
	MemUnlock(mh);
	/* VMA_COMPACT_OBJ_BLOCK: Port32 LMem blocks have no free space
	 * to drop in the file (LMemToDOS16 leaves it out) */
	if (type == VMRT_UNRELOCATE_BEFORE_WRITE)
	    ObjUnRelocateObjBlock(mh, type);
	else
	    ObjRelocateObjBlock(mh, type);
	return;
    }
    if (f->VF_reloc)
	f->VF_reloc(f->VF_handle, h, mh, p, type);
    MemUnlock(mh);
}

word VMReadBlock(KVMFile *f, VMBlockHandle h)
{
    word size = VHGet16(f, h + VMBH_FILE_SIZE);
    Boolean lmem = (f->VF_hdr[h + VMBH_FLAGS] & VMBF_LMEM) != 0;
    MemHandle mh;
    byte *p;
    word e = 0;

    if (!size)
	KernelFatal(VM_BLOCK_HAS_NO_DATA);
    mh = MemAllocSetOwner(f->VF_handle, size, HF_SWAPABLE, 0);
    if (!mh)
	return ERROR_INSUFFICIENT_MEMORY;
    p = MemLock(mh);
    FilePos(f->VF_file, VHGet32(f, h + VMBH_FILE_POS), FILE_POS_START);
    if (FileRead(f->VF_file, p, size, FALSE) != size)
	e = ThreadGetError() ? ThreadGetError() : ERROR_SHORT_READ_WRITE;
    if (!e && lmem) {
	/* the DOS16 layout of the file to the Port32 one */
	MemHandle lm = LMemFromDOS16(p, size, f->VF_handle);
	if (!lm)
	    e = ERROR_FILE_FORMAT_MISMATCH;
	MemUnlock(mh);
	MemFree(mh);
	mh = lm;
	if (mh) {
	    LMemBlockHeader *lh = MemLock(mh);
	    lh->LMBH_flags |= LMF_IS_VM;
	    if (lh->LMBH_lmemType == LMEM_TYPE_OBJ_BLOCK && f->VF_execThread)
		MemModifyOtherInfo(mh, f->VF_execThread);
	    MemUnlock(mh);
	}
    } else
	MemUnlock(mh);
    if (e) {
	if (mh)
	    MemFree(mh);
	return e;
    }
    f->VF_mem[VM_INDEX(h)] = mh;
    f->VF_dirty[VM_INDEX(h)] = FALSE;
    CallReloc(f, h, mh, VMRT_RELOCATE_AFTER_READ);
    return 0;
}

word VMWriteData(KVMFile *f, VMBlockHandle h, const void *data, word size)
{
    static const byte zero[16];
    dword newSize = ((dword) size + 15) & ~(dword) 15;
    word old = VHGet16(f, h + VMBH_FILE_SIZE);
    dword oldPos = VHGet32(f, h + VMBH_FILE_POS), pos;

    if (newSize > 0xfff0)
	return ERROR_ARGS_INVALID;
    if (old && newSize <= old) {
	pos = oldPos;
	VMReleaseSpace(f, oldPos + newSize, old - newSize);
    } else {
	if (old)
	    VMReleaseSpace(f, oldPos, old);
	pos = VMUseSpace(f, (word) newSize);
    }
    VHSet16(f, h + VMBH_FILE_SIZE, (word) newSize);
    VHSet32(f, h + VMBH_FILE_POS, pos);
    VHSet32(f, VMH_USED_SIZE, VHGet32(f, VMH_USED_SIZE) + newSize - old);
    FilePos(f->VF_file, pos, FILE_POS_START);
    if (FileWrite(f->VF_file, data, size, FALSE) != size ||
	(newSize > size &&
	 FileWrite(f->VF_file, zero, (word) (newSize - size), FALSE) !=
	 newSize - size))
	return VM_UPDATE_INSUFFICIENT_DISK_SPACE;
    return 0;
}

word VMWriteBlock(KVMFile *f, VMBlockHandle h)
{
    MemHandle mh = f->VF_mem[VM_INDEX(h)];
    byte flags = f->VF_hdr[h + VMBH_FLAGS] & (byte) ~VMBF_LMEM;
    byte *p, *img = NULL;
    word e, size;

    if (!mh)
	return 0;
    CallReloc(f, h, mh, VMRT_UNRELOCATE_BEFORE_WRITE);
    p = MemLock(mh);
    size = (word) MemGetInfo(mh, MGIT_SIZE);
    /* an LMem block goes to the file in the DOS16 layout (lmemCode.c) */
    if ((MemGetInfo(mh, MGIT_FLAGS_AND_LOCK_COUNT) & HF_LMEM) &&
	!(((LMemBlockHeader *) p)->LMBH_flags & LMF_NO_HANDLES)) {
	size = LMemDOS16Size(mh);
	img = size ? malloc(size) : NULL;
	if (!img) {
	    MemUnlock(mh);
	    CallReloc(f, h, mh, VMRT_RELOCATE_AFTER_WRITE);
	    return ERROR_INSUFFICIENT_MEMORY;
	}
	size = LMemToDOS16(mh, img);
	flags |= VMBF_LMEM;
    }
    VHSet8(f, h + VMBH_FLAGS, flags);
    e = VMWriteData(f, h, img ? img : p, size);
    free(img);
    MemUnlock(mh);
    CallReloc(f, h, mh, VMRT_RELOCATE_AFTER_WRITE);
    if (!e)
	f->VF_dirty[VM_INDEX(h)] = FALSE;
    return e;
}

void VMFreeBlockLow(KVMFile *f, VMBlockHandle h)
{
    word i = (word) VM_INDEX(h);
    word size = VHGet16(f, h + VMBH_FILE_SIZE);

    if (f->VF_mem[i]) {
	MemFree(f->VF_mem[i]);
	f->VF_mem[i] = 0;
    }
    f->VF_dirty[i] = FALSE;
    if (size) {
	VMReleaseSpace(f, VHGet32(f, h + VMBH_FILE_POS), size);
	VHSet32(f, VMH_USED_SIZE, VHGet32(f, VMH_USED_SIZE) - size);
    }
    VHSet16(f, VMH_NUM_USED, (word) (VHGet16(f, VMH_NUM_USED) - 1));
    VMUnassignBlk(f, h);
}
