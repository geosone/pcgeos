/*
 * test_kernel_carray.c -- chunk, element and name arrays of the C kernel
 * (Library/Kernel/LMem/lmemChunkArray.c, lmemElementArray.c,
 * lmemNameArray.c; CInclude/chunkarr.h).
 */
#include "kernelC.h"

#include <chunkarr.h>
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

static dword seed = 777;
static dword Rand(dword n)
{
    seed = seed * 1103515245u + 12345u;
    return (seed >> 8) % n;
}

static sword CmpDword(void *a, void *b, word v)
{
    dword x = *(dword *) a, y = *(dword *) b;
    (void) v;
    return x < y ? -1 : x > y;
}

static word CmpDwordW(void *a, void *b, word v)
{
    return (word) CmpDword(a, b, v);
}

/* deletes even values while enumerating */
static Boolean DropEven(void *el, void *data)
{
    optr arr = *(optr *) data;
    if (*(dword *) el % 2 == 0)
	ChunkArrayDelete(arr, el);
    return FALSE;
}

static Boolean StopAt(void *el, void *data)
{
    (*(int *) data)++;
    return *(dword *) el == 7;
}

typedef struct {
    RefElementHeader h;
    word value;
} Ref;

static Boolean SameValue(void *a, void *b, dword v)
{
    (void) v;
    return ((Ref *) a)->value == ((Ref *) b)->value;
}

static Boolean OddOnly(void *el, dword v)
{
    (void) v;
    return ((Ref *) el)->value % 2 == 1;
}

static dword Refs(optr arr, word token)
{
    RefElementHeader *e = ChunkArrayElementToPtr(arr, token, NULL);
    return (word) e->REH_refCount.WAAH_low |
	   ((dword) (byte) e->REH_refCount.WAAH_high << 16);
}

#define VN 40
static char vshadow[VN * 4][300];
static word vlen[VN * 4];

int main(void)
{
    MemHandle mh = 0;
    optr arr, va, ea, na;
    ChunkHandle ch;
    dword i, *p, buf, sorted[1000], plain[1000];
    word size, n, t1, t2, t3, k;
    int visits;
    QuickSortParameters qsp;

    kernelFatalHook = Catch;
    kernelCurrentGeode = 0x2340;
    mh = MemAllocLMem(LMEM_TYPE_GENERAL, 0);

    /* --- fixed-size array ------------------------------------------- */
    ch = ChunkArrayCreate(mh, sizeof(dword), 0, 0);
    CHECK(ch != 0);
    arr = ConstructOptr(mh, ch);
    for (i = 0; i < 100; i++) {
	p = ChunkArrayAppend(arr, 0);
	CHECK(p && *p == 0);
	*p = i;
    }
    CHECK(ChunkArrayGetCount(arr) == 100);
    p = ChunkArrayElementToPtr(arr, 42, &size);
    CHECK(*p == 42 && size == 4);
    CHECK(ChunkArrayPtrToElement(arr, p) == 42);
    CHECK(ChunkArrayPtrToElementHandle(ch, p) == 42);	/* handle 0 */
    CHECK(*(dword *) ChunkArrayElementToPtr(arr, 500, NULL) == 99);
    ChunkArrayGetElement(arr, 7, &buf);
    CHECK(buf == 7);
    p = ChunkArrayInsertAt(arr, ChunkArrayElementToPtr(arr, 10, NULL), 0);
    *p = 1000;
    CHECK(*(dword *) ChunkArrayElementToPtr(arr, 10, NULL) == 1000 &&
	  *(dword *) ChunkArrayElementToPtr(arr, 11, NULL) == 10);
    ChunkArrayDelete(arr, ChunkArrayElementToPtr(arr, 10, NULL));
    ChunkArrayDeleteRange(arr, 90, 0xffff);
    CHECK(ChunkArrayGetCount(arr) == 90);
    visits = 0;
    CHECK(ChunkArrayEnum(arr, &visits, StopAt) == TRUE && visits == 8);
    visits = 0;
    CHECK(ChunkArrayEnumRange(arr, 3, 2, &visits, StopAt) == FALSE &&
	  visits == 2);
    ChunkArrayEnum(arr, &arr, DropEven);	/* deletes as it goes */
    n = ChunkArrayGetCount(arr);
    CHECK(n == 45);
    for (k = 0; k < n; k++)
	CHECK(*(dword *) ChunkArrayElementToPtr(arr, k, NULL) == 2u * k + 1);
    for (k = 0; k < n; k++)
	*(dword *) ChunkArrayElementToPtr(arr, k, NULL) = Rand(1000);
    ChunkArraySort(arr, 0, CmpDword);
    for (k = 1; k < n; k++)
	CHECK(*(dword *) ChunkArrayElementToPtr(arr, k - 1, NULL) <=
	      *(dword *) ChunkArrayElementToPtr(arr, k, NULL));
    ChunkArrayZero(arr);
    CHECK(ChunkArrayGetCount(arr) == 0);
    LMemValidate(mh);

    /* --- variable-sized array against shadows ------------------------ */
    va = ConstructOptr(mh, ChunkArrayCreate(mh, 0, 0, 0));
    n = 0;
    for (i = 0; i < 2000; i++) {
	word at = (word) (n ? Rand(n + 1u) : 0), len;
	char *e;
	switch (n < VN ? 0 : Rand(3)) {
	case 0:					/* insert */
	    /* not 0 bytes: a pointer cannot tell an empty element from the
	     * next one (as in the asm kernel) */
	    len = (word) (1 + Rand(279));
	    e = at == n ? ChunkArrayAppend(va, len)
			: ChunkArrayInsertAt(va,
				ChunkArrayElementToPtr(va, at, NULL), len);
	    CHECK(e != NULL);
	    memmove(vshadow[at + 1], vshadow[at], (size_t) (n - at) * 300);
	    memmove(vlen + at + 1, vlen + at, (size_t) (n - at) * sizeof(word));
	    for (k = 0; k < len; k++)
		vshadow[at][k] = e[k] = (char) Rand(256);
	    vlen[at] = len;
	    n++;
	    break;
	case 1:					/* delete */
	    if (at == n)
		at--;
	    ChunkArrayDelete(va, ChunkArrayElementToPtr(va, at, NULL));
	    memmove(vshadow[at], vshadow[at + 1], (size_t) (n - at - 1) * 300);
	    memmove(vlen + at, vlen + at + 1,
		    (size_t) (n - at - 1) * sizeof(word));
	    n--;
	    break;
	case 2:					/* resize */
	    if (at == n)
		at--;
	    len = (word) (1 + Rand(279));
	    ChunkArrayElementResize(va, at, len);
	    e = ChunkArrayElementToPtr(va, at, NULL);
	    for (k = vlen[at]; k < len; k++)
		vshadow[at][k] = e[k] = (char) Rand(256);
	    vlen[at] = len;
	    break;
	}
	if (ChunkArrayGetCount(va) != n)
	    break;
	for (k = 0; k < n; k++) {
	    char *e = ChunkArrayElementToPtr(va, k, &size);
	    if (size != vlen[k] || memcmp(e, vshadow[k], size) != 0 ||
		ChunkArrayPtrToElement(va, e) != k)
		break;
	}
	if (k != n) {
	    printf("FAIL: variable array differs at step %u (element %u of %u, "
		   "size %u want %u)\n", (unsigned) i, k, n,
		   (unsigned) size, (unsigned) vlen[k]);
	    fails++;
	    break;
	}
	LMemValidate(mh);
	if (n > 3 * VN)
	    ChunkArrayDeleteRange(va, 0, (word) (n - VN)),
	    memmove(vshadow[0], vshadow[n - VN], VN * 300),
	    memmove(vlen, vlen + n - VN, VN * sizeof(word)),
	    n = VN;
    }
    EXPECT_FATAL(CANNOT_SORT_VARIABLE_SIZED_CHUNK_ARRAYS,
		 ChunkArraySort(va, 0, CmpDword));
    EXPECT_FATAL(CANNOT_RESIZE_FIXED_SIZE_ELEMENT,
		 ChunkArrayElementResize(arr, 0, 3));

    /* --- element array ----------------------------------------------- */
    ea = ConstructOptr(mh, ElementArrayCreate(mh, sizeof(Ref), 0, 0));
    {
	Ref r;
	memset(&r, 0, sizeof(r));
	r.value = 5;
	t1 = ElementArrayAddElement(ea, &r, 0, NULL);
	t2 = ElementArrayAddElement(ea, &r, 0, NULL);
	CHECK(t1 == 0 && t2 == 0 && Refs(ea, 0) == 2);
	r.value = 6;
	t2 = ElementArrayAddElement(ea, &r, 0, NULL);
	r.value = 7;
	t3 = ElementArrayAddElement(ea, &r, 0, SameValue);
	CHECK(t2 == 1 && t3 == 2);
	CHECK(ElementArrayGetUsedCount(ea, 0, NULL) == 3);
	CHECK(ElementArrayGetUsedCount(ea, 0, OddOnly) == 2);
	CHECK(ElementArrayUsedIndexToToken(ea, 1, 0, OddOnly) == 2);
	CHECK(ElementArrayTokenToUsedIndex(ea, 2, 0, OddOnly) == 1);
	ElementArrayRemoveReference(ea, t1, 0, NULL);
	CHECK(Refs(ea, 0) == 1);
	ElementArrayRemoveReference(ea, t1, 0, NULL);	/* freed */
	CHECK(ElementArrayGetUsedCount(ea, 0, NULL) == 2);
	CHECK(ElementArrayUsedIndexToToken(ea, 0, 0, NULL) == 1);
	r.value = 8;
	CHECK(ElementArrayAddElement(ea, &r, 0, NULL) == 0);	/* reused */
	CHECK(ChunkArrayGetCount(ea) == 3);
	/* element 0 becomes equal to 2: they merge */
	((Ref *) ChunkArrayElementToPtr(ea, 0, NULL))->value = 7;
	CHECK(ElementArrayElementChanged(ea, 0, 0, NULL) == 2);
	CHECK(Refs(ea, 2) == 2 && ElementArrayGetUsedCount(ea, 0, NULL) == 2);
	ElementArrayAddReference(ea, 1);
	ElementArrayDelete(ea, 1);
	CHECK(ElementArrayGetUsedCount(ea, 0, NULL) == 1);
	EXPECT_FATAL(CHUNK_ARRAY_BAD_ELEMENT, ElementArrayAddReference(ea, 1));
    }

    /* --- name array --------------------------------------------------- */
    na = ConstructOptr(mh, NameArrayCreate(mh, sizeof(word), 0, 0));
    {
	word d = 11, out = 0;
	t1 = NameArrayAdd(na, "alpha", 0, 0, &d);
	d = 22;
	t2 = NameArrayAdd(na, "beta", 0, 0, &d);
	CHECK(t1 == 0 && t2 == 1);
	d = 33;
	CHECK(NameArrayAdd(na, "alphabet", 5, 0, &d) == t1);	/* "alpha" */
	CHECK(Refs(na, t1) == 2);
	CHECK(NameArrayFind(na, "alpha", 0, &out) == t1 && out == 11);
	CHECK(NameArrayAdd(na, "alpha", 0, NAAF_SET_DATA_ON_REPLACE, &d) == t1);
	CHECK(NameArrayFind(na, "alpha", 0, &out) == t1 && out == 33);
	CHECK(NameArrayFind(na, "gamma", 0, NULL) == CA_NULL_ELEMENT);
	NameArrayChangeName(na, t2, "a much longer name", 0);
	CHECK(NameArrayFind(na, "beta", 0, NULL) == CA_NULL_ELEMENT);
	CHECK(NameArrayFind(na, "a much longer name", 0, &out) == t2 &&
	      out == 22);
	ElementArrayRemoveReference(na, t2, 0, NULL);	/* freed, shrunk */
	ChunkArrayElementToPtr(na, t2, &size);
	CHECK(size == sizeof(RefElementHeader));
	CHECK(NameArrayAdd(na, "delta", 0, 0, NULL) == t2);	/* reused */
	CHECK(NameArrayFind(na, "delta", 0, NULL) == t2);
	CHECK(NameArrayFind(na, "alpha", 0, NULL) == t1);
    }
    LMemValidate(mh);

    /* --- ArrayQuickSort on a plain array ------------------------------ */
    for (i = 0; i < 1000; i++)
	plain[i] = sorted[i] = Rand(100000);
    memset(&qsp, 0, sizeof(qsp));
    qsp.QSP_compareCallback = CmpDwordW;
    ArrayQuickSort(plain, 1000, sizeof(dword), 0, &qsp);
    for (i = 0; i < 1000; i++) {		/* reference: insertion */
	dword j = i, v = sorted[i];
	while (j > 0 && sorted[j - 1] > v) {
	    sorted[j] = sorted[j - 1];
	    j--;
	}
	sorted[j] = v;
    }
    CHECK(memcmp(plain, sorted, sizeof(plain)) == 0);

    MemFree(mh);
    printf("%s: %d failure(s)\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
