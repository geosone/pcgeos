/*
 * objectProcess.c -- ProcessClass (after objectProcess.asm).
 *
 * A process is not an object in a chunk: messages to a process, its
 * threads or their queues reach the class the thread was attached with
 * (ThreadAttachToQueue), with pself NULL.  ProcessClass makes objects for
 * blocks the thread runs and frees them at the end of a block free.
 *
 * Not yet: creating event and UI threads, MSG_PROCESS_CALL_ROUTINE, the
 * copy-chunk messages, discarding and VM blocks, detach.
 */
#include "kernelC.h"

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

static const Message processMessages[] = {
    KMSG_PROCESS_INSTANTIATE,
    KMSG_PROCESS_FINAL_BLOCK_FREE,
    KMSG_PROCESS_INSTANTIATE_FOR_THREAD
};

static GocMethodThunk *const processMethods[] = {
    ProcessInstantiate,
    ProcessFinalBlockFree,
    ProcessInstantiateForThread
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
