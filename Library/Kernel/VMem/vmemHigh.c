/*
 * vmemHigh.c -- the VM file routines (after vmemHigh.asm and the C stubs
 * of vmemC.asm).
 *
 * A VMFileHandle is a SIG_VM handle owned by the process that opened the
 * file; it points to the KVMFile and owns the memory of the blocks, so
 * MemOwner of a block is the file's owner.  Opening a file that is open
 * already gives the same handle again (VMClose counts down).  A block
 * is read when it is first locked and stays in memory until the file is
 * closed; VMUpdate and VMClose write what is dirty and then the header.
 *
 * LMem blocks go to the file in the DOS16 LMem layout and object blocks
 * unrelocated (vmemBlkManip.c, Port32/ABI.md sections 13b and 13d).
 *
 * Backup mode (vmemBlkManip.c): VMSave makes the blocks written since
 * the last save the file, VMRevert goes back to it, VMSaveAs writes the
 * current version to a new file and reverts the old one, which it
 * closes.  VMDiscardDirtyBlocks drops what was not written.
 *
 * The update counter (VMFH_updateCounter) goes up with each update, as
 * DOS16 did, and VMCheckForModifications compares it with the one the
 * file had when this handle last read or wrote it.  VMGrabExclusive is
 * a lock of the file's handle with a timeout; the file is not shared
 * with other GEOS systems (VMA_SHARED_MULTIPLE files are not reread
 * when another wrote them).
 *
 * Not yet: discarding clean blocks, saving to state, compression,
 * VMSetDirtyLimit and VMEnforceHandleLimits (kept, not enforced).
 */
#include "vmemInt.h"
#include "ghal.h"

#include <stdio.h>
#include <string.h>

static KVMFile *vmFiles;

/* ---------------------------------------------------------------------
 * Opening and closing
 * ------------------------------------------------------------------- */

static void FreeVM(KVMFile *f)
{
    KVMFile **p;
    word i, n = f->VF_hdr ? (word) VM_INDEX(VHGet16(f, VMH_LAST_HANDLE)) : 0;

    for (i = 0; i < n; i++)
	if (f->VF_mem[i])
	    MemFree(f->VF_mem[i]);
    for (p = &vmFiles; *p; p = &(*p)->VF_next)
	if (*p == f) {
	    *p = f->VF_next;
	    break;
	}
    if (f->VF_file)
	FileClose(f->VF_file, FALSE);
    if (f->VF_handle)
	HandleFree(f->VF_handle);
    ghal_mem_free(f->VF_hdr);
    ghal_mem_free(f->VF_mem);
    ghal_mem_free(f->VF_dirty);
    ghal_mem_free(f);
}

/* a name in directory `dir' that does not exist yet */
/* a new file VMnnnn.TMP in the directory dir (VMO_TEMP_FILE); its path
 * in name.  The caller's buffer gets it too (VMOpen), so it must have
 * room for the 12 characters and a backslash after the directory. */
static FileHandle CreateTemp(const char *dir, char *name, size_t size)
{
    static word counter;
    word tries;
    FileHandle fh;

    for (tries = 0; tries < 0x1000; tries++) {
	snprintf(name, size, "%s%sVM%04X.TMP", dir,
		 *dir && dir[strlen(dir) - 1] != '\\' ? "\\" : "",
		 (unsigned) (++counter & 0xffff));
	fh = FileCreate(name, FILE_CREATE_ONLY | FILE_ACCESS_RW, 0);
	if (fh || ThreadGetError() != ERROR_FILE_EXISTS)
	    return fh;
    }
    return 0;
}

/* a word of the VMFileHeader (VMFileReadWord, VMFileWriteWord) */
static word ReadWord(KVMFile *f, word off)
{
    byte b[2] = { 0, 0 };

    FilePos(f->VF_file, off, FILE_POS_START);
    (void) FileRead(f->VF_file, b, 2, FALSE);
    return (word) (b[0] | (b[1] << 8));
}

static void WriteWord(KVMFile *f, word off, word v)
{
    byte b[2];

    b[0] = (byte) v;
    b[1] = (byte) (v >> 8);
    FilePos(f->VF_file, off, FILE_POS_START);
    (void) FileWrite(f->VF_file, b, 2, FALSE);
}

static word OpenStatus(word e)
{
    return e == ERROR_FILE_NOT_FOUND ? VM_FILE_NOT_FOUND
	 : e == ERROR_FILE_EXISTS ? VM_FILE_EXISTS : e;
}

VMFileHandle VMOpen(char *name, VMAccessFlags flags, VMOpenType mode,
		    word compression)
{
    char dp[FILE_DISK_PATH_SIZE], tmp[HOST_PATH_SIZE];
    FSDDisk *disk;
    dword id = 0;
    FileHandle fh = 0;
    Boolean create = FALSE, readOnly = FALSE, exists;
    FileCreateFlags native = (mode & VMO_NATIVE_WITH_EXT_ATTRS)
			     ? FCF_NATIVE_WITH_EXT_ATTRS : 0;
    KVMFile *f;
    word e;

    (void) compression;
    mode &= (VMOpenType) ~VMO_NATIVE_WITH_EXT_ATTRS;
    if (mode == VMO_TEMP_FILE) {
	fh = CreateTemp(name, tmp, sizeof(tmp));
	if (!fh) {
	    KernelSetError(OpenStatus(ThreadGetError()));
	    return 0;
	}
	/* as DOS16: the name of the file into the caller's buffer (the
	 * directory it passed, with the name appended) */
	strcpy(name, tmp);
	name = tmp;
	create = TRUE;
    }
    disk = FileLocate(0, name, dp, sizeof(dp));
    if (!disk) {
	if (fh)
	    FileClose(fh, FALSE);
	return 0;
    }
    /* open already (the same file: disk and FEA_FILE_ID): the same
     * handle */
    exists = !FSD_OF(disk)->FSD_pathExtAttrs(disk, dp, FEA_FILE_ID, &id,
					     sizeof(id), FALSE);
    for (f = vmFiles; exists && !fh && f; f = f->VF_next)
	if (f->VF_disk == disk && f->VF_id == id) {
	    if (mode == VMO_CREATE_ONLY) {
		KernelSetError(VM_FILE_EXISTS);
		return 0;
	    }
	    f->VF_refs++;
	    KernelSetError(f->VF_readOnly ? VM_OPEN_OK_READ_ONLY
					  : VM_OPEN_OK_READ_WRITE_NOT_SHARED);
	    return f->VF_handle;
	}

    if (!fh) {
	switch (mode) {
	case VMO_OPEN:
	    if (!exists) {
		KernelSetError(VM_FILE_NOT_FOUND);
		return 0;
	    }
	    break;
	case VMO_CREATE:
	    create = !exists;
	    break;
	case VMO_CREATE_ONLY:
	    if (exists) {
		KernelSetError(VM_FILE_EXISTS);
		return 0;
	    }
	    create = TRUE;
	    break;
	case VMO_CREATE_TRUNCATE:
	    create = TRUE;
	    break;
	default:
	    KernelSetError(ERROR_ARGS_INVALID);
	    return 0;
	}
	if (create) {
	    fh = FileCreate(name, (mode == VMO_CREATE_ONLY ? FILE_CREATE_ONLY
							  : FILE_CREATE_TRUNCATE)
			    | native | FILE_ACCESS_RW, 0);
	} else {
	    if (!(flags & VMAF_FORCE_READ_ONLY))
		fh = FileOpen(name, FILE_ACCESS_RW | FILE_DENY_W);
	    if (!fh && !(flags & VMAF_FORCE_READ_WRITE) &&
		((flags & VMAF_FORCE_READ_ONLY) ||
		 ThreadGetError() == ERROR_ACCESS_DENIED)) {
		fh = FileOpen(name, FILE_ACCESS_R | FILE_DENY_W);
		readOnly = TRUE;
	    }
	}
	if (!fh) {
	    KernelSetError(create && ThreadGetError() != ERROR_FILE_EXISTS
			   ? VM_CANNOT_CREATE : OpenStatus(ThreadGetError()));
	    return 0;
	}
    }

    f = ghal_mem_alloc(sizeof(KVMFile), GHAL_MEM_ZERO);
    if (!f) {
	FileClose(fh, FALSE);
	KernelSetError(ERROR_INSUFFICIENT_MEMORY);
	return 0;
    }
    f->VF_file = fh;
    f->VF_refs = 1;
    f->VF_readOnly = readOnly;
    {
	KFile *kf = FileOf(fh);
	f->VF_disk = kf->KF_file.FF_disk;
	FSD_OF(f->VF_disk)->FSD_handleExtAttrs(&kf->KF_file, FEA_FILE_ID,
					       &f->VF_id, sizeof(f->VF_id),
					       FALSE);
    }
    f->VF_handle = HandleAlloc(SIG_VM, GeodeGetProcessHandle());
    if (!f->VF_handle) {
	FreeVM(f);
	KernelSetError(ERROR_TOO_MANY_OPEN_FILES);
	return 0;
    }
    HandleEntryOf(f->VF_handle)->HE_addr = f;
    e = create ? VMCreateHeader(f) : VMReadHeader(f);
    if (e) {
	FreeVM(f);
	KernelSetError(e);
	return 0;
    }
    f->VF_updateCount = ReadWord(f, VMFH_UPDATE_COUNTER);
    f->VF_next = vmFiles;
    vmFiles = f;
    /* make sure that any DB stuff is also sane */
    if (!create && !DBCheckDBFile(f->VF_handle)) {
	VMClose(f->VF_handle, TRUE);
	KernelSetError(VM_OPEN_INVALID_VM_FILE);
	return 0;
    }
    KernelSetError(create ? VM_CREATE_OK : readOnly ? VM_OPEN_OK_READ_ONLY
				      : VM_OPEN_OK_READ_WRITE_NOT_SHARED);
    return f->VF_handle;
}

word VMHighUpdate(KVMFile *f)
{
    Boolean any = FALSE;
    word h, e;

    for (h = VM_HEADER_BLOCK + VMBH_SIZE; h < VHGet16(f, VMH_LAST_HANDLE);
	 h += VMBH_SIZE)
	if (f->VF_dirty[VM_INDEX(h)]) {
	    if (f->VF_readOnly)
		return ERROR_ACCESS_DENIED;
	    e = VMWriteBlock(f, h);
	    if (e)
		return e;
	    any = TRUE;
	}
    if (any || f->VF_hdrDirty) {
	if (f->VF_readOnly)
	    return ERROR_ACCESS_DENIED;
	e = VMWriteHeader(f);
	/* VMUpdateLow: the change counter, even after an error */
	f->VF_updateCount++;
	WriteWord(f, VMFH_UPDATE_COUNTER, f->VF_updateCount);
	WriteWord(f, VMFH_UPDATE_TYPE, VMO_READ);
	if (e)
	    return e;
	return 0;
    }
    return VM_UPDATE_NOTHING_DIRTY;
}

word VMUpdate(VMFileHandle file)
{
    word e = VMHighUpdate(VMFileOf(file));

    KernelSetError(e == VM_UPDATE_NOTHING_DIRTY ? 0 : e);
    return e;
}

word VMClose(VMFileHandle file, Boolean noErrorFlag)
{
    KVMFile *f = VMFileOf(file);
    word e = 0;

    if (--f->VF_refs)
	return 0;
    if (!f->VF_readOnly) {
	e = VMHighUpdate(f);
	if (e == VM_UPDATE_NOTHING_DIRTY)
	    e = 0;
    }
    if (f->VF_exclSem)
	ThreadFreeSem(f->VF_exclSem);
    FreeVM(f);
    if (e && noErrorFlag)
	KernelFatal(FILE_ERROR_NOT_EXPECTED);
    KernelSetError(e);
    return e;
}

void VMFreeHandle(Handle vm)
{
    KVMFile *f = VMFileOf(vm);

    f->VF_refs = 1;
    VMClose(vm, FALSE);
}

/* ---------------------------------------------------------------------
 * Blocks
 * ------------------------------------------------------------------- */

static VMBlockHandle BlockOfMem(KVMFile *f, MemHandle mh)
{
    word h;

    for (h = VM_HEADER_BLOCK + VMBH_SIZE; h < VHGet16(f, VMH_LAST_HANDLE);
	 h += VMBH_SIZE)
	if (f->VF_mem[VM_INDEX(h)] == mh)
	    return h;
    return 0;
}

/* the VM file of a block's memory, NULL if it is none */
static KVMFile *FileOfMem(MemHandle mh)
{
    HandleEntry *he = HandleEntryPeek(HandleEntryOf(mh)->HE_owner);

    return he && he->HE_type == SIG_VM ? he->HE_addr : NULL;
}

/* a preserved handle (VMDiscardMem) gets the data again */
static word Reload(KVMFile *f, VMBlockHandle block, MemHandle mh)
{
    word i = (word) VM_INDEX(block), e, size;
    MemHandle nm;

    f->VF_mem[i] = 0;
    e = VMReadBlock(f, block);
    nm = f->VF_mem[i];
    f->VF_mem[i] = mh;
    if (e)
	return e;
    size = (word) MemGetInfo(nm, MGIT_SIZE);
    MemReAlloc(mh, size, HAF_NO_ERR);
    memcpy(MemLock(mh), MemLock(nm), size);
    if (MemGetInfo(nm, MGIT_FLAGS_AND_LOCK_COUNT) & HF_LMEM) {
	MemModifyFlags(mh, HF_LMEM, 0);
	((LMemBlockHeader *) MemDeref(mh))->LMBH_handle = mh;
    }
    MemModifyOtherInfo(mh, MemGetInfo(nm, MGIT_OTHER_INFO));
    MemUnlock(nm);
    MemUnlock(mh);
    MemFree(nm);
    return 0;
}

static MemHandle InMemory(KVMFile *f, VMBlockHandle block)
{
    MemHandle mh;
    word e = 0;

    VMCheckBlock(f, block);
    mh = f->VF_mem[VM_INDEX(block)];
    if (!mh)
	e = VMReadBlock(f, block);
    else if (MemGetInfo(mh, MGIT_FLAGS_AND_LOCK_COUNT) & HF_DISCARDED)
	e = Reload(f, block, mh);
    if (e) {
	KernelSetError(e);
	return 0;
    }
    return f->VF_mem[VM_INDEX(block)];
}

void *VMLock(VMFileHandle file, VMBlockHandle block, MemHandle *mh)
{
    MemHandle m = InMemory(VMFileOf(file), block);

    if (mh)
	*mh = m;
    return m ? MemLock(m) : NULL;
}

void VMUnlock(MemHandle mh)
{
    MemUnlock(mh);
}

void VMDirty(MemHandle mh)
{
    KVMFile *f = FileOfMem(mh);
    VMBlockHandle h = f ? BlockOfMem(f, mh) : 0;

    if (!h)
	KernelFatal(VM_NOT_A_VM_BLOCK);
    f->VF_dirty[VM_INDEX(h)] = TRUE;
}

VMBlockHandle VMAlloc(VMFileHandle file, word size, word userId)
{
    KVMFile *f = VMFileOf(file);
    VMBlockHandle h = VMGetUnassignedBlk(f);

    VMMarkBlkUsed(f, h);
    VHSet16(f, h + VMBH_UID, userId);
    if (size) {
	MemHandle mh = MemAllocSetOwner(file, size, HF_SWAPABLE,
					HAF_ZERO_INIT);
	if (!mh)
	    KernelFatal(VM_ALLOC_FAILED);
	f->VF_mem[VM_INDEX(h)] = mh;
	f->VF_dirty[VM_INDEX(h)] = TRUE;
    }
    return h;
}

/* in backup mode a block that has a version in the file stays as a
 * ZOMBIE until VMSave or VMRevert */
void VMFree(VMFileHandle file, VMBlockHandle block)
{
    KVMFile *f = VMFileOf(file);
    byte sig;

    VMCheckBlock(f, block);
    sig = f->VF_hdr[block + VMBH_SIG];
    if (!(f->VF_hdr[VMH_ATTRIBUTES] & VMA_BACKUP) ||
	(sig == VMBT_DUP &&
	 !(f->VF_hdr[block + VMBH_FLAGS] & VMBF_HAS_BACKUP) &&
	 !VHGet16(f, block + VMBH_FILE_SIZE))) {
	VMFreeBlockLow(f, block);
	return;
    }
    {
	word i = (word) VM_INDEX(block);
	if (f->VF_mem[i]) {
	    MemFree(f->VF_mem[i]);
	    f->VF_mem[i] = 0;
	}
	f->VF_dirty[i] = FALSE;
    }
    /* a USED block: the backup takes its space */
    if (sig == VMBT_USED && VHGet16(f, block + VMBH_FILE_SIZE))
	VMBackupBlock(f, block);
    VHSet8(f, block + VMBH_SIG, VMBT_ZOMBIE);
    VHSet16(f, VMH_NUM_USED, (word) (VHGet16(f, VMH_NUM_USED) - 1));
}

VMBlockHandle VMFind(VMFileHandle file, VMBlockHandle startBlock,
		     word userId)
{
    KVMFile *f = VMFileOf(file);
    word h = startBlock ? startBlock + VMBH_SIZE
			: VM_HEADER_BLOCK + VMBH_SIZE;

    for (; h < VHGet16(f, VMH_LAST_HANDLE); h += VMBH_SIZE)
	if (VMIsUsed(f, h) && VHGet16(f, h + VMBH_UID) == userId)
	    return h;
    return 0;
}

void VMModifyUserID(VMFileHandle file, VMBlockHandle block, word userId)
{
    KVMFile *f = VMFileOf(file);

    VMCheckBlock(f, block);
    VHSet16(f, block + VMBH_UID, userId);
}

Boolean VMInfo(VMFileHandle file, VMBlockHandle block, VMInfoStruct *info)
{
    KVMFile *f = VMFileOf(file);
    MemHandle mh;

    if (!VMIsUsed(f, block))
	return FALSE;
    mh = f->VF_mem[VM_INDEX(block)];
    if (info) {
	info->mh = mh;
	info->size = mh ? (word) MemGetInfo(mh, MGIT_SIZE)
			: VHGet16(f, block + VMBH_FILE_SIZE);
	info->userId = VHGet16(f, block + VMBH_UID);
    }
    return TRUE;
}

word VMGetDirtyState(VMFileHandle file)
{
    KVMFile *f = VMFileOf(file);
    word h;

    if (f->VF_hdrDirty)
	return 0x0101;
    for (h = VM_HEADER_BLOCK + VMBH_SIZE; h < VHGet16(f, VMH_LAST_HANDLE);
	 h += VMBH_SIZE)
	if (f->VF_dirty[VM_INDEX(h)])
	    return 0x0101;
    return 0;
}

VMBlockHandle VMGetMapBlock(VMFileHandle file)
{
    return VHGet16(VMFileOf(file), VMH_MAP_BLOCK);
}

void VMSetMapBlock(VMFileHandle file, VMBlockHandle block)
{
    KVMFile *f = VMFileOf(file);

    if (block)
	VMCheckBlock(f, block);
    VHSet16(f, VMH_MAP_BLOCK, block);
}

VMBlockHandle VMGetDBMap(VMFileHandle file)
{
    return VHGet16(VMFileOf(file), VMH_DB_MAP_BLOCK);
}

void VMSetDBMap(VMFileHandle file, VMBlockHandle block)
{
    VHSet16(VMFileOf(file), VMH_DB_MAP_BLOCK, block);
}

word VMGetAttributes(VMFileHandle file)
{
    return VMFileOf(file)->VF_hdr[VMH_ATTRIBUTES];
}

VMAttributes VMSetAttributes(VMFileHandle file, VMAttributes attrToSet,
			     VMAttributes attrToClear)
{
    KVMFile *f = VMFileOf(file);
    byte a = (byte) ((f->VF_hdr[VMH_ATTRIBUTES] & ~attrToClear) | attrToSet);

    VHSet8(f, VMH_ATTRIBUTES, a);
    return a;
}

VMBlockHandle VMMemBlockToVMBlock(MemHandle mh, VMFileHandle *file)
{
    KVMFile *f = FileOfMem(mh);
    VMBlockHandle h = f ? BlockOfMem(f, mh) : 0;

    if (!h)
	KernelFatal(VM_NOT_A_VM_BLOCK);
    if (file)
	*file = f->VF_handle;
    return h;
}

MemHandle VMVMBlockToMemBlock(VMFileHandle file, VMBlockHandle block)
{
    return InMemory(VMFileOf(file), block);
}

VMBlockHandle VMAttach(VMFileHandle file, VMBlockHandle block, MemHandle mh)
{
    KVMFile *f = VMFileOf(file);
    word i;

    if (!block) {
	block = VMGetUnassignedBlk(f);
	VMMarkBlkUsed(f, block);
    } else
	VMCheckBlock(f, block);
    i = (word) VM_INDEX(block);
    if (f->VF_mem[i] && f->VF_mem[i] != mh)
	MemFree(f->VF_mem[i]);
    f->VF_mem[i] = mh;
    f->VF_dirty[i] = TRUE;
    HandleModifyOwner(mh, file);
    if (MemGetInfo(mh, MGIT_FLAGS_AND_LOCK_COUNT) & HF_LMEM) {
	((LMemBlockHeader *) MemLock(mh))->LMBH_flags |= LMF_IS_VM;
	MemUnlock(mh);
    }
    return block;
}

MemHandle VMDetach(VMFileHandle file, VMBlockHandle block, GeodeHandle owner)
{
    KVMFile *f = VMFileOf(file);
    word i = (word) VM_INDEX(block);
    MemHandle mh;

    VMCheckBlock(f, block);
    /* the file keeps the contents */
    if (f->VF_mem[i] && f->VF_dirty[i] && !f->VF_readOnly)
	VMWriteBlock(f, block);
    mh = InMemory(f, block);
    if (!mh)
	return 0;
    f->VF_mem[i] = 0;
    f->VF_dirty[i] = FALSE;
    HandleModifyOwner(mh, owner ? owner : GeodeGetProcessHandle());
    if (MemGetInfo(mh, MGIT_FLAGS_AND_LOCK_COUNT) & HF_LMEM) {
	((LMemBlockHeader *) MemLock(mh))->LMBH_flags &= ~LMF_IS_VM;
	MemUnlock(mh);
    }
    return mh;
}

void VMSetReloc(VMFileHandle file,
		void (*reloc)(VMFileHandle file, VMBlockHandle block,
			      MemHandle mh, void *data, VMRelocType type))
{
    VMFileOf(file)->VF_reloc =
	(void (*)(VMFileHandle, VMBlockHandle, MemHandle, void *, word))
	(void (*)(void)) reloc;
}

void VMSetExecThread(VMFileHandle file, ThreadHandle thread)
{
    VMFileOf(file)->VF_execThread = thread;
}

void VMPreserveBlocksHandle(VMFileHandle file, VMBlockHandle block)
{
    KVMFile *f = VMFileOf(file);

    VMCheckBlock(f, block);
    VHSet8(f, block + VMBH_FLAGS,
	   (byte) (f->VF_hdr[block + VMBH_FLAGS] | VMBF_PRESERVE_HANDLE));
}

/* ---------------------------------------------------------------------
 * Backup mode: save, revert, save as (VMSaveRevertCommon, VMSaveAs)
 * ------------------------------------------------------------------- */

void VMHighSetSig(KVMFile *f, VMBlockHandle h, byte sig)
{
    VHSet8(f, h + VMBH_SIG, sig);
    VHSet8(f, h + VMBH_FLAGS,
	   (byte) (f->VF_hdr[h + VMBH_FLAGS] & ~VMBF_HAS_BACKUP));
}

/* VMSave: the version of the file since the last save becomes the one
 * to go back to; FALSE if it was written (else the error is set) */
Boolean VMSave(VMFileHandle file)
{
    KVMFile *f = VMFileOf(file);
    word e = f->VF_readOnly ? ERROR_ACCESS_DENIED : VMSaveRevertCommon(f, TRUE);

    KernelSetError(e);
    return (Boolean) (e != 0);
}

/* VMRevert: back to the last save; what was not written since is
 * dropped too */
void VMRevert(VMFileHandle file)
{
    KVMFile *f = VMFileOf(file);

    KernelSetError(VMSaveRevertCommon(f, FALSE));
}

/* the header of a new file for VMSaveAs: the source's block table with
 * its blocks (USED and DUP) as USED blocks without a place, everything
 * else unassigned; the header's own entry stays the destination's */
static void CopyTable(KVMFile *d, const KVMFile *s)
{
    word last = VHGet16(s, VMH_LAST_HANDLE), h, used = 0;
    byte own[VMBH_SIZE];

    memcpy(own, d->VF_hdr + VM_HEADER_BLOCK, VMBH_SIZE);
    VMReserveHeader(d, last);
    memcpy(d->VF_hdr, s->VF_hdr, last);
    memcpy(d->VF_hdr + VM_HEADER_BLOCK, own, VMBH_SIZE);
    VHSet16(d, VMH_ASSIGNED_PTR, 0);
    VHSet16(d, VMH_LAST_ASSIGNED, 0);
    VHSet16(d, VMH_NUM_ASSIGNED, 0);
    VHSet16(d, VMH_UNASSIGNED_PTR, 0);
    VHSet16(d, VMH_NUM_UNASSIGNED, 0);
    VHSet32(d, VMH_USED_SIZE, VHGet16(d, VM_HEADER_BLOCK + VMBH_FILE_SIZE));
    for (h = last - VMBH_SIZE; h > VM_HEADER_BLOCK; h -= VMBH_SIZE) {
	if (VMIsUsed(s, h)) {
	    VHSet8(d, h + VMBH_SIG, VMBT_USED);
	    VHSet8(d, h + VMBH_FLAGS,
		   (byte) (d->VF_hdr[h + VMBH_FLAGS] & ~VMBF_HAS_BACKUP));
	    VHSet16(d, h + VMBH_MEM_HANDLE, 0);
	    VHSet16(d, h + VMBH_FILE_SIZE, 0);
	    VHSet32(d, h + VMBH_FILE_POS, 0);
	    used++;
	} else
	    VMUnassignBlk(d, h);
    }
    VHSet16(d, VMH_NUM_USED, (word) (used + 1));
    VHSet16(d, VMH_NUM_RESIDENT, 1);
}

/* VMSaveAsTransferHeader: the GEOS file header's attributes, not the
 * template or sharing flags */
static void TransferAttrs(KVMFile *s, KVMFile *d)
{
    word flags = 0;
    ReleaseNumber rel;
    ProtocolNumber proto;
    GeodeToken token, creator;
    char notes[GFH_USER_NOTES_BUFFER_SIZE];
    FileExtAttrDesc a[6];
    void *v[6];
    word sz[6];
    static const FileExtendedAttribute which[6] = {
	FEA_FLAGS, FEA_RELEASE, FEA_PROTOCOL, FEA_TOKEN, FEA_CREATOR,
	FEA_USER_NOTES
    };
    int i;

    v[0] = &flags; sz[0] = sizeof(flags);
    v[1] = &rel; sz[1] = sizeof(rel);
    v[2] = &proto; sz[2] = sizeof(proto);
    v[3] = &token; sz[3] = sizeof(token);
    v[4] = &creator; sz[4] = sizeof(creator);
    v[5] = notes; sz[5] = sizeof(notes);
    for (i = 0; i < 6; i++) {
	a[i].FEAD_attr = which[i];
	a[i].FEAD_value = v[i];
	a[i].FEAD_size = sz[i];
	a[i].FEAD_name = NULL;
    }
    if (FileGetHandleExtAttributes(s->VF_file, FEA_MULTIPLE, a, 6))
	return;
    flags &= (word) ~(GFHF_TEMPLATE | GFHF_SHARED_MULTIPLE |
		      GFHF_SHARED_SINGLE);
    FileSetHandleExtAttributes(d->VF_file, FEA_MULTIPLE, a, 6);
}

/* VMSaveAs: the current version of the file to a new one (whose handle
 * is returned, the status set as by VMOpen); the file goes back to its
 * last save and is closed.  The blocks in memory, and so their handles,
 * go to the new file. */
VMFileHandle VMSaveAs(VMFileHandle file, const char *name,
		      VMAccessFlags flags, VMOpenType mode, word compression)
{
    KVMFile *s = VMFileOf(file), *d;
    char buf[PATH_BUFFER_SIZE];
    VMFileHandle nf;
    word h, last, e = 0, status;

    strncpy(buf, name, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    nf = VMOpen(buf, flags, mode, compression);
    if (!nf)
	return 0;
    status = ThreadGetError();
    d = VMFileOf(nf);
    TransferAttrs(s, d);
    CopyTable(d, s);
    last = VHGet16(s, VMH_LAST_HANDLE);
    for (h = VM_HEADER_BLOCK + VMBH_SIZE; h < last && !e; h += VMBH_SIZE) {
	word i = (word) VM_INDEX(h);
	MemHandle mh;
	if (!VMIsUsed(s, h))
	    continue;
	mh = s->VF_mem[i];
	if (mh && !(MemGetInfo(mh, MGIT_FLAGS_AND_LOCK_COUNT) &
		    HF_DISCARDED)) {
	    /* VMTransfer: the memory moves */
	    s->VF_mem[i] = 0;
	    s->VF_dirty[i] = FALSE;
	    d->VF_mem[i] = mh;
	    d->VF_dirty[i] = TRUE;
	    HandleModifyOwner(mh, nf);
	} else {
	    /* the bytes as they are in the file */
	    word size = VHGet16(s, h + VMBH_FILE_SIZE);
	    byte *data = size ? ghal_mem_alloc(size, 0) : NULL;
	    if (size && !data)
		e = ERROR_INSUFFICIENT_MEMORY;
	    else if (size) {
		FilePos(s->VF_file, VHGet32(s, h + VMBH_FILE_POS),
			FILE_POS_START);
		if (FileRead(s->VF_file, data, size, FALSE) != size)
		    e = ERROR_SHORT_READ_WRITE;
		else
		    e = VMWriteData(d, h, data, size);
	    }
	    ghal_mem_free(data);
	    if (mh) {		/* a preserved handle: the new file's */
		s->VF_mem[i] = 0;
		d->VF_mem[i] = mh;
		HandleModifyOwner(mh, nf);
	    }
	}
    }
    d->VF_reloc = s->VF_reloc;
    d->VF_execThread = s->VF_execThread;
    if (!e)
	e = VMUpdateNoBackup(d);
    if (e) {
	/* undo: the memory back to the file, the new one deleted */
	for (h = VM_HEADER_BLOCK + VMBH_SIZE; h < last; h += VMBH_SIZE) {
	    word i = (word) VM_INDEX(h);
	    if (d->VF_mem[i] && VMIsUsed(s, h)) {
		s->VF_mem[i] = d->VF_mem[i];
		s->VF_dirty[i] = TRUE;
		HandleModifyOwner(s->VF_mem[i], file);
		d->VF_mem[i] = 0;
	    }
	}
	d->VF_readOnly = TRUE;		/* nothing more to write */
	VMClose(nf, FALSE);
	FileDelete(buf);
	if (e == ERROR_SHORT_READ_WRITE)
	    e = VM_UPDATE_INSUFFICIENT_DISK_SPACE;
	KernelSetError(e);
	return 0;
    }
    (void) VMSaveRevertCommon(s, FALSE);
    s->VF_refs = 1;
    VMClose(file, FALSE);
    KernelSetError(status);
    return nf;
}

/* VMDiscardDirtyBlocks: what was not written goes (the blocks are read
 * again when locked); a file not in backup mode reads its header again,
 * a backup file restores the zombies that still have their place */
word VMDiscardDirtyBlocks(VMFileHandle file)
{
    KVMFile *f = VMFileOf(file);
    word h, last = VHGet16(f, VMH_LAST_HANDLE), e = 0;
    Boolean any = f->VF_hdrDirty;

    for (h = VM_HEADER_BLOCK + VMBH_SIZE; h < last; h += VMBH_SIZE)
	any |= f->VF_dirty[VM_INDEX(h)];
    if (!any) {
	KernelSetError(0);
	return VM_UPDATE_NOTHING_DIRTY;
    }
    if (!(f->VF_hdr[VMH_ATTRIBUTES] & VMA_BACKUP)) {
	for (h = VM_HEADER_BLOCK + VMBH_SIZE; h < last; h += VMBH_SIZE)
	    if (f->VF_mem[VM_INDEX(h)]) {
		VHSet8(f, h + VMBH_FLAGS, (byte) (f->VF_hdr[h + VMBH_FLAGS] &
						  ~VMBF_PRESERVE_HANDLE));
		VMDiscardMemBlk(f, h);
	    }
	e = VMReadHeader(f);
    } else {
	for (h = VM_HEADER_BLOCK + VMBH_SIZE; h < last; h += VMBH_SIZE) {
	    byte sig = f->VF_hdr[h + VMBH_SIG];
	    if (sig == VMBT_ZOMBIE && VHGet16(f, h + VMBH_FILE_SIZE)) {
		VHSet8(f, h + VMBH_SIG, VMBT_DUP);
		VHSet16(f, VMH_NUM_USED,
			(word) (VHGet16(f, VMH_NUM_USED) + 1));
	    } else if (sig == VMBT_BACKUP) {
		/* a used block freed since the update: back to used, its
		 * place from the backup */
		VMBlockHandle u = VHGet16(f, h + VMBH_UID);
		if (f->VF_hdr[u + VMBH_SIG] == VMBT_ZOMBIE &&
		    !VHGet16(f, u + VMBH_FILE_SIZE)) {
		    VMHighSetSig(f, u, VMBT_USED);
		    VHSet16(f, VMH_NUM_USED,
			    (word) (VHGet16(f, VMH_NUM_USED) + 1));
		    memcpy(f->VF_hdr + u + VMBH_FILE_SIZE,
			   f->VF_hdr + h + VMBH_FILE_SIZE, 6);
		    VHSet16(f, h + VMBH_FILE_SIZE, 0);
		    VMFreeEntry(f, h);
		}
	    } else if (f->VF_mem[VM_INDEX(h)] && f->VF_dirty[VM_INDEX(h)])
		VMDiscardMemBlk(f, h);
	}
    }
    KernelSetError(e);
    return e;
}

/* ---------------------------------------------------------------------
 * Exclusive access and other information
 * ------------------------------------------------------------------- */

/* VMGrabExclusive: the file to this thread (again: counted) until
 * VMReleaseExclusive; timeout in 1/10 s (0: wait), VMSERV_TIMEOUT with
 * the owner's operation */
VMStartExclusiveReturnValue VMGrabExclusive(VMFileHandle file,
					    word timeout,
					    VMOperation operation,
					    VMOperation *currentOperation)
{
    KVMFile *f = VMFileOf(file);
    ThreadHandle me = KernelThisThread();

    if (!f->VF_exclSem)
	f->VF_exclSem = ThreadAllocSem(1);
    if (f->VF_exclOwner != me) {
	if (!timeout)
	    ThreadPSem(f->VF_exclSem);
	else if (ThreadPTimedSem(f->VF_exclSem,
				 (word) (timeout * 6)) == SE_TIMEOUT) {
	    if (currentOperation)
		*currentOperation = (VMOperation) f->VF_exclOp;
	    return VMSERV_TIMEOUT;
	}
	f->VF_exclOwner = me;
	f->VF_exclOp = operation;
    }
    f->VF_exclCount++;
    if (ReadWord(f, VMFH_UPDATE_COUNTER) != f->VF_updateCount) {
	f->VF_updateCount = ReadWord(f, VMFH_UPDATE_COUNTER);
	return VMSERV_CHANGES;
    }
    return VMSERV_NO_CHANGES;
}

/* VMReleaseExclusive: a changed file is updated */
void VMReleaseExclusive(VMFileHandle file)
{
    KVMFile *f = VMFileOf(file);

    if (f->VF_exclOwner != KernelThisThread() || !f->VF_exclCount)
	return;
    if (--f->VF_exclCount)
	return;
    if (!f->VF_readOnly)
	(void) VMHighUpdate(f);
    f->VF_exclOwner = 0;
    ThreadVSem(f->VF_exclSem);
}

/* TRUE if the file was written since this handle last read or wrote
 * its update counter */
Boolean VMCheckForModifications(VMFileHandle file)
{
    KVMFile *f = VMFileOf(file);

    return (Boolean) (ReadWord(f, VMFH_UPDATE_COUNTER) != f->VF_updateCount);
}

void VMSetDirtyLimit(VMFileHandle file, word dirtyLimit)
{
    VMFileOf(file)->VF_dirtyLimit = dirtyLimit;
}

void VMEnforceHandleLimits(VMFileHandle file, word low, word high)
{
    (void) VMFileOf(file);
    (void) low;
    (void) high;
}
