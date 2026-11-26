/*
 * fileMacro.h -- after fileMacro.def: the macros of the module, and
 * the helpers its files share.
 */
#ifndef H_FILEMACRO
#define H_FILEMACRO

/*
 * FEA_DISK, the handle of the disk a file is on, is the kernel's own
 * (the asm's drivers answered it from the disk they were passed): alone
 * or among FEA_MULTIPLE it is filled in here.  TRUE if that was all;
 * else *bufferP and *sizeP are what is left for the driver (a list
 * without it: in `rest', or in a block returned in *mhP to free).
 */
#define EA_REST	16

#endif /* H_FILEMACRO */
