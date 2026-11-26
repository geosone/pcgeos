/*
 * lmemMacros.h -- after lmemMacros.def: macros of the LMem module.
 */
#ifndef LMEMMACROS_H
#define LMEMMACROS_H

#include "lmemConstants.h"
#include <chunkarr.h>

/* RoundUp: a size rounded up to the chunk alignment */
#define RoundUp(n)	(((n) + LMEM_ALIGN - 1) & ~(dword) (LMEM_ALIGN - 1))

/* Port32: the block header at b, and the word at offset off of it */
#define LMEM_HDR(b)	((LMemBlockHeader *) (b))
#define LMEM_WORD(b, off)	(*(word *) ((byte *) (b) + (off)))

/* Port32: short for the chunk array header */
typedef ChunkArrayHeader CAH;

#endif /* LMEMMACROS_H */
