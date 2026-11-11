/*
 * test_kernel_lmem.c -- local memory heaps of the C kernel
 * (Library/Kernel/LMem/lmemCode.c, CInclude/lmem.h).
 *
 * A random sequence of allocations, frees, resizes, inserts and deletes
 * runs against a shadow copy of every chunk; after each step the block
 * is validated and every chunk compared.  Then object blocks (flags
 * chunk), contraction, the 64K limit, error checks and a block built
 * from a goc -cg resource description.
 */
#include "kernelC.h"

#include <object.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
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

static dword seed = 12345;
static dword Rand(dword n)
{
    seed = seed * 1103515245u + 12345u;
    return (seed >> 8) % n;
}

#define SLOTS 60
#define MAXSZ 700
static ChunkHandle ch[SLOTS];
static word len[SLOTS];
static byte shadow[SLOTS][MAXSZ * 2];

static int Same(MemHandle mh)
{
    int i;
    for (i = 0; i < SLOTS; i++) {
	byte *p;
	if (!ch[i])
	    continue;
	if (LMemGetChunkSize(ConstructOptr(mh, ch[i])) != len[i])
	    return 0;
	p = LMemDeref(ConstructOptr(mh, ch[i]));
	if (len[i] == 0 ? p != NULL
	    : (p == NULL || ((uintptr_t) p & 7) || memcmp(p, shadow[i], len[i])))
	    return 0;
    }
    return 1;
}

static void Fill(int i, word from, word n)
{
    word k;
    for (k = from; k < from + n; k++)
	shadow[i][k] = (byte) Rand(256);
}

static void RandomOps(MemHandle mh, int steps)
{
    int s, bad = 0;

    for (s = 0; s < steps && !bad; s++) {
	int i = (int) Rand(SLOTS);
	optr o = ConstructOptr(mh, ch[i]);
	word n, off;
	byte *p;

	switch (ch[i] ? Rand(4) : 9) {
	case 9:					/* alloc */
	    n = (word) Rand(MAXSZ);
	    ch[i] = LMemAlloc(mh, n);
	    len[i] = n;
	    Fill(i, 0, n);
	    if (n)
		memcpy(LMemDeref(ConstructOptr(mh, ch[i])), shadow[i], n);
	    break;
	case 0:					/* free */
	    LMemFree(o);
	    ch[i] = 0;
	    break;
	case 1:					/* resize */
	    n = (word) Rand(MAXSZ);
	    CHECK(!LMemReAlloc(o, n));
	    if (n > len[i])
		Fill(i, len[i], n - len[i]);
	    p = LMemDeref(o);
	    if (n > len[i])
		memcpy(p + len[i], shadow[i] + len[i], n - len[i]);
	    len[i] = n;
	    break;
	case 2:					/* insert */
	    off = (word) Rand(len[i] + 1u);
	    n = (word) Rand(200);
	    CHECK(!LMemInsertAt(o, off, n));
	    memmove(shadow[i] + off + n, shadow[i] + off, len[i] - off);
	    memset(shadow[i] + off, 0, n);
	    len[i] = (word) (len[i] + n);
	    if (len[i] > MAXSZ) {		/* keep the shadow in bounds */
		LMemDeleteAt(o, MAXSZ, (word) (len[i] - MAXSZ));
		len[i] = MAXSZ;
	    }
	    break;
	case 3:					/* delete */
	    off = (word) Rand(len[i] + 1u);
	    n = (word) Rand(len[i] - off + 1u);
	    LMemDeleteAt(o, off, n);
	    memmove(shadow[i] + off, shadow[i] + off + n, len[i] - off - n);
	    len[i] = (word) (len[i] - n);
	    break;
	}
	LMemValidate(mh);
	if (!Same(mh)) {
	    printf("FAIL: chunks differ after step %d\n", s);
	    fails++;
	    bad = 1;
	}
    }
}

/* a resource as goc -cg describes it: object block, flags chunk, an
 * object, a two-part chunk, an empty chunk */
typedef struct { void *cls; word a; optr link; } TestObj;
static ObjLMemBlockHeader resHeader = {
    {0, (word) GOCR_LMEM_HANDLES(ObjLMemBlockHeader),
     LMF_HAS_FLAGS | LMF_IN_RESOURCE, LMEM_TYPE_OBJ_BLOCK, 0, 4, 0, 0},
    0, 0, 0, 0, 3};
static byte resFlags[] = {OCF_IGNORE_DIRTY, OCF_IN_RESOURCE | OCF_IS_OBJECT,
			  OCF_IN_RESOURCE, OCF_IN_RESOURCE, 0, 0, 0};
static TestObj resObj = {&resHeader, 0x1234,
			 GOCR_UNRELOC_OPTR(3, GOCR_CHUNK_HANDLE(ObjLMemBlockHeader, 2))};
static char resPart1[] = "head:";
static char resPart2[] = "tail";
static const GocChunk resChunks[] = {
    {resFlags, sizeof(resFlags), 0, 0, 0, 0},
    {&resObj, sizeof(resObj), 0, 0, 0, 0},
    {resPart1, 5, resPart2, sizeof(resPart2), 0, 0},
    {0, 0, 0, 0, 0, 0},
};
static const GocResource res = {3, 4, &resHeader, sizeof(resHeader),
				resChunks};

int main(int argc, char **argv)
{
    MemHandle mh, ob, rb;
    ChunkHandle a, c;
    byte *flags, *p;
    word before, i;
    LMemBlockHeader *h;
    TestObj *obj;

    kernelFatalHook = Catch;
    kernelCurrentGeode = 0x2340;
    if (argc > 1)			/* another random sequence */
	seed = (dword) strtoul(argv[1], NULL, 0);

    /* random operations against shadow copies */
    mh = MemAllocLMem(LMEM_TYPE_GENERAL, 0);
    CHECK(mh && (MemGetInfo(mh, MGIT_FLAGS_AND_LOCK_COUNT) & HF_LMEM));
    MemLock(mh);
    h = MemDeref(mh);
    CHECK(h->LMBH_handle == mh && h->LMBH_lmemType == LMEM_TYPE_GENERAL);
    CHECK(h->LMBH_offset == ((sizeof(LMemBlockHeader) + 3) & ~3u));
    LMemValidate(mh);
    RandomOps(mh, 3000);
    MemUnlock(mh);

    /* contract: compaction, then the free space goes */
    before = (word) MemGetInfo(mh, MGIT_SIZE);
    for (i = 0; i < SLOTS; i += 2)
	if (ch[i]) {
	    LMemFree(ConstructOptr(mh, ch[i]));
	    ch[i] = 0;
	}
    LMemContract(mh);
    LMemValidate(mh);
    CHECK(Same(mh));
    CHECK(MemGetInfo(mh, MGIT_SIZE) < before);
    CHECK(((LMemBlockHeader *) MemDeref(mh))->LMBH_totalFree <= 8);
    RandomOps(mh, 500);			/* still works after it */

    /* the 64K limit: fatal, or 0 / TRUE with LMF_RETURN_ERRORS */
    a = LMemAlloc(mh, 30000);
    CHECK(a != 0);
    EXPECT_FATAL(LMEM_CANNOT_ALLOCATE, LMemAlloc(mh, 40000));
    ((LMemBlockHeader *) MemDeref(mh))->LMBH_flags |= LMF_RETURN_ERRORS;
    CHECK(LMemAlloc(mh, 40000) == 0);
    CHECK(LMemReAlloc(ConstructOptr(mh, a), 60000) == TRUE);
    CHECK(LMemGetChunkSize(ConstructOptr(mh, a)) == 30000);
    LMemValidate(mh);
    LMemFree(ConstructOptr(mh, a));

    /* error checks */
    EXPECT_FATAL(ILLEGAL_HANDLE, LMemDeref(ConstructOptr(mh, a)));
    EXPECT_FATAL(LMEM_HANDLE_BEFORE_HANDLE_TABLE,
		 LMemDeref(ConstructOptr(mh, 2)));
    EXPECT_FATAL(LMEM_HANDLE_BEYOND_HANDLE_TABLE,
		 LMemDeref(ConstructOptr(mh, 0xfff0)));
    EXPECT_FATAL(LMEM_CHUNK_HANDLE_AT_ODD_ADDRESS,
		 LMemDeref(ConstructOptr(mh, (ChunkHandle) (a + 1))));
    c = LMemAlloc(mh, 10);
    EXPECT_FATAL(LMEM_BAD_INSERT_OFFSET,
		 LMemInsertAt(ConstructOptr(mh, c), 11, 1));
    EXPECT_FATAL(LMEM_BAD_DELETE_SIZE,
		 LMemDeleteAt(ConstructOptr(mh, c), 5, 6));
    EXPECT_FATAL(NOT_LMEM_HANDLE,
		 LMemAlloc(MemAlloc(10, HF_DYNAMIC, 0), 4));
    MemFree(mh);

    /* object block: handle 0 is the flags chunk, a byte per handle */
    ob = MemAllocLMem(LMEM_TYPE_OBJ_BLOCK, 0);
    h = MemDeref(ob);
    CHECK(h->LMBH_offset ==
	  ((sizeof(ObjLMemBlockHeader) + 3) & ~3u));
    CHECK(h->LMBH_flags == (LMF_HAS_FLAGS | LMF_RELOCATED));
    CHECK(MemGetInfo(ob, MGIT_OTHER_INFO) == KernelThisThread());
    for (i = 0; i < 40; i++) {
	a = LMemAlloc(ob, 16);
	CHECK(a == h->LMBH_offset + 2 * (i + 1) || (h = MemDeref(ob), 0));
	h = MemDeref(ob);
    }
    CHECK(h->LMBH_nHandles >= 41);
    CHECK(LMemGetChunkSize(ConstructOptr(ob, h->LMBH_offset))
	  >= h->LMBH_nHandles);
    flags = LMemDeref(ConstructOptr(ob, h->LMBH_offset));
    for (i = 0; i < h->LMBH_nHandles; i++)
	CHECK(flags[i] == 0);
    LMemValidate(ob);
    MemFree(ob);

    /* a block from a goc -cg resource description */
    rb = LMemCreateResourceBlock(&res, 0x2340, HF_SWAPABLE, 0);
    CHECK(rb != 0);
    LMemValidate(rb);
    h = MemDeref(rb);
    CHECK(h->LMBH_handle == rb && h->LMBH_nHandles == 4);
    CHECK(h->LMBH_flags == (LMF_HAS_FLAGS | LMF_IN_RESOURCE));
    flags = LMemDeref(ConstructOptr(rb, GOCR_CHUNK_HANDLE(ObjLMemBlockHeader, 0)));
    CHECK(flags[1] == (OCF_IN_RESOURCE | OCF_IS_OBJECT));
    obj = LMemDeref(ConstructOptr(rb, GOCR_CHUNK_HANDLE(ObjLMemBlockHeader, 1)));
    CHECK(obj && ((uintptr_t) obj & 7) == 0 && obj->a == 0x1234 &&
	  obj->link == resObj.link);
    p = LMemDeref(ConstructOptr(rb, GOCR_CHUNK_HANDLE(ObjLMemBlockHeader, 2)));
    CHECK(p && strcmp((char *) p, "head:tail") == 0);
    CHECK(LMemGetChunkSize(ConstructOptr(rb,
			   GOCR_CHUNK_HANDLE(ObjLMemBlockHeader, 2))) == 10);
    CHECK(LMemDeref(ConstructOptr(rb,
		    GOCR_CHUNK_HANDLE(ObjLMemBlockHeader, 3))) == NULL);
    /* it is an ordinary LMem block after that */
    a = LMemAlloc(rb, 100);
    CHECK(!LMemReAlloc(ConstructOptr(rb, GOCR_CHUNK_HANDLE(ObjLMemBlockHeader, 3)), 20));
    LMemValidate(rb);
    p = LMemDeref(ConstructOptr(rb, GOCR_CHUNK_HANDLE(ObjLMemBlockHeader, 2)));
    CHECK(strcmp((char *) p, "head:tail") == 0);
    MemFree(rb);

    printf("%s: %d failure(s)\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
