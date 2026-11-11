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
#include <sem.h>
#include <thread.h>
#include <lmem.h>
#include <geosport.h>

/* ---------------------------------------------------------------------
 * Fatal errors.  The names are those of kernelErrors.def; the C kernel
 * reports them by name, so the values need not match the asm kernel.
 * ------------------------------------------------------------------- */
#define KERNEL_FATAL_ERRORS(X) \
    X(ILLEGAL_HANDLE) \
    X(HANDLE_TABLE_FULL) \
    X(TOO_MANY_LOCKS) \
    X(BAD_UNLOCK) \
    X(HANDLE_FREE) \
    X(NOT_MEM_HANDLE) \
    X(ALLOC_ZERO) \
    X(ALLOC_TOO_LARGE) \
    X(REALLOC_TOO_LARGE) \
    X(ALLOC_BAD_FLAGS) \
    X(MEM_GET_INFO_BAD_PARAMETER) \
    X(FREEING_FREE_HANDLE) \
    X(MEM_REFERENCE_COUNT_ILLEGAL_INITIAL_VALUE) \
    X(MEM_REFERENCE_COUNT_OVERFLOW) \
    X(MEM_REFERENCE_COUNT_UNDERFLOW) \
    X(CANNOT_CHANGE_OWNER) \
    X(HAF_NO_ERR_ALLOC_FAILED)	/* HAF_NO_ERR allocation failed */ \
    X(ILLEGAL_THREAD) \
    X(NOT_ALLOWED_TO_PSEM_IN_INTERRUPT_CODE) \
    X(NON_SEMAPHORE_PASSED_TO_SEM_ROUTINE) \
    X(THREAD_RELEASE_NOT_OWNER) \
    X(UNLOCK_MODULE_BY_WRONG_THREAD) \
    X(THREAD_GET_INFO_BAD_PARAMETER) \
    X(THREAD_CREATE_FAILED)	/* no memory for a thread */ \
    X(LMEM_CANNOT_ALLOCATE) \
    X(NOT_LMEM_HANDLE) \
    X(CORRUPTED_LMEM_BLOCK) \
    X(LMEM_HANDLE_BEFORE_HANDLE_TABLE) \
    X(LMEM_HANDLE_BEYOND_HANDLE_TABLE) \
    X(LMEM_CHUNK_HANDLE_AT_ODD_ADDRESS) \
    X(LMEM_BAD_INSERT_OFFSET) \
    X(LMEM_BAD_DELETE_OFFSET) \
    X(LMEM_BAD_DELETE_SIZE)

#define KFE_ENUM(name) name,
typedef enum {
    KFE_NONE = 0,
    KERNEL_FATAL_ERRORS(KFE_ENUM)
    KFE_COUNT
} KernelFatalError;
#undef KFE_ENUM

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

/* A semaphore (Semaphore of semInt.def): value and the queue of the
 * threads waiting on it (thread handles, best priority first). */
typedef struct {
    sword	S_value;
    Handle	S_queue;
} KSem;

typedef struct {
    byte	HE_type;	/* HT_* / SIG_* */
    byte	HE_flags;	/* memory: HeapFlags; semaphore: HSF_* */
    byte	HE_lockCount;	/* memory */
    byte	HE_allocFlags;	/* memory: HAF_UI, READ_ONLY, OBJECT_RESOURCE,
				 * CODE, CONFORMING as given */
    Handle	HE_owner;	/* geode */
    Handle	HE_next;	/* free list */
    word	HE_size;	/* memory: size in bytes */
    word	HE_nesting;	/* MemThreadGrab, thread lock: nesting */
    dword	HE_otherInfo;	/* memory: HM_otherInfo (reference count,
				 * exec thread, grabbing thread);
				 * thread lock: owner */
    KSem	HE_sem;		/* memory: HandleP/V; semaphore */
    void	*HE_addr;	/* memory: the data, NULL if discarded;
				 * thread: its KThread */
} HandleEntry;

/* HE_flags of a SIG_SEMAPHORE handle */
#define HSF_THREAD_LOCK	0x01	/* ThreadAllocThreadLock */

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

/* ---------------------------------------------------------------------
 * Threads (Thread/threadThread.c, threadSem.c; after threadThread.asm,
 * threadSem.asm).  Port32/ABI.md section 9.
 * ------------------------------------------------------------------- */
typedef struct ghal_ctx ghal_ctx;

enum { TS_READY, TS_RUNNING, TS_BLOCKED, TS_DEAD };

typedef struct {
    ghal_ctx	*T_ctx;
    Handle	T_nextQ;	/* next on the run queue or a semaphore */
    KSem	*T_waitSem;	/* semaphore waited on (TS_BLOCKED) */
    dword	T_deadline;	/* ThreadPTimedSem: tick it times out at, 0 */
    byte	T_state;	/* TS_* */
    byte	T_basePriority;
    byte	T_curPriority;
    byte	T_cpuUsage;
    byte	T_timedOut;
    word	(*T_start)(word valuePassed);
    word	T_startValue;
    word	T_exitCode;
    Handle	T_eventQueue;
} KThread;

/* the running thread (0 before KernelThreadInit) */
extern ThreadHandle kernelCurrentThread;

/* Make the caller (the program's own context) the first GEOS thread. */
ThreadHandle KernelThreadInit(ThreadPriority priority);
KThread *ThreadOf(ThreadHandle th);	/* fatal ILLEGAL_THREAD */

/* the running thread; makes the caller the first thread (priority
 * standard) if there is none yet */
ThreadHandle KernelThisThread(void);

/* Semaphore primitives (threadSem.c).  timeout in ticks: 0 does not
 * block, KSEM_FOREVER waits for good. */
#define KSEM_FOREVER	0xffffffffu
void  KSemInit(KSem *s, sword value);
SemaphoreError KSemP(KSem *s, dword timeout);
void  KSemV(KSem *s);
/* scheduler side (threadThread.c), inside ghal_crit: block the running
 * thread on s->S_queue (returns non-zero if it timed out); wake the best
 * thread of s->S_queue */
int   ThreadBlockOn(KSem *s, dword timeout);
void  ThreadWakeOne(KSem *s);
/* let other threads of the same priority run */
void  KernelYield(void);
/* ticks (1/60 s) since the start, from the GHAL clock */
dword KernelTicks(void);

/* ---------------------------------------------------------------------
 * LMem (LMem/lmemCode.c, after lmemCode.asm).  Port32/ABI.md section 10.
 * ------------------------------------------------------------------- */
/* Check the structure of an LMem block (fatal CORRUPTED_LMEM_BLOCK). */
void LMemValidate(MemHandle mh);
/* Build the LMem block of a resource made by goc -cg (header, handle
 * table, chunks in handle order).  Optrs in it stay unrelocated. */
MemHandle LMemCreateResourceBlock(const GocResource *r, GeodeHandle owner,
				  HeapFlags hf, HeapAllocFlags haf);

#endif /* KERNELC_H */
