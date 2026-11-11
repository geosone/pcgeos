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
#include <chunkarr.h>
#include <geode.h>
#include <timer.h>
#include <object.h>
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
    X(LMEM_BAD_DELETE_SIZE) \
    X(CHUNK_ARRAY_BAD_ELEMENT) \
    X(CHUNK_ARRAY_HEADER_SIZE_TOO_SMALL) \
    X(CANNOT_SORT_VARIABLE_SIZED_CHUNK_ARRAYS) \
    X(CANNOT_RESIZE_FIXED_SIZE_ELEMENT) \
    X(NULL_ELEMENT_PASSED_TO_CHUNK_ARRAY_ROUTINE) \
    X(BAD_TIMER_TYPE) \
    X(NOT_AN_EVENT_HANDLE) \
    X(NOT_A_QUEUE_HANDLE) \
    X(OUT_OF_HANDLES) \
    X(QUEUE_FLUSH_TOO_LARGE) \
    X(NO_THREAD_FOR_DESTINATION) \
    X(NO_QUEUE_FOR_DESTINATION) \
    X(ILLEGAL_DESTINATION) \
    X(MF_CUSTOM_NOT_SUPPORTED) \
    X(CANNOT_HAVE_BOTH_MF_FORCE_QUEUE_AND_MF_CALL) \
    X(VARIANT_SUPERCLASS_NOT_RESOLVED) \
    X(OBJ_BAD_MASTER_PARTS) \
    X(NOT_AN_OBJECT) \
    X(NOT_OBJECT_BLOCK) \
    X(NESTED_FREE_OBJ_BLOCK) \
    X(OVS_SIZE_OF_VAR_DATA_ENTRIES_GREATER_THAN_VAR_DATA_AREA) \
    X(OVS_BAD_MSG_META_INITIALIZE_VAR_DATA_HANDLER) \
    X(BAD_RELOCATION_TYPE) \
    X(ILLEGAL_VARDATA_RELOCATION_OFFSET) \
    X(ERROR_RETURNED_BY_RELOCATION_HANDLER) \
    X(CANNOT_RELOCATE) \
    X(CANNOT_UNRELOCATE) \
    X(ADD_CHILD_OF_OBJ_ALREADY_IN_COMPOSITE) \
    X(BAD_OBJ_COMP_CALL_TYPE) \
    X(GCN_LIST_ERROR) \
    X(BAD_DETACH_MSG) \
    X(OBJ_SHUTDOWN_OR_DETACH_WHILE_OTHER_STILL_ACTIVE) \
    X(OBJ_DETACH_NOT_STARTED)

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
    ClassStruct	*T_class;	/* ThreadAttachToQueue: for messages to
				 * the thread, its queue or its process */
    MemHandle	T_dynamicObjBlock;	/* ObjInstantiateForThread */
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
/* Set from interrupt code when another thread should run (time slice
 * over, a better thread woke up); KernelPreemptPoint, called at kernel
 * entry points, switches then.  Port32/GHAL-Concept.md 4.1. */
extern volatile int kernelReschedulePending;
void  KernelPreemptPoint(void);
/* from the timer interrupt (Timer/timerInt.c), inside ghal_crit: CPU
 * usage and time slice of the running thread, timeouts; every second:
 * halve the CPU usage of all threads */
void  ThreadTick(void);
void  ThreadDecay(void);
/* ticks (1/60 s) since the start, from the GHAL clock */
dword KernelTicks(void);

/* ---------------------------------------------------------------------
 * Timer (Timer/timerList.c, timerInt.c).  Port32/ABI.md section 11.
 * ------------------------------------------------------------------- */
#define TIMER_INTERRUPT_RATE	60	/* ticks per second */
#define TIME_SLICE		6	/* ticks (INTERRUPT_RATE / 10) */

/* Start the timer interrupt (GHAL_IRQ_TICK at 60 Hz). */
void KernelTimerInit(void);
void KernelTimerExit(void);
/* runs the timers that are due; from the timer interrupt */
void TimerCheckList(dword now);
/* frees what interrupt code could not (deferred); not from interrupts */
void TimerReap(void);
/* Event timers send a message; the message system sets this.  Until it
 * does, events are counted in kernelTimerEventsDropped. */
extern void (*kernelTimerEventHook)(optr dest, Message msg, dword tick,
				    word id);
extern dword kernelTimerEventsDropped;

/* ---------------------------------------------------------------------
 * Events, queues and messages (Geodes/geodesEvent.c, Object/
 * objectClass.c).  Port32/ABI.md section 12.
 * ------------------------------------------------------------------- */
/* MetaClass and ProcessClass messages the kernel handles or sends
 * (MetaMessages in metaC.goh, ProcessMessages in processC.goh; the
 * ctest kernel.obj compares them with goc's numbers) */
#define KMSG_META_NULL				0
#define KMSG_META_INITIALIZE			1
#define KMSG_META_DUMMY				3
#define KMSG_META_ACK				9
#define KMSG_META_GET_CLASS			13
#define KMSG_META_IS_OBJECT_IN_CLASS		14
#define KMSG_META_BLOCK_FREE			15
#define KMSG_META_OBJ_FREE			16
#define KMSG_META_FINAL_OBJ_FREE		17
#define KMSG_META_DEC_BLOCK_REF_COUNT		18
#define KMSG_META_RESOLVE_VARIANT_SUPERCLASS	19
#define KMSG_META_RELOCATE			20
#define KMSG_META_UNRELOCATE			21
#define KMSG_META_SET_FLAGS			23
#define KMSG_META_GET_FLAGS			24
#define KMSG_META_DISPATCH_EVENT		28
#define KMSG_META_SEND_CLASSED_EVENT		29
#define KMSG_META_GET_OPTR			30
#define KMSG_META_ADD_VAR_DATA			32
#define KMSG_META_DELETE_VAR_DATA		33
#define KMSG_META_INITIALIZE_VAR_DATA		34
#define KMSG_META_GET_VAR_DATA			46
#define KMSG_META_NOTIFY_OBJ_BLOCK_INTERACTIBLE	47
#define KMSG_META_NOTIFY_OBJ_BLOCK_NOT_INTERACTIBLE 48
#define KMSG_META_SET_OBJ_BLOCK_OUTPUT		53
#define KMSG_META_GET_OBJ_BLOCK_OUTPUT		56
#define KMSG_META_NOTIFY			35
#define KMSG_META_NOTIFY_WITH_DATA_BLOCK	36
#define KMSG_META_GCN_LIST_ADD			37
#define KMSG_META_GCN_LIST_REMOVE		38
#define KMSG_META_GCN_LIST_SEND			39
#define KMSG_META_GCN_LIST_DESTROY		40
#define KMSG_META_GCN_LIST_FIND_ITEM		60
#define KMSG_META_DETACH			6
#define KMSG_META_DETACH_COMPLETE		7
#define KMSG_META_APP_SHUTDOWN			10
#define KMSG_META_SHUTDOWN_COMPLETE		11
#define KMSG_META_SHUTDOWN_ACK			12
#define KMSG_META_QUIT				26
#define KMSG_META_QUIT_ACK			27
#define KMSG_META_FINISH_QUIT			52
/* MetaClass variable data (metaC.goh) */
#define KVD_TEMP_META_GCN			0	/* the first */
#define KVD_TEMP_META_QUIT_LIST			4
#define KVD_DETACH_DATA				8
#define KMSG_PROCESS_INSTANTIATE		8197
#define KMSG_PROCESS_EXIT			8198
#define KMSG_PROCESS_FINAL_BLOCK_FREE		8204
#define KMSG_PROCESS_INSTANTIATE_FOR_THREAD	8208

/* event reserve for interrupt code, event timers through messages */
void KernelEventInit(void);
/* events interrupt code could not send (no reserve left) */
extern dword kernelEventsDropped;
/* the parameter structure of an event */
const void *KernelEventParams(EventHandle event, dword *size);
/* call the method of `dest' (run by the current thread) */
void KernelDeliver(optr dest, Message msg, const void *params, void *reply);
/* the thread a queue's messages go to (ThreadAttachToQueue) */
void KQueueAttach(QueueHandle qh, ThreadHandle th);
void KQueueDetach(QueueHandle qh);

/* ---------------------------------------------------------------------
 * The object system (Object/, Port32/ABI.md section 13).
 * ------------------------------------------------------------------- */
extern ClassStruct MetaClass, ProcessClass;	/* objectMeta.c, objectProcess.c */
#define OBJ_ALIGN(n)	(((n) + 7u) & ~7u)	/* parts, variable data */
#define OBJ_VARIANT_SUPER ((ClassStruct *) 1)	/* GOCR_VARIANT_SUPERCLASS */

/* the class of the object (chunk locked) */
#define ObjClassOf(o)	(*(ClassStruct **) LMemDeref(o))
/* the superclass a variant master class `cls' has for this object (asks
 * the object if it is not known yet; objectClass.c) */
ClassStruct *ObjResolveVariant(optr o, ClassStruct *cls);
/* the next class up from `cls' for object `o' (0: none): variant
 * superclasses come from the object, unresolved ones give 0 */
ClassStruct *ObjSuperOf(optr o, ClassStruct *cls);
/* where an object's variable data lies in its chunk, from the chunk
 * start (objectVarData.c) */
void ObjVarDataArea(optr o, word *start, word *end);
/* the relocation method of a class (Class_relocMethod, made by goc for
 * "@method XClass, _reloc", whose prototype has a word message): TRUE on
 * error */
typedef Boolean ObjRelocMethod(void *pself, optr oself, word message,
			       word vmRelocType, word frame);
/* Relocate an object block: the output, and every object through its
 * classes' relocation tables and methods (objectReloc.c).  Unrelocate
 * does the reverse.  The handles come from the resources of the block's
 * owner (GeodeResourceHandle). */
void ObjRelocateObjBlock(MemHandle mh, word vmRelocType);
void ObjUnRelocateObjBlock(MemHandle mh, word vmRelocType);

/* ---------------------------------------------------------------------
 * Composites and links (Object/objectComposite.c, objectLinkable.c),
 * as in metaC.goh.  A link (LinkPart) holds the next sibling, or the
 * parent with LP_IS_PARENT set in the chunk for the last child; a
 * composite (CompPart) holds its first child.  The offsets are those of
 * the C API: the master offset (0: none), and the offsets of the
 * CompPart and LinkPart in that master part.
 * ------------------------------------------------------------------- */
#define LP_IS_PARENT	1
typedef WordFlags CompChildFlags;
#define CCF_MARK_DIRTY	0x8000
#define CCF_REFERENCE	0x7fff
#define CCO_FIRST	0x0000
#define CCO_LAST	0x7FFF
typedef enum {
    OCCT_SAVE_PARAMS_TEST_ABORT = 0,
    OCCT_SAVE_PARAMS_DONT_TEST_ABORT = 2,
    OCCT_DONT_SAVE_PARAMS_TEST_ABORT = 4,
    OCCT_DONT_SAVE_PARAMS_DONT_TEST_ABORT = 6,
    OCCT_ABORT_AFTER_FIRST = 8,
    OCCT_COUNT_CHILDREN = 10
} ObjCompCallType;
typedef Boolean ObjCompCallback(optr parent, optr child, void *cbData);

optr ObjLinkFindParent(optr obj, word masterOffset, word linkOffset);
word ObjCompFindChildByOptr(optr obj, optr childToFind, word masterOffset,
			    word compOffset, word linkOffset);
optr ObjCompFindChildByNumber(optr obj, word childToFind, word masterOffset,
			      word compOffset, word linkOffset);
void ObjCompAddChild(optr obj, optr objToAdd, word flags, word masterOffset,
		     word compOffset, word linkOffset);
void ObjCompRemoveChild(optr obj, optr objToRemove, word flags,
			word masterOffset, word compOffset, word linkOffset);
void ObjCompMoveChild(optr obj, optr objToMove, word flags,
		      word masterOffset, word compOffset, word linkOffset);
Boolean ObjCompProcessChildren(optr obj, optr firstChild,
			       ObjCompCallType stdCallback, void *cbData,
			       word masterOffset, word compOffset,
			       word linkOffset, ObjCompCallback *callback);

/* ---------------------------------------------------------------------
 * General change notification lists (LMem/lmemGCNList.c), as in
 * metaC.goh and gcnlist.goh (Port32 layout: Port32/ABI.md section 13).
 * ------------------------------------------------------------------- */
typedef struct {
    word	GCNLT_manuf;
    word	GCNLT_type;
} GCNListType;
#define GCNLTF_SAVE_TO_STATE	0x0001
typedef struct {
    optr	GCNLE_item;
} GCNListElement;
typedef struct {
    ChunkArrayHeader	GCNLH_meta;
    EventHandle		GCNLH_statusEvent;
    MemHandle		GCNLH_statusData;
    word		GCNLH_statusCount;
    word		GCNLH_reserved[3];	/* elements aligned to 8 */
} GCNListHeader;
typedef struct {
    GCNListType		GCNLOLE_ID;
    ChunkHandle		GCNLOLE_list;
} GCNListOfListsElement;
typedef struct {
    ChunkArrayHeader	GCNLOL_meta;
} GCNListOfListsHeader;
typedef ByteFlags TempMetaGCNFlags;
#define TMGCNF_RELOCATED	0x80
typedef struct {
    ChunkHandle		TMGCND_listOfLists;
    TempMetaGCNFlags	TMGCND_flags;
} TempMetaGCNData;
typedef WordFlags GCNListSendFlags;
#define GCNLSF_SET_STATUS			0x8000
#define GCNLSF_IGNORE_IF_STATUS_TRANSITIONING	0x4000
#define GCNLSF_FORCE_QUEUE			0x2000

/* the system lists (a kernel block) */
Boolean GCNListAdd(optr OD, ManufacturerID manufID, word listType);
Boolean GCNListRemove(optr OD, ManufacturerID manufID, word listType);
word GCNListSend(ManufacturerID manufID, word listType, EventHandle event,
		 MemHandle dataBlock, GCNListSendFlags flags);
void GCNListClearList(ManufacturerID manufID, word listType);
/* lists in a block: a list of lists */
ChunkHandle GCNListCreateBlock(MemHandle mh);
Boolean GCNListAddToBlock(optr OD, ManufacturerID manufID, word listType,
			  MemHandle mh, ChunkHandle listOfLists);
Boolean GCNListRemoveFromBlock(optr OD, ManufacturerID manufID,
			       word listType, MemHandle mh,
			       ChunkHandle listOfLists);
word GCNListSendToBlock(ManufacturerID manufID, word listType,
			EventHandle event, MemHandle dataBlock, MemHandle mh,
			ChunkHandle listOfLists, GCNListSendFlags flags);
ChunkHandle GCNListFindListInBlock(ManufacturerID manufID, word listType,
				   MemHandle mh, ChunkHandle listOfLists,
				   Boolean createIfNotFound);
void GCNListDestroyBlock(MemHandle mh, ChunkHandle listOfLists);
void GCNListRelocateBlock(MemHandle mh, ChunkHandle listOfLists,
			  MemHandle relocBlock);
Boolean GCNListUnRelocateBlock(MemHandle mh, ChunkHandle listOfLists,
			       MemHandle relocBlock);
/* one list */
ChunkHandle GCNListCreateList(MemHandle mh);
Boolean GCNListAddToList(optr OD, optr list);
Boolean GCNListRemoveFromList(optr OD, optr list);
void GCNListSendToList(optr list, EventHandle event, MemHandle dataBlock,
		       GCNListSendFlags flags);
Boolean GCNListFindItemInList(optr OD, optr list);
void GCNListDestroyList(optr list);
void GCNListRelocateList(optr list, MemHandle relocBlock);
void GCNListUnRelocateList(optr list, MemHandle relocBlock);
/* is `newEv' (with data block `newData') the same status as `oldEv'
 * (with `oldData')?  Then it need not be sent again (geodesEvent.c) */
Boolean EventSameStatus(EventHandle newEv, MemHandle newData,
			EventHandle oldEv, MemHandle oldData);

/* ---------------------------------------------------------------------
 * Detach and quit (Object/objectShutdown.c), as in metaC.goh.
 * ------------------------------------------------------------------- */
typedef struct {
    word	DDE_ackCount;
    word	DDE_callerID;
    optr	DDE_ackOD;
    word	DDE_completeMsg;
} DetachDataEntry;
#define MGCNLT_ACTIVE_LIST	0x0000		/* geoworks.h */

void ObjInitDetach(Message msg, optr obj, word callerID, optr ackOD);
void ObjIncDetach(optr obj);
void ObjEnableDetach(optr obj);
/* the MetaClass methods (objectShutdown.c) */
GocMethodThunk MetaAck, MetaDetach, MetaDetachComplete, MetaAppShutdown,
	       MetaShutdownComplete, MetaQuit, MetaQuitAck, MetaFinishQuit;

/* ---------------------------------------------------------------------
 * Geode resources (Geodes/geodesResource.c).  Until GeodeLoad exists,
 * whoever builds a geode's resources registers their handles here.
 * ------------------------------------------------------------------- */
/* handles[0] is the geode's process (core block), as in
 * __goc_resource_handles; the table must stay valid */
void GeodeSetResourceHandles(GeodeHandle gh, const Handle *handles,
			     word count);
/* the handle of resource `res' of a geode (GOCR_PROCESS_RES: the
 * process); 0 if the geode or resource is not known */
Handle GeodeResourceHandle(GeodeHandle gh, word res);
/* the resource number of `h' in a geode, or -1 */
sdword GeodeHandleToResource(GeodeHandle gh, Handle h);

/* ---------------------------------------------------------------------
 * LMem (LMem/lmemCode.c, after lmemCode.asm).  Port32/ABI.md section 10.
 * ------------------------------------------------------------------- */
/* Check the structure of an LMem block (fatal CORRUPTED_LMEM_BLOCK). */
void LMemValidate(MemHandle mh);
/* Build the LMem block of a resource made by goc -cg (header, handle
 * table, chunks in handle order).  Optrs in it stay unrelocated. */
/* ElementArrayAddElement with the size of a variable-sized element
 * (lmemElementArray.c) */
word ElementArrayAddElementSized(optr arr, void *element, word size,
				 dword callbackData,
				 Boolean (*callback)(void *elementToAdd,
						     void *elementFromArray,
						     dword valueForCallback));
MemHandle LMemCreateResourceBlock(const GocResource *r, GeodeHandle owner,
				  HeapFlags hf, HeapAllocFlags haf);

#endif /* KERNELC_H */
