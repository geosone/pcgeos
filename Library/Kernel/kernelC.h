/*
 * kernelC.h -- internal interfaces of the C kernel (Port32, phase P4).
 *
 * The C kernel replaces the asm kernel module by module; its files lie
 * next to the asm they replace (Library/Kernel/<Module>/<file>.c) and are
 * built by CMake only (the pmake build lists its asm sources in the
 * generated Makefile).  This header is what the C modules share; the
 * public API is CInclude (heap.h, ...).  Port32/PLAN.md P4.
 */
#ifndef KERNELC_H
#define KERNELC_H

#include <geos.h>
#include <heap.h>

/* ---------------------------------------------------------------------
 * Fatal errors.  The names are those of kernelErrors.def; the C kernel
 * reports them by name, so the values need not match the asm kernel.
 * ------------------------------------------------------------------- */
typedef enum {
    KFE_NONE = 0,
    ILLEGAL_HANDLE,
    HANDLE_TABLE_FULL,
    TOO_MANY_LOCKS,
    BAD_UNLOCK,
    HANDLE_FREE,
    NOT_MEM_HANDLE,
    ALLOC_ZERO,
    ALLOC_TOO_LARGE,
    REALLOC_TOO_LARGE,
    ALLOC_BAD_FLAGS,
    MEM_GET_INFO_BAD_PARAMETER,
    FREEING_FREE_HANDLE,
    MEM_REFERENCE_COUNT_ILLEGAL_INITIAL_VALUE,
    MEM_REFERENCE_COUNT_OVERFLOW,
    MEM_REFERENCE_COUNT_UNDERFLOW,
    CANNOT_CHANGE_OWNER,
    HAF_NO_ERR_ALLOC_FAILED,	/* HAF_NO_ERR allocation failed */
    KFE_COUNT
} KernelFatalError;

/* The name of a fatal error ("ILLEGAL_HANDLE"). */
const char *KernelFatalErrorName(KernelFatalError code);

/* Stop GEOS with a fatal error.  Tests may set kernelFatalHook to catch
 * it (the hook must not return, e.g. longjmp); otherwise the error is
 * logged and the process ends. */
typedef void KernelFatalHook(KernelFatalError code);
extern KernelFatalHook *kernelFatalHook;
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void KernelFatal(KernelFatalError code);

/* error checking (DO_ERROR_CHECKING): checks that cost time */
#if ERROR_CHECK
#define EC_CHECK(cond, code)	do { if (!(cond)) KernelFatal(code); } while (0)
#else
#define EC_CHECK(cond, code)	((void) 0)
#endif

/* ---------------------------------------------------------------------
 * The current geode: owner of new blocks.  Until threads exist, the
 * kernel itself (or a test) sets it.
 * ------------------------------------------------------------------- */
extern GeodeHandle kernelCurrentGeode;

/* ---------------------------------------------------------------------
 * Handle table (Heap/heapHandle.c, after heapHandle.asm).
 *
 * A Handle is 32 bits (Port32/ABI.md section 2): the index of the entry
 * times 16, so handles keep the alignment they had as offsets into the
 * 16-bit handle table and 0 stays the null handle.  Entries never move.
 * ------------------------------------------------------------------- */
#define HANDLE_GRANULE		16

/* Types: memory handles are HT_MEM; the others keep the signatures of
 * heapInt.def. */
enum {
    HT_FREE		= 0,
    HT_MEM		= 1,
    SIG_RESERVATION	= 0xf2,
    SIG_QUEUE		= 0xf4,
    SIG_TIMER		= 0xf6,
    SIG_EVENT_DATA	= 0xf7,
    SIG_EVENT_STACK	= 0xf8,
    SIG_EVENT_REG	= 0xf9,
    SIG_SAVED_BLOCK	= 0xfa,
    SIG_SEMAPHORE	= 0xfb,
    SIG_VM		= 0xfc,
    SIG_FILE		= 0xfd,
    SIG_THREAD		= 0xfe
};

typedef struct {
    byte	HE_type;	/* HT_* / SIG_* */
    byte	HE_flags;	/* memory: HeapFlags */
    byte	HE_lockCount;	/* memory */
    byte	HE_allocFlags;	/* memory: HAF_UI, READ_ONLY, OBJECT_RESOURCE,
				 * CODE, CONFORMING as given */
    Handle	HE_owner;	/* geode */
    Handle	HE_next;	/* free list */
    word	HE_otherInfo;	/* memory: HM_otherInfo (reference count, ...) */
    word	HE_size;	/* memory: size in bytes */
    void	*HE_addr;	/* memory: the data, NULL if discarded */
} HandleEntry;

/* Allocate a handle of the given type, owned by `owner'; the entry is
 * cleared.  0 if the table is full (HANDLE_TABLE_FULL is the caller's
 * business). */
Handle HandleAlloc(byte type, GeodeHandle owner);
void   HandleFree(Handle h);
/* The entry of a handle in use; fatal ILLEGAL_HANDLE / HANDLE_FREE. */
HandleEntry *HandleEntryOf(Handle h);
/* The entry of a memory handle; fatal also with NOT_MEM_HANDLE. */
HandleEntry *MemEntryOf(Handle h);
/* Call fn for every handle in use (in handle order); stops when fn
 * returns non-zero and returns that value. */
int HandleEnum(int (*fn)(Handle h, HandleEntry *he, void *ctx), void *ctx);
/* handles in use (statistics, tests) */
dword HandleCountUsed(void);

#endif /* KERNELC_H */
