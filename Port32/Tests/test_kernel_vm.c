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
 * (vm_reference.h).  A.VM, B.VM and REF.VM stay for vmcheck.py (ctest
 * kernel.vm.format).
 */
#include "kernelC.h"
#include "ghal.h"
#include "hostfs.h"
#include <vm.h>
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
static uint64_t SizeOf(const char *rel)
{
    dword size = 0;

    FileGetPathExtAttributes(rel, FEA_SIZE, &size, sizeof(size));
    return size;
}

int main(int argc, char **argv)
{
    VMFileHandle vm, vm2, f;
    VMBlockHandle b1, b2, b3, b4, b5, more[20], c1, c2, c3, t;
    VMChain copy;
    VMInfoStruct info;
    MemHandle mh, d;
    char *slash;
    dword base, size;
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
    vm = VMOpen("TMP", 0, VMO_TEMP_FILE, 0);
    CHECK(vm && ThreadGetError() == VM_CREATE_OK);
    CHECK(VMClose(vm, FALSE) == 0);

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

    CHECK(HandleCountUsed() == base);
    printf(fails ? "FAILED\n" : "ok\n");
    return fails != 0;
}
