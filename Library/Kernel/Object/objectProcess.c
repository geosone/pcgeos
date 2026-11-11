/*
 * objectProcess.c -- ProcessClass (after objectProcess.asm).
 *
 * A process is not an object in a chunk: messages to a process, its
 * threads or their queues reach the class the thread was attached with
 * (ThreadAttachToQueue), with pself NULL.  ProcessClass makes objects for
 * blocks the thread runs and frees them at the end of a block free.
 *
 * Not yet: creating event and UI threads, MSG_PROCESS_CALL_ROUTINE, the
 * copy-chunk messages, discarding and VM blocks.
 */
#include "kernelC.h"

#include <string.h>

/* MSG_PROCESS_INSTANTIATE (internal): ObjInstantiate for another thread */
static void ProcessInstantiate(void *pself, optr oself, Message m,
			       void *params, void *reply)
{
    struct { MemHandle block; ClassStruct *cls; } *p = params;

    (void) pself; (void) oself; (void) m;
    *(ChunkHandle *) reply = OptrToChunk(ObjInstantiate(p->block, p->cls));
}

static void ProcessInstantiateForThread(void *pself, optr oself, Message m,
					void *params, void *reply)
{
    (void) pself; (void) oself; (void) m;
    *(optr *) reply = ObjInstantiateForThread(0, *(ClassStruct **) params);
}

/* the end of ObjFreeObjBlock: the queue is through */
static void ProcessFinalBlockFree(void *pself, optr oself, Message m,
				  void *params, void *reply)
{
    (void) pself; (void) oself; (void) m; (void) reply;
    MemFree(*(MemHandle *) params);
}

/* MSG_META_DETACH to a process: the thread exits once its queue is
 * through (MSG_PROCESS_EXIT, queued with the same parameters) */
static void ProcessDetach(void *pself, optr oself, Message m, void *params,
			  void *reply)
{
    struct { word callerID; optr ackOD; } p = { 0, 0 };

    (void) pself; (void) oself; (void) m; (void) reply;
    if (params)
	memcpy(&p, params, sizeof(p));
    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(KernelThisThread(), 0),
		 KMSG_PROCESS_EXIT, &p, sizeof(p), NULL, 0);
}

/* MSG_PROCESS_EXIT: ThreadDestroy(exit code, ack optr); the ack data the
 * asm kernel passed in SI is 0 (Port32: no registers) */
static void ProcessExit(void *pself, optr oself, Message m, void *params,
			void *reply)
{
    struct { word callerID; optr ackOD; } p = { 0, 0 };

    (void) pself; (void) oself; (void) m; (void) reply;
    if (params)
	memcpy(&p, params, sizeof(p));
    ThreadDestroy(p.callerID, p.ackOD, 0);
}

static const Message processMessages[] = {
    KMSG_PROCESS_INSTANTIATE,
    KMSG_PROCESS_FINAL_BLOCK_FREE,
    KMSG_PROCESS_INSTANTIATE_FOR_THREAD,
    KMSG_META_DETACH,
    KMSG_PROCESS_EXIT
};

static GocMethodThunk *const processMethods[] = {
    ProcessInstantiate,
    ProcessFinalBlockFree,
    ProcessInstantiateForThread,
    ProcessDetach,
    ProcessExit
};

ClassStruct ProcessClass = {
    &MetaClass,
    0,
    sizeof(processMessages) / sizeof(processMessages[0]),
    sizeof(ClassStruct *),
    NULL, NULL,
    CLASSF_NEVER_SAVED,
    2,						/* messages 8192-16383 */
    processMessages,
    (MessageMethod *const *) processMethods,
    NULL,
    NULL
};
