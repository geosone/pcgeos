/*
 * lmemC.c -- after lmemC.asm: ChunkArrayCreateAt, LMemGetChunkSize,
 * ElementArrayCreateAt, NameArrayCreateAt.
 */
#include "kernelC.h"
#include <chunkarr.h>
#include <stdlib.h>
#include <string.h>
#include "ghal.h"
#include <object.h>

typedef ChunkArrayHeader CAH;

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
    h = LMemCArrayHdr(arr);
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

#define LMEM_EMPTY	0xffffu		/* LMEM_NOMEM_HANDLE */
#define W(b, off)	(*(word *) ((byte *) (b) + (off)))

word LMemGetChunkSize(optr o)
{
    byte *b = LMemBlockOf(OptrToHandle(o));
    word v = *LMemEntryOf(b, OptrToChunk(o));

    return v == LMEM_EMPTY ? 0 : (word) (W(b, v - 2) - 2);
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
	LMemEArrayHdr(ConstructOptr(OptrToHandle(arr), ch))->EAH_freePtr =
	    EA_FREE_LIST_TERMINATOR;
    return ch;
}

ChunkHandle NameArrayCreateAt(optr arr, word dataSize, word headerSize,
			      ObjChunkFlags ocf)
{
    ChunkHandle ch;

    if (headerSize == 0)
	headerSize = sizeof(NameArrayHeader);
    if (headerSize < sizeof(NameArrayHeader))
	KernelFatal(CHUNK_ARRAY_HEADER_SIZE_TOO_SMALL);
    ch = ElementArrayCreateAt(arr, 0, headerSize, ocf);
    if (ch)
	((NameArrayHeader *) LMemDeref(ConstructOptr(OptrToHandle(arr), ch)))
	    ->NAH_dataSize = dataSize;
    return ch;
}
