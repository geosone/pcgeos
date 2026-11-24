/*
 * test_kernel_heap.c -- the handle table and memory block API of the C
 * kernel (Library/Kernel/Heap/heapHandle.c, heapHigh.c; CInclude/heap.h),
 * and LZG (heapCompress.c): a stream written as the asm writes it, and
 * data of runs, repeats and noise through compress and uncompress.
 *
 * Fatal errors are caught through kernelFatalHook, so the error checks
 * are tested too.
 */
#include "kernelC.h"

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

/* run `stmt' and check that it ends in fatal error `code' */
#define EXPECT_FATAL(code, stmt) \
    do { \
	caught = KFE_NONE; \
	if (setjmp(catcher) == 0) { stmt; } \
	if (caught != (code)) { \
	    fails++; \
	    printf("FAIL line %d: expected %s, got %s\n", __LINE__, \
		   KernelFatalErrorName(code), KernelFatalErrorName(caught)); \
	} \
    } while (0)

#define MANY 5000

/* handles are 32 bits (Port32/ABI.md section 2) */
typedef char HandleIs32Bits[sizeof(Handle) == 4 && sizeof(MemHandle) == 4
			    ? 1 : -1];

int main(void)
{
    static MemHandle many[MANY];
    MemHandle h, h2, f;
    byte *p;
    dword base, i;
    int ok;

    kernelFatalHook = Catch;
    kernelCurrentGeode = 0x2340;
    base = HandleCountUsed();

    /* allocate, lock, use */
    h = MemAlloc(100, HF_SWAPABLE, HAF_ZERO_INIT);
    CHECK(h != 0 && h % HANDLE_GRANULE == 0);
    CHECK(MemGetInfo(h, MGIT_SIZE) == 100);
    CHECK(MemOwner(h) == 0x2340);
    CHECK(MemGetInfo(h, MGIT_OWNER_OR_VM_FILE_HANDLE) == 0x2340);
    CHECK(MGI_LOCK_COUNT(MemGetInfo(h, MGIT_FLAGS_AND_LOCK_COUNT)) == 0);
    CHECK(MGI_TYPE_FLAGS(MemGetInfo(h, MGIT_FLAGS_AND_LOCK_COUNT))
	  == HF_SWAPABLE);
    p = MemLock(h);
    CHECK(p != NULL && p[0] == 0 && p[99] == 0);
    memcpy(p, "hello", 6);
    CHECK(MGI_LOCK_COUNT(MemGetInfo(h, MGIT_FLAGS_AND_LOCK_COUNT)) == 1);
    CHECK(MemLock(h) == p && MemDeref(h) == p);
    MemUnlock(h);
    MemUnlock(h);
    CHECK(MemPtrToHandle(p + 50) == h);
    CHECK(MemPtrToHandle(p + 100) != h);
    EXPECT_FATAL(BAD_UNLOCK, MemUnlock(h));

    /* resize: keeps the data, zero-fills the new part */
    CHECK(MemReAlloc(h, 300, HAF_ZERO_INIT | HAF_LOCK) == h);
    p = MemDeref(h);
    CHECK(strcmp((char *) p, "hello") == 0 && p[299] == 0 && p[150] == 0);
    CHECK(MemGetInfo(h, MGIT_SIZE) == 300);
    MemUnlock(h);

    /* resize to 0: the handle stays, the data is gone */
    CHECK(MemReAlloc(h, 0, 0) == h);
    CHECK(MemLock(h) == NULL);
    CHECK(MGI_TYPE_FLAGS(MemGetInfo(h, MGIT_FLAGS_AND_LOCK_COUNT))
	  & HF_DISCARDED);
    CHECK(MemReAlloc(h, 20, HAF_ZERO_INIT) == h && MemDeref(h) != NULL);
    CHECK(!(MemGetInfo(h, MGIT_FLAGS_AND_LOCK_COUNT) & HF_DISCARDED));

    /* discard: only when unlocked */
    MemModifyFlags(h, HF_DISCARDABLE, 0);
    MemLock(h);
    MemDiscard(h);
    CHECK(MemDeref(h) != NULL);
    MemUnlock(h);
    MemDiscard(h);
    CHECK(MemDeref(h) == NULL && MemLock(h) == NULL);
    MemFree(h);
    EXPECT_FATAL(HANDLE_FREE, MemLock(h));
    EXPECT_FATAL(HANDLE_FREE, MemFree(h));

    /* fixed blocks need no lock */
    f = MemAlloc(16, HF_FIXED, HAF_LOCK);
    CHECK(MemDeref(f) != NULL && MemLock(f) == MemDeref(f));
    CHECK(MGI_LOCK_COUNT(MemGetInfo(f, MGIT_FLAGS_AND_LOCK_COUNT)) == 0);
    MemUnlock(f);
    MemFree(f);

    /* reference counts */
    h = MemAlloc(8, HF_DYNAMIC, 0);
    MemInitRefCount(h, 2);
    MemIncRefCount(h);
    MemDecRefCount(h);
    MemDecRefCount(h);
    CHECK(MemGetInfo(h, MGIT_OTHER_INFO) == 1);
    MemDecRefCount(h);                      /* frees the block */
    EXPECT_FATAL(HANDLE_FREE, MemDeref(h));

    /* owner, other info */
    h = MemAllocSetOwner(0x5670, 8, HF_DYNAMIC, 0);
    CHECK(MemOwner(h) == 0x5670);
    HandleModifyOwner(h, 0x2340);
    CHECK(MemOwner(h) == 0x2340);
    MemModifyOtherInfo(h, 0xbeef);
    CHECK(MemGetInfo(h, MGIT_OTHER_INFO) == 0xbeef);

    /* error checks */
    EXPECT_FATAL(ILLEGAL_HANDLE, MemLock(0x1234));
    EXPECT_FATAL(ILLEGAL_HANDLE, MemLock(0));
    EXPECT_FATAL(ILLEGAL_HANDLE, MemLock(0x7ffffff0));
    EXPECT_FATAL(MEM_GET_INFO_BAD_PARAMETER, MemGetInfo(h, MGIT_ADDRESS));
    EXPECT_FATAL(ALLOC_ZERO, MemAlloc(0, HF_DYNAMIC, 0));
    EXPECT_FATAL(ALLOC_BAD_FLAGS, MemModifyFlags(h, HF_FIXED, 0));
    EXPECT_FATAL(MEM_REFERENCE_COUNT_ILLEGAL_INITIAL_VALUE,
		 MemInitRefCount(h, 0));
    for (i = 0; i < 255; i++)
	MemLock(h);
    EXPECT_FATAL(TOO_MANY_LOCKS, MemLock(h));
    MemFree(h);

    /* many handles: the table grows, handles are distinct and reused */
    ok = 1;
    for (i = 0; i < MANY; i++) {
	many[i] = MemAlloc(4, HF_DYNAMIC, HAF_LOCK);
	if (!many[i] || many[i] % HANDLE_GRANULE)
	    ok = 0;
	else
	    *(dword *) MemDeref(many[i]) = i;
    }
    CHECK(ok);
    CHECK(HandleCountUsed() == base + MANY);
    for (i = 0; i < MANY; i++)
	if (*(dword *) MemDeref(many[i]) != i)
	    ok = 0;
    CHECK(ok);
    for (i = 0; i < MANY; i++)
	MemFree(many[i]);
    CHECK(HandleCountUsed() == base);
    h2 = MemAlloc(4, HF_DYNAMIC, 0);
    CHECK(h2 != 0);
    MemFree(h2);

    /* LZG */
    {
	static byte data[9000], packed[12000], back[9000];
	static const byte abc[] = "ABCABCABC";
	static const byte abcPacked[] =
	    { 0xe0, 'A', 'B', 'C', 0x18, 0x03, 0x60 };
	MemHandle st = LZGAllocCompressStack(0);
	dword seed = 1;
	int n, sizes[] = { 0, 1, 2, 3, 9, 100, 1000, 9000 }, k;
	CHECK(st != 0);
	n = LZGCompress(packed, (byte *) abc, 9, st);
	CHECK(n == (int) sizeof(abcPacked) &&
	      !memcmp(packed, abcPacked, sizeof(abcPacked)));
	CHECK(LZGGetUncompressedSize(packed) == 9);
	CHECK(LZGUncompress(back, packed) == 9 && !memcmp(back, abc, 9));
	for (i = 0; i < (int) sizeof(data); i++) {
	    seed = seed * 1103515245UL + 12345;
	    data[i] = (i / 700) % 3 == 0 ? 0 :		/* runs */
		      (i / 700) % 3 == 1 ? (byte) "pc/geos "[i % 8] :
		      (byte) (seed >> 16);			/* noise */
	}
	for (k = 0; k < (int) (sizeof(sizes) / sizeof(sizes[0])); k++) {
	    memset(back, 0xee, sizeof(back));
	    n = LZGCompress(packed, data, sizes[k], st);
	    CHECK(n > 0 && n < sizes[k] + sizes[k] / 8 + 8);
	    CHECK(LZGGetUncompressedSize(packed) == sizes[k]);
	    CHECK(LZGUncompress(back, packed) == sizes[k] &&
		  !memcmp(back, data, sizes[k]));
	}
	CHECK(LZGCompress(packed, data, 9000, st) < 9000 * 2 / 3 + 300);
	LZGFreeCompressStack(st);
    }

    printf("%s: %d failure(s)\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
