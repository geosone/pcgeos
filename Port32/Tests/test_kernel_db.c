/*
 * test_kernel_db.c -- the database manager of the C kernel
 * (Library/Kernel/DBase) on VM files, on every host.
 *
 * The GEOS top directory is GDTOP next to the program.  Checked: the DB
 * map made the first time, groups, items allocated, locked, dirtied,
 * resized (in their block, and into another one when it would grow too
 * large), with bytes inserted and deleted, freed (an item block goes with
 * its last item), copied in a file and to another one, DBInfo of good
 * and bad items, the map item, ungrouped items (a new ungroup group when
 * one is full, and one with room again reused), freeing a group, and all
 * of it written and read again (the chunk handles of the file), DB items
 * as VM chains (copied, compared, counted, freed), and a
 * file whose DB map is broken refused.  Given files of DOS16 GEOS (as
 * arguments, with their GEOS file header), each is copied to the top,
 * its map item read and compared with the bytes of the file, changed,
 * and read again.  DB.VM, DB2.VM and the copies (D16_<n>.VM) stay for
 * vmcheck.py (ctest kernel.db.format).
 */
#include "kernelC.h"
#include "ghal.h"
#include "hostfs.h"
#include <vm.h>
#include <dbase.h>

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

static void Fill(VMFileHandle vm, DBGroupAndItem id, word size, byte seed)
{
    byte *p = DBLockUngrouped(vm, id);
    word i;

    for (i = 0; i < size; i++)
	p[i] = (byte) (seed + i * 7);
    DBDirty(p);
    DBUnlock(p);
}

static Boolean Same(VMFileHandle vm, DBGroupAndItem id, word size, byte seed)
{
    byte *p = DBLockUngrouped(vm, id);
    word i, n;
    Boolean ok = DBInfoUngrouped(vm, id, &n) && n == size;

    for (i = 0; ok && i < size; i++)
	ok = p[i] == (byte) (seed + i * 7);
    DBUnlock(p);
    return ok;
}

static Boolean Exists(VMFileHandle vm, VMBlockHandle b)
{
    VMInfoStruct info;

    return VMInfo(vm, b, &info);
}

static VMBlockHandle BlockOf(VMFileHandle vm, DBGroupAndItem id)
{
    optr o;
    VMFileHandle f;
    void *p = DBLockGetRefUngrouped(vm, id, &o);
    VMBlockHandle b = VMMemBlockToVMBlock(OptrToHandle(o), &f);

    CHECK(LMemDeref(o) == p);
    DBUnlock(p);
    return b;
}

static word Get16(const byte *p)
{
    return (word) (p[0] | (p[1] << 8));
}

/* the map item of a DOS16 VM image, read by hand: its data and size */
static const byte *MapItemOf(const byte *img, dword len, word *size)
{
    dword hpos = Get16(img + 4) | ((dword) Get16(img + 6) << 16);
    const byte *hdr = img + hpos, *map, *grp, *ib;
#define BLK(h) (img + (Get16(hdr + (h) + 8) | \
		       ((dword) Get16(hdr + (h) + 10) << 16)))
    word item, info, chunk, data;

    if (hpos + 32 > len || !Get16(hdr + 30))
	return NULL;
    map = BLK(Get16(hdr + 30));
    if (!Get16(map + 4))
	return NULL;
    grp = BLK(Get16(map + 4));
    item = Get16(map + 6);
    info = Get16(grp + item);
    chunk = Get16(grp + item + 2);
    ib = BLK(Get16(grp + info + 2));
    data = Get16(ib + chunk);
    *size = (word) (Get16(ib + data - 2) - 2);
    return ib + data;
#undef BLK
}

/* a file of DOS16 GEOS: copied, its map item checked, changed */
static void Dos16File(const char *path, int n)
{
    static byte img[200000];
    char name[20];
    FILE *fp = fopen(path, "rb");
    dword len = fp ? (dword) fread(img, 1, sizeof(img), fp) : 0;
    FileHandle fh;
    VMFileHandle vm;
    const byte *ref;
    word refSize, size;
    DBGroupAndItem map, extra;
    byte *p, saved[4096];

    if (fp)
	fclose(fp);
    CHECK(len > 256 && len < sizeof(img) && img[0] == 0xc7);
    if (len <= 256 || len >= sizeof(img))
	return;
    ref = MapItemOf(img + 256, len - 256, &refSize);
    CHECK(ref != NULL && refSize > 0 && refSize <= sizeof(saved));
    if (!ref || refSize > sizeof(saved))
	return;
    memcpy(saved, ref, refSize);
    sprintf(name, "D16_%d.VM", n);
    fh = FileCreate(name, FILE_CREATE_ONLY | FCF_NATIVE | FILE_ACCESS_RW, 0);
    CHECK(fh != 0);
    FileWrite(fh, img + 256, (word) (len - 256), FALSE);
    FileClose(fh, FALSE);
    vm = VMOpen(name, 0, VMO_OPEN, 0);
    CHECK(vm != 0);
    if (!vm)
	return;
    map = DBGetMap(vm);
    CHECK(map != 0 && DBInfoUngrouped(vm, map, &size) && size == refSize);
    p = DBLockMap(vm);
    CHECK(!memcmp(p, saved, refSize));
    DBUnlock(p);
    /* changed: the map item grows, an ungrouped item is added */
    DBInsertAtUngrouped(vm, map, refSize, 100);
    extra = DBAllocUngrouped(vm, 77);
    Fill(vm, extra, 77, 8);
    CHECK(VMClose(vm, FALSE) == 0);
    vm = VMOpen(name, 0, VMO_OPEN, 0);
    CHECK(vm != 0);
    if (!vm)
	return;
    CHECK(DBInfoUngrouped(vm, map, &size) && size == refSize + 100);
    p = DBLockUngrouped(vm, map);
    CHECK(!memcmp(p, saved, refSize) && !p[refSize] && !p[refSize + 99]);
    DBUnlock(p);
    CHECK(Same(vm, extra, 77, 8));
    CHECK(VMClose(vm, FALSE) == 0);
}

#define NUNG	4200

static DBGroupAndItem ung[NUNG];
static DBGroupAndItem items[40];

int main(int argc, char **argv)
{
    VMFileHandle vm, vm2;
    DBGroup g, g2, ug1, ug2;
    DBGroupAndItem a, b, c, big, copy, copy2, inG2, map;
    VMBlockHandle blk, blk2, bad;
    word i, n;
    byte *p;
    char *slash;

    setvbuf(stdout, NULL, _IONBF, 0);
    kernelFatalHook = Catch;
    kernelCurrentGeode = 0x2340;
    strncpy(top, argv[0], sizeof(top) - 8);
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash)
	strcpy(slash + 1, "GDTOP");
    else
	strcpy(top, "GDTOP");
    RemoveTree(top);
    CHECK(ghal_fs_mkdir(top) == GHAL_OK);
    FileSetTopHostPath(top);
    KernelThreadInit(PRIORITY_STANDARD);
    if (setjmp(catcher)) {
	printf("FAIL: fatal error %d\n", (int) caught);
	return 1;
    }

    vm = VMOpen("DB.VM", 0, VMO_CREATE_ONLY, 0);
    CHECK(vm != 0);
    /* --- the map, a group, items ------------------------------------- */
    CHECK(DBGetMap(vm) == 0 && VMGetDBMap(vm) != 0);
    g = DBGroupAlloc(vm);
    a = DBCombineGroupAndItem(g, DBAlloc(vm, g, 10));
    b = DBCombineGroupAndItem(g, DBAlloc(vm, g, 300));
    c = DBCombineGroupAndItem(g, DBAlloc(vm, g, 0));
    CHECK(DBGroupFromGroupAndItem(a) == g && DBItemFromGroupAndItem(a) >= 16);
    CHECK(DBItemFromGroupAndItem(a) != DBItemFromGroupAndItem(b));
    Fill(vm, a, 10, 1);
    Fill(vm, b, 300, 2);
    CHECK(Same(vm, a, 10, 1) && Same(vm, b, 300, 2));
    CHECK(DBInfoUngrouped(vm, c, &n) && n == 0);
    CHECK(BlockOf(vm, a) == BlockOf(vm, b));
    /* bad ones */
    CHECK(!DBInfoUngrouped(vm, DBCombineGroupAndItem(VMGetDBMap(vm), 16),
			   &n));
    CHECK(!DBInfoUngrouped(vm, DBCombineGroupAndItem(g, 0xfff0), &n));
    /* --- resizing, inserting, deleting ------------------------------- */
    DBReAllocUngrouped(vm, a, 20);
    p = DBLockUngrouped(vm, a);
    for (i = 0; i < 10; i++)
	CHECK(p[i] == (byte) (1 + i * 7));
    DBUnlock(p);
    Fill(vm, a, 20, 3);
    DBReAllocUngrouped(vm, a, 5);
    CHECK(Same(vm, a, 5, 3));
    DBInsertAtUngrouped(vm, a, 2, 3);
    p = DBLockUngrouped(vm, a);
    CHECK(DBInfoUngrouped(vm, a, &n) && n == 8);
    CHECK(p[0] == 3 && p[1] == 10 && !p[2] && !p[3] && !p[4] &&
	  p[5] == 17 && p[6] == 24 && p[7] == 31);
    DBUnlock(p);
    DBDeleteAtUngrouped(vm, a, 2, 3);
    CHECK(Same(vm, a, 5, 3));
    /* a block that would grow too large: the item moves to another
     * one, its data with it */
    for (i = 0; i < 30; i++) {
	items[i] = DBCombineGroupAndItem(g, DBAlloc(vm, g, 350));
	Fill(vm, items[i], 350, (byte) i);
    }
    blk = BlockOf(vm, b);
    for (i = 0; i < 30 && BlockOf(vm, items[i]) == blk; i++)
	;
    CHECK(i > 10 && i < 30);		/* the first block filled up */
    DBReAllocUngrouped(vm, b, 2000);
    CHECK(BlockOf(vm, b) != blk);
    p = DBLockUngrouped(vm, b);
    for (i = 0; i < 300 && p[i] == (byte) (2 + i * 7); i++)
	;
    CHECK(i == 300);
    DBUnlock(p);
    Fill(vm, b, 2000, 4);
    /* an item alone in its block grows there */
    big = DBCombineGroupAndItem(g, DBAlloc(vm, g, 7000));
    blk2 = BlockOf(vm, big);
    DBReAllocUngrouped(vm, big, 9000);
    CHECK(BlockOf(vm, big) == blk2);
    Fill(vm, big, 9000, 5);
    /* the last item of a block takes it along */
    DBFreeUngrouped(vm, big);
    CHECK(!Exists(vm, blk2));
    /* --- the map item ------------------------------------------------ */
    DBSetMap(vm, g, DBItemFromGroupAndItem(a));
    CHECK(DBGetMap(vm) == a);
    p = DBLockMap(vm);
    CHECK(p[0] == 3);
    DBUnlock(p);
    /* --- copies ------------------------------------------------------ */
    g2 = DBGroupAlloc(vm);
    inG2 = DBCombineGroupAndItem(g2, DBCopyDBItem(vm, g,
						  DBItemFromGroupAndItem(b),
						  vm, g2));
    CHECK(Same(vm, inG2, 2000, 4));
    copy2 = DBCopyDBItemUngrouped(vm, items[3], vm);
    CHECK(Same(vm, copy2, 350, 3));
    vm2 = VMOpen("DB2.VM", 0, VMO_CREATE_ONLY, 0);
    copy = DBCopyDBItemUngrouped(vm, b, vm2);
    CHECK(Same(vm2, copy, 2000, 4));
    CHECK(VMClose(vm2, FALSE) == 0);
    /* --- ungrouped items --------------------------------------------- */
    for (i = 0; i < NUNG; i++)
	ung[i] = DBAllocUngrouped(vm, (word) (1 + i % 5));
    ug1 = DBGroupFromGroupAndItem(ung[0]);
    CHECK(DBGroupFromGroupAndItem(copy2) == ug1);
    for (i = 1; i < NUNG && DBGroupFromGroupAndItem(ung[i]) == ug1; i++)
	;
    CHECK(i > 1500 && i < 2100);	/* the group block reached 8K */
    ug2 = DBGroupFromGroupAndItem(ung[i]);
    CHECK(ug2 != ug1 && ug2 != g && ug2 != g2);
    for (n = i; n < NUNG && DBGroupFromGroupAndItem(ung[n]) == ug2; n++)
	;
    CHECK(n < NUNG);			/* and the second one too */
    /* the first one with room again is taken when the third is full */
    for (i = 0; i < 1000; i++)
	DBFreeUngrouped(vm, ung[i + 1]);
    for (i = 0; i < 2100; i++) {
	a = DBAllocUngrouped(vm, 4);
	if (DBGroupFromGroupAndItem(a) == ug1)
	    break;
    }
    CHECK(i < 2100);
    /* --- DB items as VM chains --------------------------------------- */
    {
	VMChain ch;
	dword size;
	word nb, ni;

	ch = VMCopyVMChain(vm, (VMChain) items[5], vm);
	CHECK((word) ch != 0 && Same(vm, (DBGroupAndItem) ch, 350, 5));
	CHECK(VMCompareVMChains(vm, (VMChain) items[5], vm, ch));
	CHECK(!VMCompareVMChains(vm, (VMChain) items[5], vm,
				 (VMChain) items[6]));
	CHECK(VMInfoVMChain(vm, ch, &size, &nb, &ni) && size == 350 &&
	      nb == 0 && ni == 1);
	VMFreeVMChain(vm, ch);
	/* the item's structure is the next one allocated in its group */
	CHECK(DBAlloc(vm, DBGroupFromGroupAndItem(ch), 1) ==
	      DBItemFromGroupAndItem(ch));
    }
    /* --- freeing a group: its blocks, the map item ------------------- */
    blk = BlockOf(vm, items[0]);
    DBGroupFree(vm, g);
    CHECK(!Exists(vm, g) && !Exists(vm, blk) && DBGetMap(vm) == 0);
    CHECK(Same(vm, copy2, 350, 3));
    map = copy2;
    DBSetMapUngrouped(vm, map);
    Fill(vm, DBCombineGroupAndItem(g2, DBAlloc(vm, g2, 64)), 64, 9);
    CHECK(VMClose(vm, FALSE) == 0);
    /* --- read again -------------------------------------------------- */
    vm = VMOpen("DB.VM", 0, VMO_OPEN, 0);
    CHECK(vm != 0);
    CHECK(DBGetMap(vm) == map && Same(vm, map, 350, 3));
    CHECK(Same(vm, inG2, 2000, 4));
    for (i = 1001; i < NUNG; i += 97)
	CHECK(DBInfoUngrouped(vm, ung[i], &n) && n == 1 + i % 5);
    a = DBAllocUngrouped(vm, 33);
    Fill(vm, a, 33, 6);
    DBReAllocUngrouped(vm, ung[2000], 40);
    CHECK(DBInfoUngrouped(vm, ung[2000], &n) && n == 40);
    CHECK(Same(vm, a, 33, 6));
    CHECK(VMClose(vm, FALSE) == 0);
    vm2 = VMOpen("DB2.VM", 0, VMO_OPEN, 0);
    CHECK(vm2 != 0 && Same(vm2, copy, 2000, 4));
    CHECK(VMClose(vm2, FALSE) == 0);
    /* --- a broken DB map: the file is refused ------------------------ */
    vm2 = VMOpen("BAD.VM", 0, VMO_CREATE_ONLY, 0);
    bad = VMAlloc(vm2, 40, DB_MAP_ID);
    VMSetDBMap(vm2, bad);
    CHECK(VMClose(vm2, FALSE) == 0);
    CHECK(VMOpen("BAD.VM", 0, VMO_OPEN, 0) == 0 &&
	  ThreadGetError() == VM_OPEN_INVALID_VM_FILE);

    /* --- files of DOS16 GEOS ---------------------------------------- */
    for (i = 1; i < argc; i++)
	Dos16File(argv[i], i);

    printf(fails ? "kernel.db: %d failed\n" : "kernel.db ok\n", fails);
    return fails != 0;
}
