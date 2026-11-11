/*
 * vmemHigh.c -- the VM file routines (after vmemHigh.asm,
 * vmemKernelHigh.asm and vmemC.asm).
 *
 * A VMFileHandle is a SIG_VM handle owned by the process that opened the
 * file; it points to the KVMFile and owns the memory of the blocks, so
 * MemOwner of a block is the file's owner.  Opening a file that is open
 * already gives the same handle again (VMClose counts down).  A block
 * is read when it is first locked and stays in memory until the file is
 * closed; VMUpdate and VMClose write what is dirty and then the header.
 *
 * Not yet: backup mode (VMSave, VMRevert, VMSaveAs), sharing between
 * processes and VMGrabExclusive, discarding clean blocks, relocation
 * (VMSetReloc, object blocks), LMem blocks in VM files, the GEOS file
 * header of the file (a native file for now), compression.
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

static word OpenStatus(word e)
{
    return e == ERROR_FILE_NOT_FOUND ? VM_FILE_NOT_FOUND
	 : e == ERROR_FILE_EXISTS ? VM_FILE_EXISTS : e;
}

VMFileHandle VMOpen(char *name, VMAccessFlags flags, VMOpenType mode,
		    word compression)
{
    char host[HOST_PATH_SIZE], tmp[HOST_PATH_SIZE];
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
	name = tmp;
	create = TRUE;
    }
    if (!FileHostPath(name, host, sizeof(host))) {
	if (fh)
	    FileClose(fh, FALSE);
	return 0;
    }
    /* open already: the same handle */
    for (f = vmFiles; f; f = f->VF_next)
	if (!strcmp(f->VF_path, host)) {
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
	exists = ghal_fs_stat(host, NULL) == GHAL_OK;
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
    strcpy(f->VF_path, FileOf(fh)->KF_path);	/* the name it got */
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
    f->VF_next = vmFiles;
    vmFiles = f;
    KernelSetError(create ? VM_CREATE_OK : readOnly ? VM_OPEN_OK_READ_ONLY
				      : VM_OPEN_OK_READ_WRITE_NOT_SHARED);
    return f->VF_handle;
}

static word Update(KVMFile *f)
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
	if (e)
	    return e;
	return 0;
    }
    return VM_UPDATE_NOTHING_DIRTY;
}

word VMUpdate(VMFileHandle file)
{
    word e = Update(VMFileOf(file));

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
	e = Update(f);
	if (e == VM_UPDATE_NOTHING_DIRTY)
	    e = 0;
    }
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

static MemHandle InMemory(KVMFile *f, VMBlockHandle block)
{
    VMCheckBlock(f, block);
    if (!f->VF_mem[VM_INDEX(block)]) {
	word e = VMReadBlock(f, block);
	if (e) {
	    KernelSetError(e);
	    return 0;
	}
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

void VMFree(VMFileHandle file, VMBlockHandle block)
{
    KVMFile *f = VMFileOf(file);

    VMCheckBlock(f, block);
    VMFreeBlockLow(f, block);
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
    return mh;
}

void VMPreserveBlocksHandle(VMFileHandle file, VMBlockHandle block)
{
    KVMFile *f = VMFileOf(file);

    VMCheckBlock(f, block);
    VHSet8(f, block + VMBH_FLAGS,
	   (byte) (f->VF_hdr[block + VMBH_FLAGS] | VMBF_PRESERVE_HANDLE));
}

VMBlockHandle VMCopyVMBlock(VMFileHandle sourceFile,
			    VMBlockHandle sourceBlock, VMFileHandle destFile)
{
    MemHandle smh, dmh;
    const void *src = VMLock(sourceFile, sourceBlock, &smh);
    word size;
    VMBlockHandle nb;
    KVMFile *sf = VMFileOf(sourceFile);

    if (!src)
	return 0;
    size = (word) MemGetInfo(smh, MGIT_SIZE);
    nb = VMAlloc(destFile, size,
		 VHGet16(sf, sourceBlock + VMBH_UID));
    memcpy(VMLock(destFile, nb, &dmh), src, size);
    VMUnlock(dmh);
    VMUnlock(smh);
    return nb;
}
