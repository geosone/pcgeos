/*
 * lmemElementArray.c -- element arrays of the C kernel (CInclude/
 * chunkarr.h; after lmemElementArray.asm).
 *
 * An element array is a chunk array (ElementArrayHeader) whose elements
 * start with a RefElementHeader: a 24-bit reference count.  Adding an
 * element that is already there adds a reference; an element whose last
 * reference goes is put on a free list (EAH_freePtr; the free element's
 * WAAH_low is the next free one, WAAH_high EA_FREE_ELEMENT) and reused,
 * so tokens (element numbers) stay valid.  Variable-sized free elements
 * shrink to the RefElementHeader.
 */
#include "kernelC.h"

#include <chunkarr.h>
#include <string.h>

#define EA_FREE_ELEMENT	((sbyte) 0xff)

typedef Boolean EqualCB(void *elementToAdd, void *elementFromArray,
			dword valueForCallback);

static RefElementHeader *Elt(optr arr, word token, word *size)
{
    return ChunkArrayElementToPtr(arr, token, size);
}

static int IsFree(const RefElementHeader *e)
{
    return e->REH_refCount.WAAH_high == EA_FREE_ELEMENT;
}

static dword RefCount(const RefElementHeader *e)
{
    return (word) e->REH_refCount.WAAH_low |
	   ((dword) (byte) e->REH_refCount.WAAH_high << 16);
}

static void SetRefCount(RefElementHeader *e, dword n)
{
    e->REH_refCount.WAAH_low = (sword) (word) n;
    e->REH_refCount.WAAH_high = (sbyte) (byte) (n >> 16);
}

static ElementArrayHeader *EHdr(optr arr)
{
    return LMemDeref(arr);
}

/* an element equal to `element' (size bytes), skipping `skip'; the token
 * or CA_NULL_ELEMENT */
static word Find(optr arr, void *element, word size, dword cbData,
		 EqualCB *cb, word skip)
{
    word n = ChunkArrayGetCount(arr), i, es;

    for (i = 0; i < n; i++) {
	RefElementHeader *e = Elt(arr, i, &es);
	if (i == skip || IsFree(e))
	    continue;
	if (cb ? cb(element, e, cbData)
	    : (es == size &&
	       memcmp((byte *) e + sizeof(RefElementHeader),
		      (byte *) element + sizeof(RefElementHeader),
		      size - sizeof(RefElementHeader)) == 0))
	    return i;
    }
    return CA_NULL_ELEMENT;
}

ChunkHandle ElementArrayCreateAt(optr arr, word elementSize, word headerSize,
				 ObjChunkFlags ocf)
{
    ChunkHandle ch;

    if (headerSize == 0)
	headerSize = sizeof(ElementArrayHeader);
    if (headerSize < sizeof(ElementArrayHeader))
	KernelFatal(CHUNK_ARRAY_HEADER_SIZE_TOO_SMALL);
    ch = ChunkArrayCreateAt(arr, elementSize, headerSize, ocf);
    if (ch)
	EHdr(ConstructOptr(OptrToHandle(arr), ch))->EAH_freePtr =
	    EA_FREE_LIST_TERMINATOR;
    return ch;
}

void ElementArrayAddReference(optr arr, word token)
{
    RefElementHeader *e = Elt(arr, token, NULL);

    if (token >= ChunkArrayGetCount(arr) || IsFree(e))
	KernelFatal(CHUNK_ARRAY_BAD_ELEMENT);
    SetRefCount(e, RefCount(e) + 1);
}

word ElementArrayAddElementSized(optr arr, void *element, word size,
				 dword callbackData, EqualCB *callback)
{
    ElementArrayHeader *h = EHdr(arr);
    word fixed = h->EAH_meta.CAH_elementSize, token;
    RefElementHeader *e;

    if (fixed)
	size = fixed;
    token = Find(arr, element, size, callbackData, callback,
		 CA_NULL_ELEMENT);
    if (token != CA_NULL_ELEMENT) {
	e = Elt(arr, token, NULL);
	SetRefCount(e, RefCount(e) + 1);
	return token;
    }
    token = EHdr(arr)->EAH_freePtr;
    if (token != EA_FREE_LIST_TERMINATOR) {
	e = Elt(arr, token, NULL);
	EHdr(arr)->EAH_freePtr = (word) e->REH_refCount.WAAH_low;
	if (!fixed)
	    ChunkArrayElementResize(arr, token, size);
	e = Elt(arr, token, NULL);
    } else {
	e = ChunkArrayAppend(arr, size);
	if (!e)
	    return CA_NULL_ELEMENT;
	token = (word) (ChunkArrayGetCount(arr) - 1);
    }
    memcpy(e, element, size);
    SetRefCount(e, 1);
    return token;
}

/* The C API has no element size: the asm C stub passes none either, so
 * it serves fixed-size arrays (variable ones: ElementArrayAddElementSized,
 * as NameArrayAdd does). */
word ElementArrayAddElement(optr arr, void *element, dword callbackData,
			    Boolean (*callback)(void *elementToAdd,
						void *elementFromArray,
						dword valueForCallback))
{
    if (!EHdr(arr)->EAH_meta.CAH_elementSize)
	KernelFatal(CHUNK_ARRAY_BAD_ELEMENT);
    return ElementArrayAddElementSized(arr, element, 0, callbackData,
				       callback);
}

/* free `token' (callback first), whatever its reference count */
static void FreeElement(optr arr, word token, dword cbData,
			void (*callback)(void *element, dword cbData))
{
    RefElementHeader *e;

    if (callback)
	callback(Elt(arr, token, NULL), cbData);
    if (!EHdr(arr)->EAH_meta.CAH_elementSize)
	ChunkArrayElementResize(arr, token, sizeof(RefElementHeader));
    e = Elt(arr, token, NULL);
    e->REH_refCount.WAAH_low = (sword) EHdr(arr)->EAH_freePtr;
    e->REH_refCount.WAAH_high = EA_FREE_ELEMENT;
    EHdr(arr)->EAH_freePtr = token;
}

void ElementArrayRemoveReference(optr arr, word token, dword callbackData,
				 void (*callback)(void *element,
						  dword valueForCallback))
{
    RefElementHeader *e = Elt(arr, token, NULL);
    dword n;

    if (token >= ChunkArrayGetCount(arr) || IsFree(e))
	KernelFatal(CHUNK_ARRAY_BAD_ELEMENT);
    n = RefCount(e) - 1;
    SetRefCount(e, n);
    if (n == 0)
	FreeElement(arr, token, callbackData, callback);
}

void ElementArrayDelete(optr arr, word token)
{
    if (token >= ChunkArrayGetCount(arr) || IsFree(Elt(arr, token, NULL)))
	KernelFatal(CHUNK_ARRAY_BAD_ELEMENT);
    FreeElement(arr, token, 0, NULL);
}

word ElementArrayElementChanged(optr arr, word token, dword callbackData,
				Boolean (*callback)(void *elementChanged,
						    void *elementToCompare,
						    dword valueForCallback))
{
    word size, other;
    RefElementHeader *e = Elt(arr, token, &size);
    RefElementHeader *o;

    other = Find(arr, e, size, callbackData, callback, token);
    if (other == CA_NULL_ELEMENT)
	return token;
    /* the same as another one: it takes the references */
    e = Elt(arr, token, NULL);
    o = Elt(arr, other, NULL);
    SetRefCount(o, RefCount(o) + RefCount(e));
    FreeElement(arr, token, 0, NULL);
    return other;
}

/* the used elements the callback accepts, in token order */
typedef Boolean UsedCB(void *element, dword cbData);

static word Walk(optr arr, dword cbData, UsedCB *cb, word stopIndex,
		 word stopToken, word *found)
{
    word n = ChunkArrayGetCount(arr), i, used = 0;

    for (i = 0; i < n; i++) {
	RefElementHeader *e = Elt(arr, i, NULL);
	if (IsFree(e) || (cb && !cb(e, cbData)))
	    continue;
	if (used == stopIndex || i == stopToken) {
	    *found = stopIndex != CA_NULL_ELEMENT ? i : used;
	    return used;
	}
	used++;
    }
    *found = CA_NULL_ELEMENT;
    return used;
}

word ElementArrayGetUsedCount(optr arr, dword callbackData,
			      Boolean (*callback)(void *element,
						  dword cbData))
{
    word dummy;
    return Walk(arr, callbackData, callback, CA_NULL_ELEMENT,
		CA_NULL_ELEMENT, &dummy);
}

word ElementArrayUsedIndexToToken(optr arr, word index, dword callbackData,
				  Boolean (*callback)(void *element,
						      dword cbData))
{
    word token;
    Walk(arr, callbackData, callback, index, CA_NULL_ELEMENT, &token);
    return token;
}

word ElementArrayTokenToUsedIndex(optr arr, word token, dword callbackData,
				  Boolean (*callback)(void *element,
						      dword cbData))
{
    word index;
    Walk(arr, callbackData, callback, CA_NULL_ELEMENT, token, &index);
    return index;
}
