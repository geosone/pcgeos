/*
 * dbaseMacro.h -- after dbaseMacro.def: the macros of the module, and
 * the helpers its files share.
 */
#ifndef H_DBASEMACRO
#define H_DBASEMACRO

#define GROUP_OF(id)	((VMBlockHandle) ((id) >> 16))
#define ITEM_OF(id)	((word) (id))

#endif /* H_DBASEMACRO */
