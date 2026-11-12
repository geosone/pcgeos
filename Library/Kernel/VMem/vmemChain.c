/*
 * vmemChain.c -- VM chains (after vmemChain.asm).
 *
 * A VMChain is a block handle in the high word and 0 in the low word.
 * The block starts with a VMChainLink: the next block of the chain, or
 * VM_CHAIN_TREE for a tree block, whose VMChainTree gives the offset
 * and count of the VMChains (little-endian dwords, 0 for none) of its
 * branches.  These are file data, read byte by byte.
 *
 * The link of a huge array block (vmemHugeArray.c) is HAD_data or
 * HAB_next; a copied huge array gets its own handles
 * (FixupHugeArrayChain), and two are compared element by element.
 *
 * A VMChain with an item in the low word is a DB item (group and item):
 * it is copied to DB_UNGROUPED of the other file, freed, compared byte
 * for byte, and counted as an item by VMInfoVMChain.
 */
#include "vmemInt.h"
#include "ghal.h"
#include <dbase.h>

#include <string.h>

static word Get16(const byte *p)
{
    return (word) (p[0] | (p[1] << 8));
}

static dword Get32(const byte *p)
{
    return (dword) Get16(p) | ((dword) Get16(p + 2) << 16);
}

static void Put16(byte *p, word v)
{
    p[0] = (byte) v;
    p[1] = (byte) (v >> 8);
}

static void Put32(byte *p, dword v)
{
    Put16(p, (word) v);
    Put16(p + 2, (word) (v >> 16));
}

static word Uid(VMFileHandle file, VMBlockHandle b)
{
    return VHGet16(VMFileOf(file), (word) (b + VMBH_UID));
}

/* the link of block b, locked at p */
static word Link(VMFileHandle file, VMBlockHandle b, const byte *p)
{
    Boolean ha;
    word l = VMHugeArrayLink(Uid(file, b), p, &ha);

    return ha ? l : Get16(p);
}

static void SetLink(VMFileHandle file, VMBlockHandle b, byte *p, word l)
{
    if (!VMHugeArraySetLink(Uid(file, b), p, l))
	Put16(p, l);
}

#define IS_DB_ITEM(chain)	((word) (chain) != 0)

static VMBlockHandle BlockOf(VMChain chain)
{
    return VMCHAIN_GET_VM_BLOCK(chain);
}

/* the branches of a tree block, in a buffer of the caller (count) */
static word Branches(VMFileHandle file, VMBlockHandle b, dword **out)
{
    MemHandle mh;
    const byte *p = VMLock(file, b, &mh);
    word off = Get16(p + 2), count = Get16(p + 4), i;
    dword *br = count ? ghal_mem_alloc(count * sizeof(dword), 0) : NULL;

    if (count && !br)
	KernelFatal(VM_ALLOC_FAILED);
    for (i = 0; i < count; i++)
	br[i] = Get32(p + off + 4 * i);
    VMUnlock(mh);
    *out = br;
    return count;
}

static Boolean IsTree(VMFileHandle file, VMBlockHandle b, VMBlockHandle *next)
{
    MemHandle mh;
    const byte *p = VMLock(file, b, &mh);
    word n = Link(file, b, p);

    VMUnlock(mh);
    *next = n == VM_CHAIN_TREE ? 0 : n;
    return n == VM_CHAIN_TREE;
}

void VMFreeVMChain(VMFileHandle file, VMChain chain)
{
    VMBlockHandle b = BlockOf(chain), next;

    if (IS_DB_ITEM(chain)) {
	if (DBInfoUngrouped(file, chain, NULL))
	    DBFreeUngrouped(file, chain);
	return;
    }
    while (b) {
	if (IsTree(file, b, &next)) {
	    dword *br;
	    word n = Branches(file, b, &br), i;
	    for (i = 0; i < n; i++)
		VMFreeVMChain(file, br[i]);
	    ghal_mem_free(br);
	}
	VMFree(file, b);
	b = next;
    }
}

VMChain VMCopyVMChain(VMFileHandle sourceFile, VMChain sourceChain,
		      VMFileHandle destFile)
{
    VMBlockHandle b = BlockOf(sourceChain), next, nb, prev = 0, first = 0;
    MemHandle mh;
    byte *p;

    if (IS_DB_ITEM(sourceChain))
	return DBRawCopyDBItem(sourceFile, sourceChain, destFile,
			       DB_UNGROUPED);
    while (b) {
	Boolean tree = IsTree(sourceFile, b, &next);
	nb = VMCopyVMBlock(sourceFile, b, destFile);
	if (!first)
	    first = nb;
	if (tree) {
	    dword *br;
	    word n = Branches(sourceFile, b, &br), i, off;
	    for (i = 0; i < n; i++)
		br[i] = br[i] ? VMCopyVMChain(sourceFile, br[i], destFile) : 0;
	    p = VMLock(destFile, nb, &mh);
	    off = Get16(p + 2);
	    for (i = 0; i < n; i++)
		Put32(p + off + 4 * i, br[i]);
	    VMDirty(mh);
	    VMUnlock(mh);
	    ghal_mem_free(br);
	}
	if (prev) {
	    p = VMLock(destFile, prev, &mh);
	    SetLink(destFile, prev, p, nb);
	    VMDirty(mh);
	    VMUnlock(mh);
	}
	prev = tree ? 0 : nb;
	b = next;
    }
    if (prev) {				/* the copy's last link */
	p = VMLock(destFile, prev, &mh);
	SetLink(destFile, prev, p, 0);
	VMDirty(mh);
	VMUnlock(mh);
    }
    if (first && Uid(destFile, first) == SVMID_HA_DIR_ID)
	VMHugeArrayFixup(destFile, first);
    return VMCHAIN_MAKE_FROM_VM_BLOCK(first);
}

/* compare the blocks byte for byte, links and branches as chains */
Boolean VMCompareVMChains(VMFileHandle sourceFile, VMChain sourceChain,
			  VMFileHandle destFile, VMChain destChain)
{
    VMBlockHandle a = BlockOf(sourceChain), b = BlockOf(destChain);
    VMBlockHandle na, nb;

    if (IS_DB_ITEM(sourceChain) || IS_DB_ITEM(destChain)) {
	const byte *pa, *pb;
	word sa, sb;
	Boolean same;

	if (!IS_DB_ITEM(sourceChain) || !IS_DB_ITEM(destChain) ||
	    !DBInfoUngrouped(sourceFile, sourceChain, &sa) ||
	    !DBInfoUngrouped(destFile, destChain, &sb) || sa != sb)
	    return FALSE;
	pa = DBLockUngrouped(sourceFile, sourceChain);
	pb = DBLockUngrouped(destFile, destChain);
	same = memcmp(pa, pb, sa) == 0;
	DBUnlock((void *) pb);
	DBUnlock((void *) pa);
	return same;
    }
    if (a && b && (Uid(sourceFile, a) == SVMID_HA_DIR_ID ||
		   Uid(destFile, b) == SVMID_HA_DIR_ID))
	return Uid(sourceFile, a) == Uid(destFile, b) &&
	       VMHugeArrayCompare(sourceFile, a, destFile, b);
    while (a && b) {
	MemHandle ma, mb;
	const byte *pa, *pb;
	word sa, sb, i, n = 0, off = 0;
	Boolean ta = IsTree(sourceFile, a, &na), tb = IsTree(destFile, b, &nb);
	Boolean same;

	if (ta != tb)
	    return FALSE;
	pa = VMLock(sourceFile, a, &ma);
	pb = VMLock(destFile, b, &mb);
	sa = (word) MemGetInfo(ma, MGIT_SIZE);
	sb = (word) MemGetInfo(mb, MGIT_SIZE);
	if (ta) {
	    off = Get16(pa + 2);
	    n = Get16(pa + 4);
	}
	same = sa == sb;
	if (same && ta)
	    /* the branches are handles: compare everything else */
	    same = Get16(pb + 2) == off && Get16(pb + 4) == n &&
		   (dword) off + 4u * n <= sa &&
		   memcmp(pa + 2, pb + 2, (size_t) (off - 2)) == 0 &&
		   memcmp(pa + off + 4 * n, pb + off + 4 * n,
			  (size_t) (sa - off - 4 * n)) == 0;
	else if (same)
	    same = memcmp(pa + 2, pb + 2, (size_t) (sa - 2)) == 0;
	VMUnlock(ma);
	VMUnlock(mb);
	if (!same)
	    return FALSE;
	if (ta) {
	    dword *ba, *bb;
	    Branches(sourceFile, a, &ba);
	    Branches(destFile, b, &bb);
	    for (i = 0; i < n && same; i++)
		same = (!ba[i] && !bb[i]) ||
		       (ba[i] && bb[i] &&
			VMCompareVMChains(sourceFile, ba[i], destFile, bb[i]));
	    ghal_mem_free(ba);
	    ghal_mem_free(bb);
	    if (!same)
		return FALSE;
	}
	a = na;
	b = nb;
    }
    return !a && !b;
}

static Boolean InfoChain(VMFileHandle file, VMChain chain, dword *size,
			 word *blocks, word *items)
{
    VMBlockHandle b = BlockOf(chain), next;
    VMInfoStruct info;

    if (IS_DB_ITEM(chain)) {
	word n;
	if (!DBInfoUngrouped(file, chain, &n))
	    return FALSE;
	*size += n;
	(*items)++;
	return TRUE;
    }
    while (b) {
	if (!VMInfo(file, b, &info))
	    return FALSE;
	*size += info.size;
	(*blocks)++;
	if (IsTree(file, b, &next)) {
	    dword *br;
	    word n = Branches(file, b, &br), i;
	    Boolean ok = TRUE;
	    for (i = 0; i < n && ok; i++)
		ok = !br[i] || InfoChain(file, br[i], size, blocks, items);
	    ghal_mem_free(br);
	    if (!ok)
		return FALSE;
	}
	b = next;
    }
    return TRUE;
}

Boolean VMInfoVMChain(VMFileHandle sourceFile, VMChain sourceChain,
		      dword *chainSize, word *vmBlockCount,
		      word *dbItemCount)
{
    dword size = 0;
    word blocks = 0, items = 0;
    Boolean ok = InfoChain(sourceFile, sourceChain, &size, &blocks, &items);

    if (chainSize)
	*chainSize = size;
    if (vmBlockCount)
	*vmBlockCount = blocks;
    if (dbItemCount)
	*dbItemCount = items;
    return ok;
}
