/*
 * objectConstant.h -- after objectConstant.def: the constants of the module
 * that the port has.
 */
#ifndef H_OBJECTCONSTANT
#define H_OBJECTCONSTANT

#define VM_EXTRA_DATA		0xfffc	/* ObjSaveExtraStateBlock */
#define VM_SAVED_APP		0xfffd	/* run by a thread of the process */
/* VM user IDs of the state file (objectConstant.def) */
#define VM_SAVED_ATTACHED	0xffff	/* block is in memory */
#define VM_SAVED_UI		0xfffe	/* run by the UI thread */

#endif /* H_OBJECTCONSTANT */
