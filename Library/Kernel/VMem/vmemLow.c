/*
 * vmemLow.c -- after vmemLow.asm: Low-level VM file manipulation
 */
#include "vmemInt.h"
#include <string.h>

/* VMDiscardMemBlk: the memory of an entry goes; a block whose handle is
 * preserved (VMBF_PRESERVE_HANDLE) keeps it, discarded, and is read
 * into it again when locked */
void VMDiscardMemBlk(KVMFile *f, VMBlockHandle h)
{
    word i = (word) VM_INDEX(h);
    MemHandle mh = f->VF_mem[i];

    f->VF_dirty[i] = FALSE;
    if (!mh)
	return;
    if (f->VF_hdr[h + VMBH_FLAGS] & VMBF_PRESERVE_HANDLE)
	(void) MemReAlloc(mh, 0, 0);
    else {
	MemFree(mh);
	f->VF_mem[i] = 0;
    }
}

/* VMUpdateNoBackup: the dirty blocks written in place, as USED */
word VMUpdateNoBackup(KVMFile *f)
{
    word e;

    if (f->VF_readOnly)
	return 0;
    f->VF_noBackup = TRUE;
    e = VMHighUpdate(f);
    f->VF_noBackup = FALSE;
    return e == VM_UPDATE_NOTHING_DIRTY ? 0 : e;
}

/* VMSaveRevertCommon: the entries from the last to the first */
word VMSaveRevertCommon(KVMFile *f, Boolean save)
{
    word h;

    for (h = VHGet16(f, VMH_LAST_HANDLE) - VMBH_SIZE; h > VM_HEADER_BLOCK;
	 h -= VMBH_SIZE) {
	byte sig = f->VF_hdr[h + VMBH_SIG];
	byte flags = f->VF_hdr[h + VMBH_FLAGS];
	if (!(sig & VM_IN_USE_BIT))
	    continue;
	if (sig == VMBT_BACKUP) {
	    VMBlockHandle d = VHGet16(f, h + VMBH_UID);
	    if (save) {
		/* the new version stays; a zombie goes */
		if (f->VF_hdr[d + VMBH_SIG] == VMBT_ZOMBIE)
		    VMFreeEntry(f, d);
		else
		    VMHighSetSig(f, d, VMBT_USED);
	    } else {
		/* the backup's place goes back to the block (VMExchangeWords
		 * of the size and the position), its memory is out of
		 * date */
		byte t[6];
		memcpy(t, f->VF_hdr + d + VMBH_FILE_SIZE, 6);
		memcpy(f->VF_hdr + d + VMBH_FILE_SIZE,
		       f->VF_hdr + h + VMBH_FILE_SIZE, 6);
		memcpy(f->VF_hdr + h + VMBH_FILE_SIZE, t, 6);
		VMDiscardMemBlk(f, d);
		if (f->VF_hdr[d + VMBH_SIG] == VMBT_ZOMBIE)
		    VHSet16(f, VMH_NUM_USED,
			    (word) (VHGet16(f, VMH_NUM_USED) + 1));
		VMHighSetSig(f, d, VMBT_USED);
	    }
	    VMFreeEntry(f, h);
	} else if (sig == VMBT_DUP) {
	    if (flags & VMBF_HAS_BACKUP)
		continue;		/* through its backup */
	    if (save)
		VHSet8(f, h + VMBH_SIG, VMBT_USED);
	    else {
		/* new since the save: gone */
		if (h == VHGet16(f, VMH_DB_MAP_BLOCK))
		    VHSet16(f, VMH_DB_MAP_BLOCK, 0);
		VMFreeBlockLow(f, h);
	    }
	} else if (sig == VMBT_USED) {
	    if (!save && f->VF_dirty[VM_INDEX(h)])
		VMDiscardMemBlk(f, h);
	} else if (sig == VMBT_ZOMBIE && !(flags & VMBF_HAS_BACKUP))
	    VMFreeEntry(f, h);
    }
    f->VF_hdrDirty = TRUE;
    return VMUpdateNoBackup(f);
}
