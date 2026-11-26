/*
 * lmemConstants.h -- after lmemConstants.def: constants of the LMem
 * module (the ones of the asm that the port has).
 */
#ifndef LMEMCONSTANTS_H
#define LMEMCONSTANTS_H

/* the value of a chunk handle that has no memory (a chunk of size 0) */
#define LMEM_NOMEM_HANDLE	0xffffu

/* extra free space when a block grows (the asm's minimum padding) */
#define LMEM_MIN_ALLOCATION_PADDING	256

/* handles added to the handle table at a time */
#define LMEM_HANDLE_INCR	24

/* Port32: chunks are aligned to 8 bytes (the asm: 4, LMEM_ALIGN_SIZE),
 * so that the 32-bit structures in them are aligned; a block is at most
 * this large */
#define LMEM_ALIGN		8u
#define LMEM_MAX_BLOCK		0xfff0u

#endif /* LMEMCONSTANTS_H */
