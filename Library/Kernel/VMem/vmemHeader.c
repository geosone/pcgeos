/*
 * vmemHeader.c -- the VM header and the VMFileHeader (after
 * vmemHeader.asm).
 *
 * The header is the block of the first table entry; its own entry
 * holds the place of the header in the file, so that the entry's
 * (uid, size, pos) is what the VMFileHeader at the start of the file
 * repeats, with the file signature for the user ID (the kernel keeps
 * the signature there, other writers may not).
 */
#include "vmemInt.h"

#include <string.h>

static word Put16(byte *p, word v)
{
    p[0] = (byte) v;
    p[1] = (byte) (v >> 8);
    return v;
}

static word Get16(const byte *p)
{
    return (word) (p[0] | (p[1] << 8));
}

word VMCreateHeader(KVMFile *f)
{
    byte fh[VMFH_SIZE];

    /* the fixed part and the header's own entry, a used block */
    VMReserveHeader(f, VMH_BLOCK_TABLE + VMBH_SIZE);
    VHSet16(f, VMH_HEADER_SIG, VM_HDR_SIG);
    VHSet16(f, VMH_LAST_HANDLE, VMH_BLOCK_TABLE + VMBH_SIZE);
    VHSet16(f, VMH_COMPACTION_THRESHOLD, VM_DEF_COMPACTION_THRESHOLD);
    VMMarkBlkUsed(f, VM_HEADER_BLOCK);
    VHSet16(f, VM_HEADER_BLOCK + VMBH_UID, VM_FILE_SIG);
    VHSet16(f, VMH_NUM_RESIDENT, 1);
    VMLinkFree(f, VM_INIT_NUM_BLKS - 1);

    memset(fh, 0, sizeof(fh));
    Put16(fh + VMFH_SIGNATURE, VM_FILE_SIG);
    FilePos(f->VF_file, 0, FILE_POS_START);
    if (FileWrite(f->VF_file, fh, sizeof(fh), FALSE) != sizeof(fh))
	return VM_UPDATE_INSUFFICIENT_DISK_SPACE;
    return VMWriteHeader(f);
}

word VMReadHeader(KVMFile *f)
{
    byte fh[VMFH_SIZE];
    word size, last, h;
    dword pos, used;

    FilePos(f->VF_file, 0, FILE_POS_START);
    if (FileRead(f->VF_file, fh, sizeof(fh), FALSE) != sizeof(fh) ||
	Get16(fh + VMFH_SIGNATURE) != VM_FILE_SIG)
	return VM_OPEN_INVALID_VM_FILE;
    size = Get16(fh + VMFH_HEADER_SIZE);
    pos = (dword) Get16(fh + VMFH_HEADER_POS) |
	  ((dword) Get16(fh + VMFH_HEADER_POS + 2) << 16);
    if (size < VMH_BLOCK_TABLE + VMBH_SIZE ||
	pos + size > FileSize(f->VF_file))
	return VM_OPEN_INVALID_VM_FILE;
    VMReserveHeader(f, size);
    FilePos(f->VF_file, pos, FILE_POS_START);
    if (FileRead(f->VF_file, f->VF_hdr, size, FALSE) != size)
	return VM_OPEN_INVALID_VM_FILE;
    last = VHGet16(f, VMH_LAST_HANDLE);
    if (VHGet16(f, VMH_HEADER_SIG) != VM_HDR_SIG || last > size ||
	last < VMH_BLOCK_TABLE + VMBH_SIZE ||
	(last - VMH_BLOCK_TABLE) % VMBH_SIZE)
	return VM_OPEN_INVALID_VM_FILE;
    /* nothing of the last session's memory; the used size again */
    used = size;
    for (h = VM_HEADER_BLOCK + VMBH_SIZE; h < last; h += VMBH_SIZE)
	if (f->VF_hdr[h + VMBH_SIG] & VM_IN_USE_BIT) {
	    VHSet16(f, h + VMBH_MEM_HANDLE, 0);
	    used += VHGet16(f, h + VMBH_FILE_SIZE);
	}
    VHSet16(f, VM_HEADER_BLOCK + VMBH_MEM_HANDLE, 0);
    VHSet32(f, VMH_USED_SIZE, used);
    VHSet16(f, VMH_NUM_RESIDENT, 1);
    VHSet8(f, VMH_COMPRESS_FLAGS, 0);
    f->VF_hdrDirty = FALSE;
    return 0;
}

word VMWriteHeader(KVMFile *f)
{
    byte fh[8];
    word e;

    /* writing the header may free its old place: a spare entry, so that
     * the table does not grow while it is written */
    if (VHGet16(f, VMH_NUM_UNASSIGNED) < 2)
	VMLinkFree(f, VM_EXTEND_NUM_BLKS);
    e = VMWriteData(f, VM_HEADER_BLOCK, f->VF_hdr,
		    VHGet16(f, VMH_LAST_HANDLE));
    if (e)
	return e;
    /* the VMFileHeader repeats the header's entry, with the signature
     * in place of the user ID (VMUpdateHeader) */
    memcpy(fh, f->VF_hdr + VM_HEADER_BLOCK + VMBH_UID, 8);
    Put16(fh + VMFH_SIGNATURE, VM_FILE_SIG);
    FilePos(f->VF_file, VMFH_SIGNATURE, FILE_POS_START);
    if (FileWrite(f->VF_file, fh, sizeof(fh), FALSE) != sizeof(fh))
	return VM_UPDATE_INSUFFICIENT_DISK_SPACE;
    f->VF_hdrDirty = FALSE;
    return 0;
}
