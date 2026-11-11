/*
 * lmemCode.c -- local memory heaps of the C kernel (CInclude/lmem.h;
 * after lmemCode.asm).
 *
 * An LMem block, as the asm kernel has it:
 *
 *   header         LMemBlockHeader (or a larger one: ObjLMemBlockHeader)
 *   handle table   at LMBH_offset, LMBH_nHandles words; a chunk handle is
 *                  the offset of its entry.  Entry 0: free handle,
 *                  LMEM_EMPTY: chunk without data, else the offset of the
 *                  chunk's data.
 *   heap           chunks, each a size word (data size + 2) right before
 *                  its data.
 *
 * Port32 differences: chunk data is aligned to 8 (objects hold pointers
 * and 64-bit optrs), so every chunk occupies a multiple of 8 bytes
 * including its size word, and the heap starts where the first chunk's
 * data is 8-aligned.  A free chunk's size word is its whole slot; free
 * chunks are linked through their first word (LMBH_freeList), sorted by
 * address, and merged with free neighbours.  Blocks stay below 64K.
 *
 * Chunks move inside the block (compaction) and the block may move when
 * it grows: after LMemAlloc, LMemReAlloc, LMemInsertAt the caller
 * dereferences again, as in the asm kernel.  The block must not be
 * swapped out (it cannot be in Port32) but need not be locked.
 *
 * Not yet: LMF_NO_HANDLES (DB items), LMemSplitBlock, object block
 * relocation (the object system does that).
 */
#include "kernelC.h"
#include "ghal.h"

#include <object.h>
#include <stdlib.h>
#include <string.h>

#define LMEM_EMPTY	0xffffu		/* LMEM_NOMEM_HANDLE */
#define LMEM_ALIGN	8u
#define ALIGN(n)	(((n) + LMEM_ALIGN - 1) & ~(dword) (LMEM_ALIGN - 1))
#define LMEM_MAX_BLOCK	0xfff0u
#define LMEM_HANDLE_INCR 24		/* handles added at a time */
#define LMEM_PADDING	256		/* extra free space when growing */

#define HDR(b)		((LMemBlockHeader *) (b))
#define W(b, off)	(*(word *) ((byte *) (b) + (off)))

/* offset of the first chunk's size word */
static dword HeapStart(const byte *b)
{
    dword tableEnd = HDR(b)->LMBH_offset + 2u * HDR(b)->LMBH_nHandles;
    return ALIGN(tableEnd + 2) - 2;
}

/* bytes a chunk occupies, from its data offset */
static dword SlotOf(const byte *b, word data)
{
    return ALIGN((dword) W(b, data - 2));
}

static byte *BlockOf(MemHandle mh)
{
    HandleEntry *he = MemEntryOf(mh);
    byte *b = he->HE_addr;

    if (!b || !(he->HE_flags & HF_LMEM))
	KernelFatal(NOT_LMEM_HANDLE);
    return b;
}

/* the handle table entry of a chunk handle in use */
static word *EntryOf(byte *b, ChunkHandle ch)
{
    LMemBlockHeader *h = HDR(b);

    if (ch < h->LMBH_offset)
	KernelFatal(LMEM_HANDLE_BEFORE_HANDLE_TABLE);
    if (ch >= h->LMBH_offset + 2u * h->LMBH_nHandles)
	KernelFatal(LMEM_HANDLE_BEYOND_HANDLE_TABLE);
    if ((ch - h->LMBH_offset) & 1)
	KernelFatal(LMEM_CHUNK_HANDLE_AT_ODD_ADDRESS);
    if (W(b, ch) == 0)
	KernelFatal(ILLEGAL_HANDLE);	/* freed chunk */
    return &W(b, ch);
}

/* ---------------------------------------------------------------------
 * Free chunks
 * ------------------------------------------------------------------- */

/* give [data - 2, data - 2 + slot) back, merging with its neighbours */
static void FreeSlot(byte *b, word data, dword slot)
{
    LMemBlockHeader *h = HDR(b);
    word *link = &h->LMBH_freeList, prev = 0;

    W(b, data - 2) = (word) slot;
    while (*link && *link < data) {
	prev = *link;
	link = &W(b, *link);
    }
    W(b, data) = *link;			/* insert, sorted */
    *link = data;
    h->LMBH_totalFree += (word) slot;
    /* merge with the next one */
    if (W(b, data) && data + slot == W(b, data)) {
	word next = W(b, data);
	slot += W(b, next - 2);
	W(b, data) = W(b, next);
	W(b, data - 2) = (word) slot;
    }
    /* merge into the previous one */
    if (prev && prev + (dword) W(b, prev - 2) == data) {
	W(b, prev - 2) = (word) (W(b, prev - 2) + slot);
	W(b, prev) = W(b, data);
    }
}

/* take `need' bytes from the free chunk at *link (its slot >= need) */
static word TakeFree(byte *b, word *link, dword need)
{
    word data = *link;
    dword slot = W(b, data - 2);

    *link = W(b, data);
    HDR(b)->LMBH_totalFree -= (word) slot;
    if (slot > need)
	FreeSlot(b, (word) (data + need), slot - need);
    return data;
}

/* Move all chunks to the start of the heap, in address order; the free
 * space becomes one chunk at the end. */
static int CompareDwords(const void *x, const void *y)
{
    dword a = *(const dword *) x, c = *(const dword *) y;
    return a < c ? -1 : a > c;
}

static void Compact(byte *b)
{
    LMemBlockHeader *h = HDR(b);
    word n = h->LMBH_nHandles, i, count = 0;
    dword pos = HeapStart(b), end = h->LMBH_blockSize;
    word *table = &W(b, h->LMBH_offset);
    dword *order;

    if (h->LMBH_freeList == 0)
	return;				/* nothing to gain */
    order = ghal_mem_alloc(n ? n * sizeof(dword) : 1, 0);
    if (!order)
	return;				/* compaction is only an optimization */
    for (i = 0; i < n; i++)
	if (table[i] != 0 && table[i] != LMEM_EMPTY)
	    order[count++] = ((dword) table[i] << 16) | i;
    qsort(order, count, sizeof(dword), CompareDwords);
    for (i = 0; i < count; i++) {
	word v = (word) (order[i] >> 16), idx = (word) order[i];
	dword slot = SlotOf(b, v);
	if (v - 2u != pos)
	    memmove(b + pos, b + v - 2, slot);
	table[idx] = (word) (pos + 2);
	pos += slot;
    }
    ghal_mem_free(order);
    h->LMBH_freeList = 0;
    h->LMBH_totalFree = 0;
    if (pos < end)
	FreeSlot(b, (word) (pos + 2), end - pos);
}

/* ---------------------------------------------------------------------
 * Getting space
 * ------------------------------------------------------------------- */

/* Resize the block by `delta' bytes at its end (a multiple of 8, may be
 * negative); the new space is not entered anywhere.  0 if it cannot. */
static byte *ResizeBlock(MemHandle mh, sdword delta)
{
    byte *b = BlockOf(mh);
    dword size = HDR(b)->LMBH_blockSize + delta;

    if (delta > 0 && ((HDR(b)->LMBH_flags & LMF_NO_ENLARGE) ||
		      size > LMEM_MAX_BLOCK))
	return NULL;
    if (MemReAlloc(mh, (word) size, 0) == 0)
	return NULL;
    b = MemDeref(mh);
    HDR(b)->LMBH_blockSize = (word) size;
    return b;
}

/* a free chunk of at least `need' bytes (slot size), first fit */
static word *FindFree(byte *b, dword need)
{
    word *link = &HDR(b)->LMBH_freeList;

    while (*link) {
	if (W(b, *link - 2) >= need)
	    return link;
	link = &W(b, *link);
    }
    return NULL;
}

/* Space for a chunk of `size' bytes: the data offset (size word set), 0
 * if there is none.  May compact the heap and move the block. */
static word AllocSpace(MemHandle mh, word size)
{
    byte *b = BlockOf(mh);
    dword need = ALIGN((dword) size + 2);
    word *link, data;

    link = FindFree(b, need);
    if (!link && HDR(b)->LMBH_totalFree >= need) {
	Compact(b);
	link = FindFree(b, need);
    }
    if (!link) {
	/* grow: what the last free chunk (at the end) lacks, and some */
	dword have = 0, grow;
	word f;
	for (f = HDR(b)->LMBH_freeList; f; f = W(b, f))
	    if (f - 2u + W(b, f - 2) == HDR(b)->LMBH_blockSize)
		have = W(b, f - 2);
	grow = need - have + LMEM_PADDING;
	if (HDR(b)->LMBH_blockSize + grow > LMEM_MAX_BLOCK)
	    grow = need - have;
	{
	    dword oldEnd = HDR(b)->LMBH_blockSize;
	    b = ResizeBlock(mh, (sdword) grow);
	    if (!b)
		return 0;
	    FreeSlot(b, (word) (oldEnd + 2), grow);
	}
	link = FindFree(b, need);
	if (!link)
	    KernelFatal(CORRUPTED_LMEM_BLOCK);
    }
    data = TakeFree(b, link, need);
    W(b, data - 2) = (word) (size + 2);
    return data;
}

/* add `count' handles (zero: free); moves the heap up */
static int MoreHandles(MemHandle mh, word count)
{
    byte *b = BlockOf(mh);
    LMemBlockHeader *h = HDR(b);
    dword oldStart = HeapStart(b), newStart, delta, oldEnd = h->LMBH_blockSize;
    word i, oldN = h->LMBH_nHandles, *table;

    h->LMBH_nHandles = (word) (oldN + count);
    newStart = HeapStart(b);
    h->LMBH_nHandles = oldN;
    delta = newStart - oldStart;
    if (delta) {
	b = ResizeBlock(mh, (sdword) delta);
	if (!b)
	    return 0;
	h = HDR(b);
	memmove(b + newStart, b + oldStart, oldEnd - oldStart);
	table = &W(b, h->LMBH_offset);
	for (i = 0; i < oldN; i++)
	    if (table[i] != 0 && table[i] != LMEM_EMPTY)
		table[i] = (word) (table[i] + delta);
	if (h->LMBH_freeList) {
	    word *link = &h->LMBH_freeList;
	    while (*link) {
		*link = (word) (*link + delta);
		link = &W(b, *link);
	    }
	}
    }
    table = &W(b, h->LMBH_offset);
    for (i = oldN; i < oldN + count; i++)
	table[i] = 0;
    h->LMBH_nHandles = (word) (oldN + count);
    /* the flags chunk has a byte per handle */
    if (h->LMBH_flags & LMF_HAS_FLAGS) {
	ChunkHandle fc = h->LMBH_offset;
	word old = LMemGetChunkSize(ConstructOptr(mh, fc));
	if (old < h->LMBH_nHandles &&
	    LMemReAlloc(ConstructOptr(mh, fc), h->LMBH_nHandles))
	    return 0;
	b = BlockOf(mh);
	if (old < HDR(b)->LMBH_nHandles)
	    memset(b + W(b, fc) + old, 0, HDR(b)->LMBH_nHandles - old);
    }
    return 1;
}

static void Failed(MemHandle mh)
{
    if (!(HDR(BlockOf(mh))->LMBH_flags & LMF_RETURN_ERRORS))
	KernelFatal(LMEM_CANNOT_ALLOCATE);
}

/* ---------------------------------------------------------------------
 * The API
 * ------------------------------------------------------------------- */

void LMemInitHeap(MemHandle mh, LMemType type, LocalMemoryFlags flags,
		  word lmemOffset, word numHandles, word freeSpace)
{
    byte *b;
    LMemBlockHeader *h;
    dword offset = (lmemOffset + 3u) & ~3u, start, size, i;

    if (offset < sizeof(LMemBlockHeader))
	offset = (sizeof(LMemBlockHeader) + 3u) & ~3u;
    start = ALIGN(offset + 2u * numHandles + 2) - 2;
    size = start + ALIGN(freeSpace ? freeSpace : LMEM_ALIGN);
    if (size > LMEM_MAX_BLOCK || !MemReAlloc(mh, (word) size, 0))
	KernelFatal(LMEM_CANNOT_ALLOCATE);
    MemModifyFlags(mh, HF_LMEM, 0);
    b = MemDeref(mh);
    h = HDR(b);
    h->LMBH_handle = mh;
    h->LMBH_offset = (word) offset;
    h->LMBH_flags = flags;
    h->LMBH_lmemType = type;
    h->LMBH_blockSize = (word) size;
    h->LMBH_nHandles = numHandles;
    h->LMBH_freeList = 0;
    h->LMBH_totalFree = 0;
    for (i = 0; i < numHandles; i++)
	W(b, offset + 2 * i) = 0;
    memset(b + offset + 2u * numHandles, 0, start - offset - 2u * numHandles);
    FreeSlot(b, (word) (start + 2), size - start);
    if ((flags & LMF_HAS_FLAGS) && numHandles) {
	/* handle 0: a byte of ObjChunkFlags per handle */
	word data = AllocSpace(mh, numHandles);
	if (!data)
	    KernelFatal(LMEM_CANNOT_ALLOCATE);
	b = MemDeref(mh);
	W(b, offset) = data;
	memset(b + data, 0, numHandles);
    }
}

MemHandle MemAllocLMem(LMemType type, word headerSize)
{
    /* zeroed: the rest of a larger header (ObjLMemBlockHeader: in-use
     * count, output) starts at 0 */
    MemHandle mh = MemAlloc(128, HF_SWAPABLE, HAF_NO_ERR | HAF_ZERO_INIT);
    LocalMemoryFlags flags = 0;
    word def = sizeof(LMemBlockHeader);

    if (type == LMEM_TYPE_OBJ_BLOCK) {
	MemModifyOtherInfo(mh, KernelThisThread());	/* exec thread */
	flags = LMF_HAS_FLAGS | LMF_RELOCATED;
	def = sizeof(ObjLMemBlockHeader);
    }
    LMemInitHeap(mh, type, flags, headerSize ? headerSize : def, 2, 64);
    return mh;
}

static ChunkHandle AllocHandle(MemHandle mh, word i, word chunkSize);

ChunkHandle LMemAlloc(MemHandle mh, word chunkSize)
{
    byte *b = BlockOf(mh);
    LMemBlockHeader *h = HDR(b);
    word i;

    for (i = 0; i < h->LMBH_nHandles; i++)
	if (W(b, h->LMBH_offset + 2 * i) == 0)
	    break;
    if (i == h->LMBH_nHandles) {
	if (!MoreHandles(mh, LMEM_HANDLE_INCR)) {
	    Failed(mh);
	    return 0;
	}
    }
    return AllocHandle(mh, i, chunkSize);
}

/* LMemAllocHere: a chunk with the free handle `ch' (state, objectFile.c) */
ChunkHandle LMemAllocAt(MemHandle mh, ChunkHandle ch, word chunkSize)
{
    byte *b = BlockOf(mh);
    LMemBlockHeader *h = HDR(b);
    word i;

    if (ch < h->LMBH_offset || (ch - h->LMBH_offset) & 1)
	KernelFatal(LMEM_HANDLE_BEFORE_HANDLE_TABLE);
    i = (word) ((ch - h->LMBH_offset) / 2);
    if (i >= h->LMBH_nHandles &&
	!MoreHandles(mh, (word) (i + 1 - h->LMBH_nHandles))) {
	Failed(mh);
	return 0;
    }
    b = BlockOf(mh);
    if (W(b, ch) != 0)
	KernelFatal(CANNOT_ALLOCATE_CORRECT_CHUNK_HANDLE);
    return AllocHandle(mh, i, chunkSize);
}

static ChunkHandle AllocHandle(MemHandle mh, word i, word chunkSize)
{
    byte *b = BlockOf(mh);
    LMemBlockHeader *h = HDR(b);
    word data;
    ChunkHandle ch;

    ch = (ChunkHandle) (h->LMBH_offset + 2 * i);
    W(b, ch) = LMEM_EMPTY;		/* taken, even while we compact */
    if (chunkSize) {
	data = AllocSpace(mh, chunkSize);
	b = BlockOf(mh);
	if (!data) {
	    W(b, ch) = 0;
	    Failed(mh);
	    return 0;
	}
	W(b, ch) = data;
    }
    if (HDR(b)->LMBH_flags & LMF_HAS_FLAGS)
	b[W(b, HDR(b)->LMBH_offset) + i] = 0;	/* no ObjChunkFlags */
    return ch;
}

void *LMemDeref(optr o)
{
    byte *b = BlockOf(OptrToHandle(o));
    word v = *EntryOf(b, OptrToChunk(o));

    return v == LMEM_EMPTY ? NULL : b + v;
}

void LMemFree(optr o)
{
    byte *b = BlockOf(OptrToHandle(o));
    ChunkHandle ch = OptrToChunk(o);
    word *e = EntryOf(b, ch), v = *e;

    if (v != LMEM_EMPTY)
	FreeSlot(b, v, SlotOf(b, v));
    *e = 0;
    if (HDR(b)->LMBH_flags & LMF_HAS_FLAGS)
	b[W(b, HDR(b)->LMBH_offset) + (ch - HDR(b)->LMBH_offset) / 2] = 0;
}

word LMemGetChunkSize(optr o)
{
    byte *b = BlockOf(OptrToHandle(o));
    word v = *EntryOf(b, OptrToChunk(o));

    return v == LMEM_EMPTY ? 0 : (word) (W(b, v - 2) - 2);
}

/* TRUE if it failed (LMF_RETURN_ERRORS), as the asm C stub */
Boolean LMemReAlloc(optr o, word chunkSize)
{
    MemHandle mh = OptrToHandle(o);
    ChunkHandle ch = OptrToChunk(o);
    byte *b = BlockOf(mh);
    word v = *EntryOf(b, ch), data;
    dword slot, need = ALIGN((dword) chunkSize + 2);

    if (chunkSize == 0) {
	if (v != LMEM_EMPTY)
	    FreeSlot(b, v, SlotOf(b, v));
	W(b, ch) = LMEM_EMPTY;
	return FALSE;
    }
    if (v != LMEM_EMPTY) {
	slot = SlotOf(b, v);
	if (need <= slot) {			/* shrink or same slot */
	    W(b, v - 2) = (word) (chunkSize + 2);
	    if (slot > need)
		FreeSlot(b, (word) (v + need), slot - need);
	    return FALSE;
	}
	/* grow into a free neighbour */
	{
	    word *link = &HDR(b)->LMBH_freeList;
	    while (*link && *link < v + slot)
		link = &W(b, *link);
	    if (*link == v + slot && slot + W(b, *link - 2) >= need) {
		dword both = slot + W(b, *link - 2);
		HDR(b)->LMBH_totalFree -= W(b, *link - 2);
		*link = W(b, *link);
		W(b, v - 2) = (word) (chunkSize + 2);
		if (both > need)
		    FreeSlot(b, (word) (v + need), both - need);
		return FALSE;
	    }
	}
    }
    /* somewhere else: the handle keeps the old chunk while we look, so
     * compaction moves it along */
    data = AllocSpace(mh, chunkSize);
    if (!data) {
	Failed(mh);
	return TRUE;
    }
    b = BlockOf(mh);
    v = W(b, ch);
    if (v != LMEM_EMPTY) {
	memcpy(b + data, b + v, W(b, v - 2) - 2u);
	FreeSlot(b, v, SlotOf(b, v));
    }
    W(b, ch) = data;
    return FALSE;
}

Boolean LMemInsertAt(optr o, word insertOffset, word insertCount)
{
    word size = LMemGetChunkSize(o);
    byte *p;

    if (insertOffset > size)
	KernelFatal(LMEM_BAD_INSERT_OFFSET);
    if (LMemReAlloc(o, (word) (size + insertCount)))
	return TRUE;
    p = LMemDeref(o);
    memmove(p + insertOffset + insertCount, p + insertOffset,
	    size - insertOffset);
    memset(p + insertOffset, 0, insertCount);
    return FALSE;
}

void LMemDeleteAt(optr o, word deleteOffset, word deleteCount)
{
    word size = LMemGetChunkSize(o);
    byte *p;

    if (deleteOffset > size)
	KernelFatal(LMEM_BAD_DELETE_OFFSET);
    if (deleteCount > size - deleteOffset)
	KernelFatal(LMEM_BAD_DELETE_SIZE);
    p = LMemDeref(o);
    if (p)
	memmove(p + deleteOffset, p + deleteOffset + deleteCount,
		size - deleteOffset - deleteCount);
    LMemReAlloc(o, (word) (size - deleteCount));
}

void LMemContract(MemHandle mh)
{
    byte *b = BlockOf(mh);
    word f = 0;

    Compact(b);
    f = HDR(b)->LMBH_freeList;		/* one chunk at the end, or none */
    if (f && W(b, f - 2) > LMEM_ALIGN) {
	dword cut = W(b, f - 2) - LMEM_ALIGN;	/* keep a minimal one */
	W(b, f - 2) = LMEM_ALIGN;
	HDR(b)->LMBH_totalFree = LMEM_ALIGN;
	ResizeBlock(mh, -(sdword) cut);
    }
}

/* ---------------------------------------------------------------------
 * Checking, resources
 * ------------------------------------------------------------------- */

void LMemValidate(MemHandle mh)
{
    byte *b = BlockOf(mh);
    LMemBlockHeader *h = HDR(b);
    dword start = HeapStart(b), end = h->LMBH_blockSize, total = 0, used = 0;
    word i, f, prev = 0;

    if (h->LMBH_handle != mh || end != MemGetInfo(mh, MGIT_SIZE) ||
	start > end || (end - start) % LMEM_ALIGN)
	KernelFatal(CORRUPTED_LMEM_BLOCK);
    for (f = h->LMBH_freeList; f; f = W(b, f)) {
	dword slot = W(b, f - 2);
	if (f <= prev || f - 2u < start || f - 2u + slot > end ||
	    slot % LMEM_ALIGN || slot == 0 ||
	    (prev && prev - 2u + W(b, prev - 2) >= f - 2u))  /* unmerged */
	    KernelFatal(CORRUPTED_LMEM_BLOCK);
	total += slot;
	prev = f;
    }
    if (total != h->LMBH_totalFree)
	KernelFatal(CORRUPTED_LMEM_BLOCK);
    for (i = 0; i < h->LMBH_nHandles; i++) {
	word v = W(b, h->LMBH_offset + 2 * i);
	if (v == 0 || v == LMEM_EMPTY)
	    continue;
	if (v % LMEM_ALIGN || v - 2u < start || v - 2u + SlotOf(b, v) > end)
	    KernelFatal(CORRUPTED_LMEM_BLOCK);
	used += SlotOf(b, v);
    }
    if (used + total != end - start)	/* no overlap, nothing lost */
	KernelFatal(CORRUPTED_LMEM_BLOCK);
}

MemHandle LMemCreateResourceBlock(const GocResource *r, GeodeHandle owner,
				  HeapFlags hf, HeapAllocFlags haf)
{
    const LMemBlockHeader *src = r->GR_header;
    LMemBlockHeader tmp;
    dword start, size, pos, i;
    MemHandle mh;
    byte *b;

    if (r->GR_headerSize < sizeof(LMemBlockHeader) ||
	src->LMBH_offset < r->GR_headerSize ||
	src->LMBH_nHandles < r->GR_chunks)
	KernelFatal(CORRUPTED_LMEM_BLOCK);
    tmp = *src;
    start = HeapStart((const byte *) &tmp);
    size = start;
    for (i = 0; i < r->GR_chunks; i++) {
	dword n = r->GR_chunk[i].GC_size + r->GR_chunk[i].GC_size2;
	if (n)
	    size += ALIGN(n + 2);
    }
    size += LMEM_ALIGN;				/* a free chunk */
    if (size > LMEM_MAX_BLOCK)
	KernelFatal(LMEM_CANNOT_ALLOCATE);
    mh = MemAllocSetOwner(owner, (word) size, (HeapFlags) (hf | HF_LMEM),
			  (HeapAllocFlags) (haf | HAF_ZERO_INIT));
    if (!mh)
	return 0;
    b = MemDeref(mh);
    memcpy(b, src, r->GR_headerSize);
    HDR(b)->LMBH_handle = mh;
    HDR(b)->LMBH_blockSize = (word) size;
    HDR(b)->LMBH_freeList = 0;
    HDR(b)->LMBH_totalFree = 0;
    pos = start;
    for (i = 0; i < r->GR_chunks; i++) {
	const GocChunk *c = &r->GR_chunk[i];
	dword n = c->GC_size + c->GC_size2;
	word *e = &W(b, src->LMBH_offset + 2 * i);
	if (n == 0) {
	    *e = LMEM_EMPTY;
	    continue;
	}
	W(b, pos) = (word) (n + 2);
	memcpy(b + pos + 2, c->GC_data, c->GC_size);
	if (c->GC_size2)
	    memcpy(b + pos + 2 + c->GC_size, c->GC_data2, c->GC_size2);
	*e = (word) (pos + 2);
	pos += ALIGN(n + 2);
    }
    FreeSlot(b, (word) (pos + 2), size - pos);
    return mh;
}

/* ---------------------------------------------------------------------
 * LMem blocks in files: the DOS16 layout (VM files, Port32/ABI.md 13b)
 *
 *   LMemBlockHeader   16 bytes: handle, offset, flags, type, blockSize,
 *                     nHandles, freeList, totalFree (words)
 *   rest of a larger  the bytes of the Port32 header after its own
 *   header            LMemBlockHeader, as they are (a converter per type
 *                     where they differ: section 14)
 *   handle table      at LMBH_offset = Port32 LMBH_offset - 4
 *   heap              the chunks in handle order right after the table,
 *                     each a size word (data size + 2) and its data in a
 *                     slot rounded up to 4; no free chunk
 *
 * The chunk handles of the file are 4 lower than in memory (the Port32
 * header is 20 bytes): chunk handles inside chunk data are not changed.
 * ------------------------------------------------------------------- */

#define DOS16_LMBH_SIZE	16u
#define LMBH_DELTA	((word) (sizeof(LMemBlockHeader) - DOS16_LMBH_SIZE))
#define ALIGN4(n)	(((n) + 3u) & ~(dword) 3u)

static void Put16(byte *p, word v)
{
    p[0] = (byte) v;
    p[1] = (byte) (v >> 8);
}

static word Get16(const byte *p)
{
    return (word) (p[0] | (p[1] << 8));
}

word LMemDOS16Size(MemHandle mh)
{
    const byte *b = MemDeref(mh);
    const LMemBlockHeader *h = HDR(b);
    dword size = h->LMBH_offset - LMBH_DELTA + 2u * h->LMBH_nHandles, i;

    for (i = 0; i < h->LMBH_nHandles; i++) {
	word e = W(b, h->LMBH_offset + 2 * i);
	if (e && e != LMEM_EMPTY)
	    size += ALIGN4(W(b, e - 2));
    }
    return size > LMEM_MAX_BLOCK ? 0 : (word) size;
}

word LMemToDOS16(MemHandle mh, byte *out)
{
    const byte *b = MemDeref(mh);
    const LMemBlockHeader *h = HDR(b);
    word off = (word) (h->LMBH_offset - LMBH_DELTA), n = h->LMBH_nHandles;
    dword pos = off + 2u * n, i;

    memcpy(out + DOS16_LMBH_SIZE, b + sizeof(LMemBlockHeader),
	   off - DOS16_LMBH_SIZE);
    for (i = 0; i < n; i++) {
	word e = W(b, h->LMBH_offset + 2 * i), sz;
	if (!e || e == LMEM_EMPTY) {
	    Put16(out + off + 2 * i, e);
	    continue;
	}
	sz = W(b, e - 2);
	Put16(out + pos, sz);
	memcpy(out + pos + 2, b + e, sz - 2u);
	memset(out + pos + sz, 0, ALIGN4(sz) - sz);
	Put16(out + off + 2 * i, (word) (pos + 2));
	pos += ALIGN4(sz);
    }
    Put16(out, 0);			/* the handle means nothing in a file */
    Put16(out + 2, off);
    Put16(out + 4, h->LMBH_flags);
    Put16(out + 6, (word) h->LMBH_lmemType);
    Put16(out + 8, (word) pos);
    Put16(out + 10, n);
    Put16(out + 12, 0);
    Put16(out + 14, 0);
    return (word) pos;
}

MemHandle LMemFromDOS16(const byte *img, word size, Handle owner)
{
    word off, n, flags, i;
    dword memOff, start, total, tableEnd;
    MemHandle mh;
    byte *b;
    LMemBlockHeader *h;

    if (size < DOS16_LMBH_SIZE)
	return 0;
    off = Get16(img + 2);
    flags = Get16(img + 4);
    n = Get16(img + 10);
    tableEnd = off + 2u * n;
    if (off < DOS16_LMBH_SIZE || (off & 1) || tableEnd > size ||
	(flags & LMF_NO_HANDLES))
	return 0;
    memOff = off + LMBH_DELTA;
    start = ALIGN(memOff + 2u * n + 2) - 2;
    total = start + LMEM_ALIGN;		/* a free chunk at the end */
    for (i = 0; i < n; i++) {
	word e = Get16(img + off + 2 * i), sz;
	if (!e || e == LMEM_EMPTY)
	    continue;
	if (e < tableEnd + 2 || e > size)
	    return 0;
	sz = Get16(img + e - 2);
	if (sz < 2 || (dword) e - 2 + sz > size)
	    return 0;
	total += ALIGN(sz);
    }
    if (total > LMEM_MAX_BLOCK)
	return 0;
    mh = MemAllocSetOwner(owner, (word) total, HF_SWAPABLE | HF_LMEM,
			  HAF_ZERO_INIT);
    if (!mh)
	return 0;
    b = MemDeref(mh);
    h = HDR(b);
    memcpy(b + sizeof(LMemBlockHeader), img + DOS16_LMBH_SIZE,
	   off - DOS16_LMBH_SIZE);
    h->LMBH_handle = mh;
    h->LMBH_offset = (word) memOff;
    h->LMBH_flags = flags;
    h->LMBH_lmemType = (LMemType) Get16(img + 6);
    h->LMBH_blockSize = (word) total;
    h->LMBH_nHandles = n;
    h->LMBH_freeList = 0;
    h->LMBH_totalFree = 0;
    start = HeapStart(b);
    for (i = 0; i < n; i++) {
	word e = Get16(img + off + 2 * i), sz;
	if (!e || e == LMEM_EMPTY) {
	    W(b, memOff + 2 * i) = e;
	    continue;
	}
	sz = Get16(img + e - 2);
	W(b, start) = sz;
	memcpy(b + start + 2, img + e, sz - 2u);
	W(b, memOff + 2 * i) = (word) (start + 2);
	start += ALIGN(sz);
    }
    FreeSlot(b, (word) (start + 2), total - start);
    return mh;
}
