/*
 * test_kernel_vm.c -- VM files of the C kernel (Library/Kernel/VMem) on
 * the file API, on every host.
 *
 * The GEOS top directory is GVTOP next to the program.  Checked:
 * creating and opening (and the status codes), blocks with user IDs, the
 * map block, VMFind/VMInfo, growing, shrinking and freeing blocks and
 * reusing their space, a block table that grows, attach and detach,
 * owners, chains and trees (copy, compare, info, free), read-only and
 * temporary files, and reading a VM file written by the 16-bit tools
 * (vm_reference.h), LMem blocks (the DOS16 layout in the file) and the
 * routine of VMSetReloc; huge arrays (fixed and variable size elements,
 * inserting, deleting, replacing, resizing, Next/Prev, Expand/Contract,
 * Enum, copied and compared as chains, written and read again).  A.VM,
 * B.VM, LM.VM, HA.VM and REF.VM stay for vmcheck.py (ctest
 * kernel.vm.format).
 */
#include "kernelC.h"
#include "ghal.h"
#include "hostfs.h"
#include <vm.h>
#include <lmem.h>
#include <hugearr.h>
#include "vm_reference.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static jmp_buf catcher;
static KernelFatalError caught;

static void Catch(KernelFatalError code)
{
    caught = code;
    longjmp(catcher, 1);
}

static char top[260];

static const char *H(const char *rel)
{
    static char buf[4][300];
    static int n;
    char *b = buf[n++ & 3];

    sprintf(b, "%s%c%s", top,
#ifdef GEOS_HOST_DOS32
	    '\\',
#else
	    '/',
#endif
	    rel);
    return b;
}

static void RemoveTree(const char *path)
{
    ghal_dir *d;
    ghal_fs_info info;
    char name[260], sub[400];

    if (ghal_fs_opendir(path, &d) == GHAL_OK) {
	while (ghal_fs_readdir(d, name, sizeof(name), &info) == GHAL_OK) {
	    sprintf(sub, "%s%c%s", path, HostFSSep(), name);
	    if (info.attrs & GHAL_FS_ATTR_DIR)
		RemoveTree(sub);
	    else {
		ghal_fs_set_attrs(sub, 0);
		ghal_fs_delete(sub);
	    }
	}
	ghal_fs_closedir(d);
    }
    ghal_fs_rmdir(path);
}

/* everything below the top (GEOS files have generated names on DOS) */
static void Cleanup(void)
{
    RemoveTree(top);
}

static void Fill(VMFileHandle vm, VMBlockHandle b, word size, byte seed)
{
    MemHandle mh;
    byte *p = VMLock(vm, b, &mh);
    word i;

    for (i = 0; i < size; i++)
	p[i] = (byte) (seed + i * 7);
    VMDirty(mh);
    VMUnlock(mh);
}

static Boolean Same(VMFileHandle vm, VMBlockHandle b, word size, byte seed)
{
    MemHandle mh;
    byte *p = VMLock(vm, b, &mh);
    word i;
    Boolean ok = p != NULL;

    for (i = 0; ok && i < size; i++)
	ok = p[i] == (byte) (seed + i * 7);
    VMUnlock(mh);
    return ok;
}

static void Resize(VMFileHandle vm, VMBlockHandle b, word size, byte seed)
{
    MemHandle mh;

    VMLock(vm, b, &mh);
    VMUnlock(mh);
    CHECK(MemReAlloc(mh, size, 0) == mh);
    Fill(vm, b, size, seed);
}

/* a block whose first word is a chain link */
static VMBlockHandle LinkBlock(VMFileHandle vm, word size, word next,
			       byte seed)
{
    VMBlockHandle b = VMAlloc(vm, size, 0x5150);
    MemHandle mh;
    byte *p;

    Fill(vm, b, size, seed);
    p = VMLock(vm, b, &mh);
    p[0] = (byte) next;
    p[1] = (byte) (next >> 8);
    VMDirty(mh);
    VMUnlock(mh);
    return b;
}

/* the size of the data of a GEOS file */
/* VMSetReloc: the calls by type, and the block of the last one */
static word relocCalls[3];
static VMBlockHandle relocBlock;

static void Reloc(VMFileHandle file, VMBlockHandle block, MemHandle mh,
		  void *data, VMRelocType type)
{
    (void) file;
    (void) data;
    (void) mh;
    if (type <= VMRT_RELOCATE_AFTER_WRITE)
	relocCalls[type]++;
    relocBlock = block;
}

static void FillChunk(optr o, word size, byte seed)
{
    byte *c = LMemDeref(o);
    word i;

    for (i = 0; i < size; i++)
	c[i] = (byte) (seed + i * 7);
}

static Boolean SameChunk(optr o, word size, byte seed)
{
    byte *c = LMemDeref(o);
    word i;

    if (LMemGetChunkSize(o) != size)
	return FALSE;
    for (i = 0; i < size; i++)
	if (c[i] != (byte) (seed + i * 7))
	    return FALSE;
    return TRUE;
}

static uint64_t SizeOf(const char *rel)
{
    dword size = 0;

    FileGetPathExtAttributes(rel, FEA_SIZE, &size, sizeof(size));
    return size;
}

/* the user's part of a huge array's header in the file right after
 * the DOS16 HugeArrayDirectory (26 bytes): HAD_self and HAD_size (0)
 * just before it */
static Boolean HAHeaderAt26(const char *rel, VMBlockHandle self)
{
    static byte buf[65536];
    FileHandle fh = FileOpen(rel, FILE_ACCESS_R | FILE_DENY_NONE);
    word n, i;
    Boolean ok = FALSE;

    if (!fh)
	return FALSE;
    n = (word) FileRead(fh, buf, sizeof(buf) - 1, FALSE);
    FileClose(fh, FALSE);
    for (i = 4; i + 8 <= n; i++)
	if (!memcmp(buf + i, "USERDATA", 8))
	    ok = buf[i - 4] == (byte) self && buf[i - 3] == (byte) (self >> 8) &&
		 !buf[i - 2] && !buf[i - 1];
    return ok;
}

/* huge arrays: element i of the fixed array holds dword values[i] */
static dword values[4000];
static dword nvalues;

static Boolean HASame(VMFileHandle vm, VMBlockHandle ha)
{
    dword i;

    if (HugeArrayGetCount(vm, ha) != nvalues)
	return FALSE;
    for (i = 0; i < nvalues; i++) {
	void *p;
	word size;
	dword r = HugeArrayLock(vm, ha, i, &p, &size);
	Boolean ok = HAL_COUNT(r) && size == 4 && *(dword *) p == values[i];
	if (HAL_COUNT(r))
	    HugeArrayUnlock(p);
	if (!ok) {
	    printf("element %lu: %lu want %lu size %u\n", (unsigned long) i, HAL_COUNT(r) ? (unsigned long) *(dword *) p : 0UL, (unsigned long) values[i], size);
	    return FALSE;
	}
    }
    return TRUE;
}

static void HAInsert(dword at, word n, dword base)
{
    word j;

    memmove(values + at + n, values + at, (nvalues - at) * sizeof(dword));
    for (j = 0; j < n; j++)
	values[at + j] = base + j;
    nvalues += n;
}

static void HADelete(dword at, word n)
{
    memmove(values + at, values + at + n,
	    (nvalues - at - n) * sizeof(dword));
    nvalues -= n;
}

static Boolean SumCb(void *element, void *data)
{
    *(dword *) data += *(byte *) element;
    return FALSE;
}

static Boolean StopCb(void *element, void *data)
{
    (void) element;
    return ++*(dword *) data == 5;
}

int main(int argc, char **argv)
{
    VMFileHandle vm, vm2, f;
    VMBlockHandle b1, b2, b3, b4, b5, more[20], c1, c2, c3, t;
    VMChain copy;
    VMInfoStruct info;
    MemHandle mh, d;
    char *slash;
    dword base, size, size32;
    word i, blocks, db;
    uint64_t before;
    byte *p;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);	/* all of it, should DOS crash */
    kernelFatalHook = Catch;
    kernelCurrentGeode = 0x2340;
    strncpy(top, argv[0], sizeof(top) - 8);
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash)
	strcpy(slash + 1, "GVTOP");
    else
	strcpy(top, "GVTOP");
    Cleanup();
    CHECK(ghal_fs_mkdir(top) == GHAL_OK);
    FileSetTopHostPath(top);
    KernelThreadInit(PRIORITY_STANDARD);
    base = HandleCountUsed();

    /* a new file */
    vm = VMOpen("A.VM", 0, VMO_CREATE_ONLY, 0);
    CHECK(vm != 0 && ThreadGetError() == VM_CREATE_OK);
    CHECK(VMOpen("A.VM", 0, VMO_CREATE_ONLY, 0) == 0 &&
	  ThreadGetError() == VM_FILE_EXISTS);
    b1 = VMAlloc(vm, 100, 0x1111);
    b2 = VMAlloc(vm, 2000, 0x2222);
    b3 = VMAlloc(vm, 40, 0x3333);
    CHECK(b1 && b2 && b3 && b1 != b2 && b2 != b3);
    CHECK((b1 - 32) % 12 == 0);		/* entries of the DOS16 table */
    Fill(vm, b1, 100, 1);
    Fill(vm, b2, 2000, 2);
    Fill(vm, b3, 40, 3);
    VMSetMapBlock(vm, b2);
    VMLock(vm, b1, &mh);
    VMUnlock(mh);
    CHECK(MemOwner(mh) == 0x2340);	/* the file's owner */
    CHECK(VMMemBlockToVMBlock(mh, &f) == b1 && f == vm);
    CHECK(VMVMBlockToMemBlock(vm, b1) == mh);
    CHECK(VMGetDirtyState(vm) != 0);
    for (i = 0; i < 20; i++) {		/* the table grows */
	more[i] = VMAlloc(vm, (word) (16 * (i + 1)), (word) (0x100 + i));
	Fill(vm, more[i], (word) (16 * (i + 1)), (byte) i);
    }
    CHECK(VMUpdate(vm) == 0);
    CHECK(VMGetDirtyState(vm) == 0);
    CHECK(VMUpdate(vm) == VM_UPDATE_NOTHING_DIRTY);
    /* open again: the same handle */
    CHECK(VMOpen("A.VM", 0, VMO_OPEN, 0) == vm &&
	  ThreadGetError() == VM_OPEN_OK_READ_WRITE_NOT_SHARED);
    CHECK(VMClose(vm, FALSE) == 0);
    CHECK(VMClose(vm, FALSE) == 0);
    /* huge arrays */
    {
	VMBlockHandle ha, hv;
	VMChain hcopy;
	dword buf[3000], r, i, sum, n;
	word size;
	void *p, *q;
	byte vbuf[300];
	VMInfoStruct hi;

	vm = VMOpen("HA.VM", 0, VMO_CREATE_TRUNCATE, 0);
	CHECK(vm != 0);
	ha = HugeArrayCreate(vm, 4, 0);
	CHECK(HugeArrayGetCount(vm, ha) == 0 &&
	      HugeArrayLock(vm, ha, 0, &p, &size) == 0 && size == 4);
	for (i = 0; i < 3000; i++)
	    buf[i] = i * 7;
	nvalues = 0;
	CHECK(HugeArrayAppend(vm, ha, 3000, buf) == 0);
	HAInsert(0, 0, 0);
	for (i = 0; i < 3000; i++)
	    values[i] = i * 7;
	nvalues = 3000;
	CHECK(HASame(vm, ha));
	/* the counts of a lock: before and with it, from it on */
	r = HugeArrayLock(vm, ha, 1500, &p, &size);
	CHECK(HAL_COUNT(r) >= 1 && HAL_PREV(r) >= 1 &&
	      HAL_COUNT(r) + HAL_PREV(r) - 1 <= 1001);
	HugeArrayUnlock(p);
	HugeArrayInsert(vm, ha, 10, 1500, NULL);
	HAInsert(1500, 10, 0);
	memset(values + 1500, 0, 10 * sizeof(dword));
	CHECK(HASame(vm, ha));
	HugeArrayReplace(vm, ha, 2, 1500, buf + 1);	/* 7, 14 */
	values[1500] = 7;
	values[1501] = 14;
	HugeArrayDelete(vm, ha, 500, 100);
	HADelete(100, 500);
	HugeArrayInsert(vm, ha, 1, 0, buf + 2);
	HAInsert(0, 1, 14);
	CHECK(HugeArrayAppend(vm, ha, 2, buf + 3) == nvalues);
	HAInsert(nvalues, 2, 21);
	values[nvalues - 1] = 28;
	CHECK(HASame(vm, ha));
	/* Next over all, Prev back */
	r = HugeArrayLock(vm, ha, 0, &p, &size);
	for (i = 1, n = 1; HugeArrayNext(&p, &size); i++)
	    n += *(dword *) p == values[i];
	CHECK(i == nvalues && n == nvalues && *(dword *) p == values[i - 1]);
	for (i = nvalues - 1, n = 0; HugeArrayPrev(&p, &q, &size); i--)
	    n += *(dword *) p == values[i - 1];
	CHECK(i == 0 && n == nvalues - 1 && p == q);
	HugeArrayUnlock(p);
	/* Expand and Contract at a pointer */
	HugeArrayLock(vm, ha, 700, &p, &size);
	CHECK(HugeArrayExpand(&p, 3, buf + 10) >= 3 && *(dword *) p == 70);
	HAInsert(700, 3, 70);
	values[701] = 77;
	values[702] = 84;
	CHECK(HugeArrayContract(&p, 2) >= 1 && *(dword *) p == 84);
	HugeArrayUnlock(p);
	HADelete(700, 2);
	CHECK(HASame(vm, ha));
	/* as a chain: copied (its own handles), compared, freed */
	hcopy = VMCopyVMChain(vm, VMCHAIN_MAKE_FROM_VM_BLOCK(ha), vm);
	CHECK(VMCompareVMChains(vm, VMCHAIN_MAKE_FROM_VM_BLOCK(ha), vm,
				hcopy));
	CHECK(HASame(vm, VMCHAIN_GET_VM_BLOCK(hcopy)));
	HugeArrayReplace(vm, VMCHAIN_GET_VM_BLOCK(hcopy), 1, 5, buf);
	CHECK(!VMCompareVMChains(vm, VMCHAIN_MAKE_FROM_VM_BLOCK(ha), vm,
				 hcopy));
	HASame(vm, ha);
	/* variable size elements */
	hv = HugeArrayCreate(vm, 0, sizeof(HugeArrayDirectory) + 8);
	HugeArrayLockDir(vm, hv, &p);
	memcpy((byte *) p + sizeof(HugeArrayDirectory), "USERDATA", 8);
	HugeArrayDirty(p);
	HugeArrayUnlockDir(p);
	for (i = 0; i < 200; i++) {
	    memset(vbuf, (int) i, sizeof(vbuf));
	    HugeArrayAppend(vm, hv, (word) (i + 1), vbuf);
	}
	CHECK(HugeArrayGetCount(vm, hv) == 200);
	r = HugeArrayLock(vm, hv, 150, &p, &size);
	CHECK(HAL_COUNT(r) && size == 151 && ((byte *) p)[150] == 150);
	HugeArrayUnlock(p);
	memset(vbuf, 0xee, sizeof(vbuf));
	HugeArrayInsert(vm, hv, 300, 100, vbuf);
	HugeArrayResize(vm, hv, 101, 10);	/* was element 100: 101 */
	HugeArrayReplace(vm, hv, 3, 0, "abc");
	r = HugeArrayLock(vm, hv, 100, &p, &size);
	CHECK(size == 300 && ((byte *) p)[299] == 0xee);
	HugeArrayUnlock(p);
	r = HugeArrayLock(vm, hv, 101, &p, &size);
	CHECK(size == 10 && ((byte *) p)[9] == 100);
	HugeArrayUnlock(p);
	r = HugeArrayLock(vm, hv, 0, &p, &size);
	CHECK(size == 3 && !memcmp(p, "abc", 3));
	HugeArrayUnlock(p);
	HugeArrayResize(vm, hv, 2, 7000);	/* a block of its own */
	r = HugeArrayLock(vm, hv, 2, &p, &size);
	CHECK(size == 7000 && ((byte *) p)[2] == 2 && ((byte *) p)[6999] == 0 &&
	      HAL_COUNT(r) == 1 && HAL_PREV(r) == 1);
	HugeArrayUnlock(p);
	sum = 0;
	CHECK(!HugeArrayEnum(vm, hv, SumCb, 1, 4, &sum) && sum == 1 + 2 + 3 +
	      4);
	sum = 0;
	CHECK(HugeArrayEnum(vm, hv, StopCb, 0, 0xffffffffUL, &sum) &&
	      sum == 5);
	HugeArrayCompressBlocks(vm, hv);
	CHECK(VMInfo(vm, ha, &hi) && hi.userId == SVMID_HA_DIR_ID);
	CHECK(VMClose(vm, FALSE) == 0);

	CHECK(HAHeaderAt26("HA.VM", hv));
	/* read again (the DOS16 layout) */
	vm = VMOpen("HA.VM", 0, VMO_OPEN, 0);
	CHECK(vm != 0 && HASame(vm, ha));
	CHECK(HugeArrayGetCount(vm, hv) == 201);
	r = HugeArrayLock(vm, hv, 199, &p, &size);
	CHECK(size == 199 && ((byte *) p)[0] == 198);
	HugeArrayUnlock(p);
	HugeArrayLockDir(vm, hv, &p);
	CHECK(!memcmp((byte *) p + sizeof(HugeArrayDirectory), "USERDATA", 8));
	HugeArrayUnlockDir(p);
	HugeArrayAppend(vm, ha, 1, buf + 4);
	HAInsert(nvalues, 1, 28);
	CHECK(HASame(vm, ha));
	VMFreeVMChain(vm, hcopy);
	CHECK(VMClose(vm, FALSE) == 0);
	vm = VMOpen("HA.VM", 0, VMO_OPEN, 0);
	CHECK(HASame(vm, ha));
	HugeArrayDestroy(vm, hv);
	blocks = 0;
	CHECK(VMInfoVMChain(vm, VMCHAIN_MAKE_FROM_VM_BLOCK(ha), &size32,
			    &blocks, &db) && blocks > 3);
	CHECK(VMClose(vm, FALSE) == 0);
    }

    CHECK(HandleCountUsed() == base);

    /* opened again: everything is back */
    vm = VMOpen("A.VM", 0, VMO_OPEN, 0);
    CHECK(vm && ThreadGetError() == VM_OPEN_OK_READ_WRITE_NOT_SHARED);
    CHECK(VMGetMapBlock(vm) == b2);
    CHECK(Same(vm, b1, 100, 1) && Same(vm, b2, 2000, 2) &&
	  Same(vm, b3, 40, 3));
    for (i = 0; i < 20; i++)
	CHECK(Same(vm, more[i], (word) (16 * (i + 1)), (byte) i));
    CHECK(VMInfo(vm, b1, &info) && info.size == 112 &&	/* 16-byte units */
	  info.userId == 0x1111);
    CHECK(VMInfo(vm, b2, NULL) && !VMInfo(vm, 0x2a, NULL));
    CHECK(VMFind(vm, 0, 0x2222) == b2 && VMFind(vm, b2, 0x2222) == 0);
    VMModifyUserID(vm, b3, 0x3334);
    CHECK(VMFind(vm, 0, 0x3334) == b3);
    /* grow, shrink, free; the space is used again */
    Resize(vm, b1, 3000, 4);
    Resize(vm, b2, 500, 5);
    VMFree(vm, b3);
    for (i = 0; i < 20; i += 2)
	VMFree(vm, more[i]);
    CHECK(!VMInfo(vm, b3, NULL));
    CHECK(VMUpdate(vm) == 0);
    before = SizeOf("A.VM");
    b4 = VMAlloc(vm, 32, 0x4444);
    Fill(vm, b4, 32, 6);
    CHECK(VMUpdate(vm) == 0);
    CHECK(SizeOf("A.VM") == before);
    /* attach and detach */
    mh = MemAlloc(64, HF_SWAPABLE, 0);
    p = MemLock(mh);
    memset(p, 0x77, 64);
    MemUnlock(mh);
    b5 = VMAttach(vm, 0, mh);
    CHECK(b5 && MemOwner(mh) == 0x2340 &&
	  VMMemBlockToVMBlock(mh, NULL) == b5);
    d = VMDetach(vm, b5, 0x2350);
    CHECK(d == mh && MemOwner(d) == 0x2350);
    MemFree(d);
    p = VMLock(vm, b5, &mh);		/* the file kept it */
    CHECK(p && p[0] == 0x77 && p[63] == 0x77);
    VMUnlock(mh);
    caught = KFE_NONE;
    if (setjmp(catcher) == 0)
	VMLock(vm, 0x2a, &mh);
    CHECK(caught == VM_BAD_BLOCK_HANDLE);
    VMSetAttributes(vm, VMA_SYNC_UPDATE, 0);
    CHECK(VMGetAttributes(vm) == VMA_SYNC_UPDATE);

    /* chains: c1 -> c2 -> c3, and a tree t with c1 and nothing */
    c3 = LinkBlock(vm, 50, 0, 7);
    c2 = LinkBlock(vm, 60, c3, 8);
    c1 = LinkBlock(vm, 70, c2, 9);
    t = VMAlloc(vm, 32, 0x7777);
    p = VMLock(vm, t, &mh);
    memset(p, 0x11, 32);
    p[0] = 0xff; p[1] = 0xff;		/* VM_CHAIN_TREE */
    p[2] = 8; p[3] = 0;			/* offset */
    p[4] = 2; p[5] = 0;			/* count */
    memset(p + 8, 0, 8);
    p[10] = (byte) c1; p[11] = (byte) (c1 >> 8);	/* VMChain c1:0 */
    VMDirty(mh);
    VMUnlock(mh);
    vm2 = VMOpen("B.VM", 0, VMO_CREATE_TRUNCATE, 0);
    CHECK(vm2 && ThreadGetError() == VM_CREATE_OK);
    copy = VMCopyVMChain(vm, VMCHAIN_MAKE_FROM_VM_BLOCK(t), vm2);
    CHECK(copy != 0);
    CHECK(VMCompareVMChains(vm, VMCHAIN_MAKE_FROM_VM_BLOCK(t), vm2, copy));
    CHECK(VMInfoVMChain(vm2, copy, &size, &blocks, &db) && blocks == 4 &&
	  size == 32 + 70 + 60 + 50 && db == 0);
    {
	/* the second block of the copied list */
	VMBlockHandle nt = VMCHAIN_GET_VM_BLOCK(copy), n1, n2;
	p = VMLock(vm2, nt, &mh);
	n1 = (VMBlockHandle) (p[10] | (p[11] << 8));
	VMUnlock(mh);
	p = VMLock(vm2, n1, &mh);
	n2 = (VMBlockHandle) (p[0] | (p[1] << 8));
	VMUnlock(mh);
	CHECK(n1 && n2);
	p = VMLock(vm2, n2, &mh);
	p[20] ^= 1;
	VMDirty(mh);
	VMUnlock(mh);
	CHECK(!VMCompareVMChains(vm, VMCHAIN_MAKE_FROM_VM_BLOCK(t), vm2,
				 copy));
    }
    CHECK(VMClose(vm2, FALSE) == 0);	/* B.VM keeps the copy */
    VMFreeVMChain(vm, VMCHAIN_MAKE_FROM_VM_BLOCK(t));
    CHECK(!VMInfo(vm, t, NULL) && !VMInfo(vm, c1, NULL) &&
	  !VMInfo(vm, c2, NULL) && !VMInfo(vm, c3, NULL));
    CHECK(VMClose(vm, FALSE) == 0);

    /* errors, read-only, temporary files */
    CHECK(VMOpen("NONE.VM", 0, VMO_OPEN, 0) == 0 &&
	  ThreadGetError() == VM_FILE_NOT_FOUND);
    f = FileCreate("PLAIN.TXT", FILE_CREATE_TRUNCATE | FILE_ACCESS_RW, 0);
    CHECK(f && FileWrite(f, "not a VM file, no no no", 23, FALSE) == 23);
    FileClose(f, FALSE);
    CHECK(VMOpen("PLAIN.TXT", 0, VMO_OPEN, 0) == 0 &&
	  ThreadGetError() == VM_OPEN_INVALID_VM_FILE);
    CHECK(FileSetAttributes("A.VM", FA_RDONLY) == 0);
    vm = VMOpen("A.VM", 0, VMO_OPEN, 0);
    CHECK(vm && ThreadGetError() == VM_OPEN_OK_READ_ONLY);
    CHECK(Same(vm, b1, 3000, 4) && Same(vm, b4, 32, 6));
    CHECK(VMClose(vm, FALSE) == 0);
    CHECK(FileSetAttributes("A.VM", 0) == 0);
    CHECK(FileCreateDir("TMP") == 0);
    {
	/* the directory in, the file's name out (DOS16: the buffer must
	 * have room for it) */
	char tmp[32] = "TMP";

	vm = VMOpen(tmp, 0, VMO_TEMP_FILE, 0);
	CHECK(vm && ThreadGetError() == VM_CREATE_OK);
	CHECK(!strncmp(tmp, "TMP\\VM", 6) && strlen(tmp) == 14 &&
	      !strcmp(tmp + 10, ".TMP"));
	CHECK(VMClose(vm, FALSE) == 0);
	CHECK(FileDelete(tmp) == 0);
    }

    /* a file of the 16-bit tools */
    {
	ghal_file *hf;
	size_t done;
	word n = 0, uids = 0;
	dword sizes = 0, csum = 0, j;
	VMBlockHandle h;

	CHECK(ghal_fs_open(H("REF.VM"), GHAL_FS_WRITE | GHAL_FS_CREATE,
			   &hf) == GHAL_OK);
	CHECK(ghal_fs_write(hf, vmReference, sizeof(vmReference), &done) ==
	      GHAL_OK && done == sizeof(vmReference));
	ghal_fs_close(hf);
	vm = VMOpen("REF.VM", VMAF_FORCE_READ_ONLY, VMO_OPEN, 0);
	CHECK(vm && ThreadGetError() == VM_OPEN_OK_READ_ONLY);
	CHECK(VMGetMapBlock(vm) == VMREF_MAP_BLOCK);
	for (h = 0x2c; h < 0xfff0; h += 12)
	    if (VMInfo(vm, h, &info)) {
		n++;
		uids = (word) (uids + info.userId);
		sizes += info.size;
		p = VMLock(vm, h, &mh);
		for (j = 0; j < info.size; j++)
		    csum = csum * 31 + p[j];
		VMUnlock(mh);
	    }
	CHECK(n == VMREF_NUM_BLOCKS && uids == VMREF_UID_SUM &&
	      sizes == VMREF_SIZE_SUM && csum == VMREF_CHECKSUM);
	CHECK(VMClose(vm, FALSE) == 0);
	/* and changed by the kernel */
	vm = VMOpen("REF.VM", 0, VMO_OPEN, 0);
	CHECK(vm && ThreadGetError() == VM_OPEN_OK_READ_WRITE_NOT_SHARED);
	b1 = VMAlloc(vm, 300, 0x9999);
	Fill(vm, b1, 300, 10);
	for (h = 0x2c; h == VMREF_MAP_BLOCK || !VMInfo(vm, h, NULL); h += 12)
	    ;
	VMFree(vm, h);
	CHECK(VMClose(vm, FALSE) == 0);
	vm = VMOpen("REF.VM", 0, VMO_OPEN, 0);
	if (!vm)
	    printf("REF.VM: %u\n", ThreadGetError());
	CHECK(vm && Same(vm, b1, 300, 10) &&
	      VMGetMapBlock(vm) == VMREF_MAP_BLOCK);
	CHECK(VMClose(vm, FALSE) == 0);
    }

    /* LMem blocks: in the DOS16 layout in the file, back on reading */
    {
	static const word sizes[5] = { 1, 5, 13, 100, 2000 };
	ChunkHandle ch[5], extra;
	LMemBlockHeader *lh;
	word offset;

	vm = VMOpen("LM.VM", 0, VMO_CREATE_TRUNCATE, 0);
	CHECK(vm != 0);
	VMSetReloc(vm, Reloc);
	b1 = VMAllocLMem(vm, LMEM_TYPE_GENERAL, sizeof(LMemBlockHeader) + 8);
	lh = VMLock(vm, b1, &mh);
	CHECK(lh && (lh->LMBH_flags & LMF_IS_VM) &&
	      (MemGetInfo(mh, MGIT_FLAGS_AND_LOCK_COUNT) & HF_LMEM));
	memcpy(lh + 1, "HEADER!!", 8);
	for (i = 0; i < 5; i++) {
	    ch[i] = LMemAlloc(mh, sizes[i]);
	    FillChunk(ConstructOptr(mh, ch[i]), sizes[i], (byte) i);
	}
	extra = LMemAlloc(mh, 0);		/* an empty chunk */
	LMemFree(ConstructOptr(mh, ch[1]));	/* a free handle */
	CHECK(!LMemReAlloc(ConstructOptr(mh, ch[2]), 300));
	FillChunk(ConstructOptr(mh, ch[2]), 300, 2);
	lh = MemDeref(mh);
	offset = lh->LMBH_offset;
	VMDirty(mh);
	VMUnlock(mh);
	b2 = VMAlloc(vm, 64, 0x55);		/* no LMem: the routine's */
	Fill(vm, b2, 64, 3);
	CHECK(VMClose(vm, FALSE) == 0);
	CHECK(relocCalls[VMRT_UNRELOCATE_BEFORE_WRITE] == 2 &&
	      relocCalls[VMRT_RELOCATE_AFTER_WRITE] == 2 &&
	      relocCalls[VMRT_RELOCATE_AFTER_READ] == 0);

	vm = VMOpen("LM.VM", 0, VMO_OPEN, 0);
	CHECK(vm != 0);
	VMSetReloc(vm, Reloc);
	lh = VMLock(vm, b1, &mh);
	CHECK(relocCalls[VMRT_RELOCATE_AFTER_READ] == 1 && relocBlock == b1);
	CHECK(lh && lh->LMBH_handle == mh && lh->LMBH_offset == offset &&
	      (lh->LMBH_flags & LMF_IS_VM) &&
	      lh->LMBH_lmemType == LMEM_TYPE_GENERAL &&
	      (MemGetInfo(mh, MGIT_FLAGS_AND_LOCK_COUNT) & HF_LMEM) &&
	      !memcmp(lh + 1, "HEADER!!", 8));
	CHECK(SameChunk(ConstructOptr(mh, ch[0]), 1, 0) &&
	      SameChunk(ConstructOptr(mh, ch[2]), 300, 2) &&
	      SameChunk(ConstructOptr(mh, ch[3]), 100, 3) &&
	      SameChunk(ConstructOptr(mh, ch[4]), 2000, 4));
	CHECK(*(word *) ((byte *) lh + ch[1]) == 0 &&
	      *(word *) ((byte *) lh + extra) == 0xffff);
	/* it is an LMem heap again */
	ch[1] = LMemAlloc(mh, 40);
	FillChunk(ConstructOptr(mh, ch[1]), 40, 9);
	CHECK(!LMemReAlloc(ConstructOptr(mh, ch[0]), 500));
	FillChunk(ConstructOptr(mh, ch[0]), 500, 8);
	CHECK(SameChunk(ConstructOptr(mh, ch[1]), 40, 9) &&
	      SameChunk(ConstructOptr(mh, ch[4]), 2000, 4));
	VMDirty(mh);
	VMUnlock(mh);
	CHECK(Same(vm, b2, 64, 3) && relocBlock == b2 &&
	      relocCalls[VMRT_RELOCATE_AFTER_READ] == 2);
	CHECK(VMClose(vm, FALSE) == 0);

	vm = VMOpen("LM.VM", 0, VMO_OPEN, 0);
	lh = VMLock(vm, b1, &mh);
	CHECK(lh && SameChunk(ConstructOptr(mh, ch[0]), 500, 8) &&
	      SameChunk(ConstructOptr(mh, ch[1]), 40, 9) &&
	      SameChunk(ConstructOptr(mh, ch[2]), 300, 2));
	VMUnlock(mh);
	/* detached, it is no VM block any more */
	d = VMDetach(vm, b1, 0);
	lh = MemLock(d);
	CHECK(!(lh->LMBH_flags & LMF_IS_VM) &&
	      SameChunk(ConstructOptr(d, ch[3]), 100, 3));
	MemUnlock(d);
	MemFree(d);
	CHECK(VMClose(vm, FALSE) == 0);
	/* written again, for vmcheck.py */
	vm = VMOpen("LM.VM", 0, VMO_OPEN, 0);
	b1 = VMAllocLMem(vm, LMEM_TYPE_GENERAL, 0);
	lh = VMLock(vm, b1, &mh);
	ch[0] = LMemAlloc(mh, 77);
	VMUnlock(mh);
	CHECK(VMClose(vm, FALSE) == 0);
    }

    CHECK(HandleCountUsed() == base);
    printf(fails ? "FAILED\n" : "ok\n");
    return fails != 0;
}
