/*
 * vmemKernelHigh.c -- after vmemKernelHigh.asm: VMMarkUngroupAvail,
 * VMFindAvailUngroup.
 */
#include "vmemInt.h"
#include "ghal.h"
#include <stdio.h>
#include <string.h>

void VMMarkUngroupAvail(VMFileHandle file, VMBlockHandle block)
{
    KVMFile *f = VMFileOf(file);

    VMCheckBlock(f, block);
    VHSet8(f, block + VMBH_FLAGS,
	   (byte) (f->VF_hdr[block + VMBH_FLAGS] | VMBF_UNGROUPED_AVAIL));
}

VMBlockHandle VMFindAvailUngroup(VMFileHandle file)
{
    KVMFile *f = VMFileOf(file);
    word h;

    for (h = VM_HEADER_BLOCK + VMBH_SIZE; h < VHGet16(f, VMH_LAST_HANDLE);
	 h += VMBH_SIZE)
	if (VMIsUsed(f, h) &&
	    (f->VF_hdr[h + VMBH_FLAGS] & VMBF_UNGROUPED_AVAIL)) {
	    VHSet8(f, h + VMBH_FLAGS, (byte) (f->VF_hdr[h + VMBH_FLAGS] &
					      ~VMBF_UNGROUPED_AVAIL));
	    return h;
	}
    return 0;
}
