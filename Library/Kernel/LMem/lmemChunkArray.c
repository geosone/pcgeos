/*
 * lmemChunkArray.c -- chunk arrays of the C kernel (CInclude/chunkarr.h;
 * after lmemChunkArray.asm).
 *
 * A chunk array is one LMem chunk: a header (ChunkArrayHeader or a larger
 * one, CAH_offset bytes), then the elements.  Fixed-size elements
 * (CAH_elementSize != 0) follow the header directly.  Variable-sized ones
 * (CAH_elementSize 0) have a table of CAH_count words at CAH_offset, the
 * offset of each element from the start of the chunk; an element ends
 * where the next begins (the last one at the end of the chunk).  This is
 * also what goc -cg writes for @chunkArray.
 *
 * Inserting and deleting may move the LMem block: callers dereference
 * again.  During ChunkArrayEnum the callback may delete or insert
 * elements; CAH_curOffset holds the running enumeration's position (the
 * index of the current element + 1, 0 when none runs) so it continues
 * with the right element.
 */
#include "kernelC.h"

#include <chunkarr.h>
#include <stdlib.h>
#include <string.h>

typedef ChunkArrayHeader CAH;

/* the handle of `arr', or of the block holding `ptr' when it is 0 (the
 * ...Handle macros of chunkarr.h pass handle 0 with a pointer) */
static optr Arr(optr arr, const void *ptr)
{
    if (OptrToHandle(arr) == 0 && ptr)
	return ConstructOptr(MemPtrToHandle((void *) ptr), OptrToChunk(arr));
    return arr;
}

static CAH *Hdr(optr arr)
{
    return LMemDeref(arr);
}

static word *Table(CAH *h)			/* variable-sized only */
{
    return (word *) ((byte *) h + h->CAH_offset);
}

/* offset of element i from the chunk start (i <= count) */
static word OffsetOf(optr arr, CAH *h, word i)
{
    if (h->CAH_elementSize)
	return (word) (h->CAH_offset + i * h->CAH_elementSize);
    if (i < h->CAH_count)
	return Table(h)[i];
    return LMemGetChunkSize(arr);
}

static word SizeOf(optr arr, CAH *h, word i)
{
    if (h->CAH_elementSize)
	return h->CAH_elementSize;
    return (word) (OffsetOf(arr, h, (word) (i + 1)) - OffsetOf(arr, h, i));
}

ChunkHandle ChunkArrayCreateAt(optr arr, word elementSize, word headerSize,
			       ObjChunkFlags ocf)
{
    MemHandle mh = OptrToHandle(arr);
    ChunkHandle ch = OptrToChunk(arr);
    CAH *h;

    if (headerSize == 0)
	headerSize = sizeof(CAH);
    if (headerSize < sizeof(CAH))
	KernelFatal(CHUNK_ARRAY_HEADER_SIZE_TOO_SMALL);
    if (ch == 0) {
	ch = LMemAlloc(mh, headerSize);
	if (!ch)
	    return 0;
    } else if (LMemReAlloc(arr, headerSize))
	return 0;
    arr = ConstructOptr(mh, ch);
    h = Hdr(arr);
    memset(h, 0, headerSize);
    h->CAH_elementSize = elementSize;
    h->CAH_offset = headerSize;
    if (ocf) {
	LMemBlockHeader *b = MemDeref(mh);
	if (b->LMBH_flags & LMF_HAS_FLAGS) {
	    byte *flags = LMemDeref(ConstructOptr(mh, b->LMBH_offset));
	    flags[(ch - b->LMBH_offset) / 2] = ocf;
	}
    }
    return ch;
}

word ChunkArrayGetCount(optr arr)
{
    return Hdr(arr)->CAH_count;
}

void *ChunkArrayElementToPtr(optr arr, word elementNumber, word *elementSize)
{
    CAH *h = Hdr(arr);
    word n = elementNumber;

    if (n == CA_NULL_ELEMENT)
	KernelFatal(NULL_ELEMENT_PASSED_TO_CHUNK_ARRAY_ROUTINE);
    if (n >= h->CAH_count) {			/* the last one, as asm */
	if (h->CAH_count == 0) {
	    if (elementSize)
		*elementSize = h->CAH_elementSize;
	    return (byte *) h + h->CAH_offset;
	}
	n = (word) (h->CAH_count - 1);
    }
    if (elementSize)
	*elementSize = SizeOf(arr, h, n);
    return (byte *) h + OffsetOf(arr, h, n);
}

word ChunkArrayPtrToElement(optr arr, void *element)
{
    CAH *h;
    word off, i;

    arr = Arr(arr, element);
    h = Hdr(arr);
    off = (word) ((byte *) element - (byte *) h);
    if (h->CAH_elementSize)
	return (word) ((off - h->CAH_offset) / h->CAH_elementSize);
    for (i = 0; i < h->CAH_count; i++)
	if (Table(h)[i] >= off)
	    return Table(h)[i] == off ? i : (word) (i - 1);
    return h->CAH_count;
}

void ChunkArrayGetElement(optr arr, word elementNumber, void *buffer)
{
    word size;
    void *p = ChunkArrayElementToPtr(arr, elementNumber, &size);

    memcpy(buffer, p, size);
}

/* insert a zeroed element of `size' bytes before element `i' */
static void *InsertIndex(optr arr, word i, word size)
{
    CAH *h = Hdr(arr);
    word k, at;

    if (i > h->CAH_count)
	KernelFatal(CHUNK_ARRAY_BAD_ELEMENT);
    if (h->CAH_elementSize) {
	size = h->CAH_elementSize;
	if (LMemInsertAt(arr, OffsetOf(arr, h, i), size))
	    return NULL;
    } else {
	/* a table entry (all elements move by 2), then the data */
	if (LMemInsertAt(arr, (word) (h->CAH_offset + 2 * i), 2))
	    return NULL;
	h = Hdr(arr);
	h->CAH_count++;
	for (k = 0; k < h->CAH_count; k++)
	    if (k != i)
		Table(h)[k] = (word) (Table(h)[k] + 2);
	at = i + 1u < h->CAH_count ? Table(h)[i + 1] : LMemGetChunkSize(arr);
	Table(h)[i] = at;
	h->CAH_count--;
	if (LMemInsertAt(arr, at, size)) {
	    /* take the table entry out again */
	    h = Hdr(arr);
	    h->CAH_count++;
	    for (k = 0; k < h->CAH_count; k++)
		if (k != i)
		    Table(h)[k] = (word) (Table(h)[k] - 2);
	    h->CAH_count--;
	    LMemDeleteAt(arr, (word) (h->CAH_offset + 2 * i), 2);
	    return NULL;
	}
	h = Hdr(arr);
	for (k = (word) (i + 1); k <= h->CAH_count; k++)
	    Table(h)[k] = (word) (Table(h)[k] + size);
    }
    h = Hdr(arr);
    h->CAH_count++;
    if (h->CAH_curOffset && i < h->CAH_curOffset)
	h->CAH_curOffset++;			/* the running enum's element */
    return (byte *) h + OffsetOf(arr, h, i);
}

void *ChunkArrayAppend(optr arr, word elementSize)
{
    return InsertIndex(arr, Hdr(arr)->CAH_count, elementSize);
}

void *ChunkArrayInsertAt(optr arr, void *insertPointer, word elementSize)
{
    arr = Arr(arr, insertPointer);
    return InsertIndex(arr, ChunkArrayPtrToElement(arr, insertPointer),
		       elementSize);
}

static void DeleteIndex(optr arr, word i)
{
    CAH *h = Hdr(arr);
    word size, k;

    if (i >= h->CAH_count)
	KernelFatal(CHUNK_ARRAY_BAD_ELEMENT);
    size = SizeOf(arr, h, i);
    LMemDeleteAt(arr, OffsetOf(arr, h, i), size);
    h = Hdr(arr);
    if (!h->CAH_elementSize) {
	for (k = (word) (i + 1); k < h->CAH_count; k++)
	    Table(h)[k] = (word) (Table(h)[k] - size);
	LMemDeleteAt(arr, (word) (h->CAH_offset + 2 * i), 2);
	h = Hdr(arr);
	for (k = 0; k + 1u < h->CAH_count; k++)
	    Table(h)[k] = (word) (Table(h)[k] - 2);
    }
    h->CAH_count--;
    if (h->CAH_curOffset && i < h->CAH_curOffset)
	h->CAH_curOffset--;
}

void ChunkArrayDelete(optr arr, void *element)
{
    arr = Arr(arr, element);
    DeleteIndex(arr, ChunkArrayPtrToElement(arr, element));
}

void ChunkArrayDeleteRange(optr arr, word firstElement, word count)
{
    word n = ChunkArrayGetCount(arr);

    if (firstElement > n)
	KernelFatal(CHUNK_ARRAY_BAD_ELEMENT);
    if (count == 0xffff || count > n - firstElement)
	count = (word) (n - firstElement);
    while (count--)
	DeleteIndex(arr, (word) (firstElement + count));
}

void ChunkArrayElementResize(optr arr, word element, word newSize)
{
    CAH *h = Hdr(arr);
    word old, k, off;

    if (h->CAH_elementSize)
	KernelFatal(CANNOT_RESIZE_FIXED_SIZE_ELEMENT);
    if (element >= h->CAH_count)
	KernelFatal(CHUNK_ARRAY_BAD_ELEMENT);
    old = SizeOf(arr, h, element);
    off = OffsetOf(arr, h, element);
    if (newSize > old) {
	if (LMemInsertAt(arr, (word) (off + old), (word) (newSize - old)))
	    return;
    } else if (newSize < old)
	LMemDeleteAt(arr, (word) (off + newSize), (word) (old - newSize));
    h = Hdr(arr);
    for (k = (word) (element + 1); k < h->CAH_count; k++)
	Table(h)[k] = (word) (Table(h)[k] + newSize - old);
}

void ChunkArrayZero(optr arr)
{
    CAH *h = Hdr(arr);
    word hs = h->CAH_offset;

    h->CAH_count = 0;
    LMemReAlloc(arr, hs);
}

Boolean ChunkArrayEnumRange(optr arr, word startElement, word count,
			    void *enumData,
			    Boolean (*callback)(void *element, void *enumData))
{
    CAH *h = Hdr(arr);
    word saved = h->CAH_curOffset, i;
    Boolean stop = FALSE;

    h->CAH_curOffset = (word) (startElement + 1);
    while (count) {
	h = Hdr(arr);
	i = (word) (h->CAH_curOffset - 1);
	if (i >= h->CAH_count)
	    break;
	stop = callback((byte *) h + OffsetOf(arr, h, i), enumData);
	if (stop)
	    break;
	h = Hdr(arr);
	h->CAH_curOffset++;
	if (count != 0xffff)
	    count--;
    }
    Hdr(arr)->CAH_curOffset = saved;
    return stop ? TRUE : FALSE;
}

Boolean ChunkArrayEnum(optr arr, void *enumData,
		       Boolean (*callback)(void *element, void *enumData))
{
    return ChunkArrayEnumRange(arr, 0, 0xffff, enumData, callback);
}

/* ---------------------------------------------------------------------
 * Sorting
 * ------------------------------------------------------------------- */

static void Swap(byte *a, byte *b, word size)
{
    while (size--) {
	byte t = *a;
	*a++ = *b;
	*b++ = t;
    }
}

typedef struct {
    word (*cmp)(void *, void *, word);		/* ArrayQuickSort */
    sword (*scmp)(void *, void *, word);	/* ChunkArraySort */
    void (*lock)(void *, word);
    void (*unlock)(void *, word);
    word value;
    word size;
} SortCtx;

static sword Compare(SortCtx *c, byte *a, byte *b)
{
    sword r;

    if (c->lock) {
	c->lock(a, c->value);
	c->lock(b, c->value);
    }
    r = c->scmp ? c->scmp(a, b, c->value) : (sword) c->cmp(a, b, c->value);
    if (c->unlock) {
	c->unlock(a, c->value);
	c->unlock(b, c->value);
    }
    return r;
}

static void Sort(SortCtx *c, byte *base, word n, word insertLimit)
{
    word size = c->size;

    while (n > insertLimit && n > 2) {
	byte *lo = base, *hi = base + (dword) (n - 1) * size;
	byte *mid = base + (dword) (n / 2) * size;
	byte *pivot;
	word i, store;

	/* median of three to the end */
	if (Compare(c, lo, mid) > 0)
	    Swap(lo, mid, size);
	if (Compare(c, mid, hi) > 0)
	    Swap(mid, hi, size);
	if (Compare(c, lo, mid) > 0)
	    Swap(lo, mid, size);
	Swap(mid, hi, size);
	pivot = hi;
	store = 0;
	for (i = 0; i + 1u < n; i++)
	    if (Compare(c, base + (dword) i * size, pivot) < 0) {
		Swap(base + (dword) i * size, base + (dword) store * size,
		     size);
		store++;
	    }
	Swap(base + (dword) store * size, pivot, size);
	/* recurse into the smaller part */
	if (store < n - store - 1) {
	    Sort(c, base, store, insertLimit);
	    base += (dword) (store + 1) * size;
	    n = (word) (n - store - 1);
	} else {
	    Sort(c, base + (dword) (store + 1) * size,
		 (word) (n - store - 1), insertLimit);
	    n = store;
	}
    }
    /* insertion sort */
    {
	word i, j;
	for (i = 1; i < n; i++)
	    for (j = i; j > 0 &&
		 Compare(c, base + (dword) (j - 1) * size,
			 base + (dword) j * size) > 0; j--)
		Swap(base + (dword) (j - 1) * size, base + (dword) j * size,
		     size);
    }
}

void ArrayQuickSort(void *array, word count, word elementSize,
		    word valueForCallback, QuickSortParameters *parameters)
{
    SortCtx c;

    c.cmp = parameters->QSP_compareCallback;
    c.scmp = NULL;
    c.lock = parameters->QSP_lockCallback;
    c.unlock = parameters->QSP_unlockCallback;
    c.value = valueForCallback;
    c.size = elementSize;
    Sort(&c, array, count,
	 parameters->QSP_insertLimit ? parameters->QSP_insertLimit : 7);
}

void ChunkArraySort(optr arr, word valueForCallback,
		    sword (*callback)(void *el1, void *el2,
				      word valueForCallback))
{
    CAH *h = Hdr(arr);
    SortCtx c;

    if (!h->CAH_elementSize)
	KernelFatal(CANNOT_SORT_VARIABLE_SIZED_CHUNK_ARRAYS);
    memset(&c, 0, sizeof(c));
    c.scmp = callback;
    c.value = valueForCallback;
    c.size = h->CAH_elementSize;
    Sort(&c, (byte *) h + h->CAH_offset, h->CAH_count, 7);
}
