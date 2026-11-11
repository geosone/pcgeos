/*
 * vmemInt.h -- the VM file format and the routines of the VMem module
 * shared by its files (after vmemConstant.def).
 *
 * The file format is that of 16-bit GEOS on every host: a VMFileHeader
 * at position 0 (of the data of a GEOS file), the blocks, and the VM
 * header block with the block table.  A VMBlockHandle is the offset of
 * its entry in the header, so block handles stay the same as in DOS16
 * files.  The header is kept in memory as it is in the file (little
 * endian, 16-bit fields) and read and written with the accessors below;
 * the memory of a block is in KVMFile.VF_mem, as the 16-bit memory
 * handles of the file cannot hold ours (VMBH_memHandle stays 0).
 */
#ifndef VMEMINT_H
#define VMEMINT_H

#include "kernelC.h"
#include <vm.h>

/* VMFileHeader */
#define VMFH_SIGNATURE		0
#define VMFH_HEADER_SIZE	2
#define VMFH_HEADER_POS		4
#define VMFH_UPDATE_COUNTER	8
#define VMFH_UPDATE_TYPE	10
#define VMFH_SIZE		24
#define VM_FILE_SIG		0xadeb

/* VMHeader */
#define VMH_HEADER_SIG		0
#define VMH_ASSIGNED_PTR	2
#define VMH_LAST_ASSIGNED	4
#define VMH_UNASSIGNED_PTR	6
#define VMH_LAST_HANDLE		8
#define VMH_NUM_ASSIGNED	10
#define VMH_NUM_UNASSIGNED	12
#define VMH_NUM_USED		14
#define VMH_NUM_RESIDENT	16
#define VMH_NUM_EXTRA_UNASSIGNED 18
#define VMH_MAP_BLOCK		20
#define VMH_COMPACTION_THRESHOLD 22
#define VMH_USED_SIZE		24
#define VMH_ATTRIBUTES		28
#define VMH_COMPRESS_FLAGS	29
#define VMH_DB_MAP_BLOCK	30
#define VMH_BLOCK_TABLE		32
#define VM_HDR_SIG		0x00fb

/* VMBlockHandle (used) and VMFreeBlockHandle */
#define VMBH_SIZE		12
#define VMBH_MEM_HANDLE		0
#define VMBH_SIG		2
#define VMBH_FLAGS		3
#define VMBH_UID		4
#define VMBH_FILE_SIZE		6
#define VMBH_FILE_POS		8
#define VMFBH_NEXT		0
#define VMFBH_PREV		2
#define VMFBH_FILE_SIZE		4
#define VMFBH_FILE_POS		8

#define VMBT_USED		0xff
#define VMBT_DUP		0xfd
#define VMBT_BACKUP		0xfb
#define VMBT_ZOMBIE		0xf9
#define VM_IN_USE_BIT		0x01

#define VMBF_LMEM		0x01
#define VMBF_HAS_BACKUP		0x02
#define VMBF_PRESERVE_HANDLE	0x04

#define VM_INIT_NUM_BLKS	4
#define VM_EXTEND_NUM_BLKS	4
#define VM_DEF_COMPACTION_THRESHOLD 70
#define VM_HEADER_BLOCK		VMH_BLOCK_TABLE	/* its own entry */

/* little-endian fields of the header */
static inline word VHGet16(const KVMFile *f, word off)
{
    return (word) (f->VF_hdr[off] | (f->VF_hdr[off + 1] << 8));
}
static inline dword VHGet32(const KVMFile *f, word off)
{
    return (dword) VHGet16(f, off) | ((dword) VHGet16(f, off + 2) << 16);
}
static inline void VHSet16(KVMFile *f, word off, word v)
{
    f->VF_hdr[off] = (byte) v;
    f->VF_hdr[off + 1] = (byte) (v >> 8);
    f->VF_hdrDirty = TRUE;
}
static inline void VHSet32(KVMFile *f, word off, dword v)
{
    VHSet16(f, off, (word) v);
    VHSet16(f, off + 2, (word) (v >> 16));
}
static inline void VHSet8(KVMFile *f, word off, byte v)
{
    f->VF_hdr[off] = v;
    f->VF_hdrDirty = TRUE;
}

/* the index of an entry in VF_mem and VF_dirty */
#define VM_INDEX(h)	(((h) - VMH_BLOCK_TABLE) / VMBH_SIZE)

/* vmemBlkManip.c */
KVMFile *VMFileOf(VMFileHandle vm);
Boolean VMIsUsed(const KVMFile *f, VMBlockHandle h);
void VMCheckBlock(const KVMFile *f, VMBlockHandle h);	/* fatal if not */
VMBlockHandle VMGetUnassignedBlk(KVMFile *f);
void VMUnassignBlk(KVMFile *f, VMBlockHandle h);
void VMMarkBlkUsed(KVMFile *f, VMBlockHandle h);
void VMLinkFree(KVMFile *f, word count);
/* room for a header of `size' bytes */
void VMReserveHeader(KVMFile *f, dword size);
void VMReleaseSpace(KVMFile *f, dword pos, dword size);
dword VMUseSpace(KVMFile *f, word size);
word VMReadBlock(KVMFile *f, VMBlockHandle h);		/* error */
word VMWriteBlock(KVMFile *f, VMBlockHandle h);		/* error */
/* write `size' bytes of `data' as the contents of entry h, padded to 16
 * bytes, in place if they fit */
word VMWriteData(KVMFile *f, VMBlockHandle h, const void *data, word size);
void VMFreeBlockLow(KVMFile *f, VMBlockHandle h);

/* vmemHeader.c */
word VMCreateHeader(KVMFile *f);			/* error */
word VMReadHeader(KVMFile *f);				/* error */
word VMWriteHeader(KVMFile *f);				/* error */

#endif
