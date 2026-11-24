/*
 * test_kernel_msg.c -- events, queues and messages of the C kernel
 * (Library/Kernel/Geodes/geodesEvent.c, Object/objectClass.c) with the
 * classes and objects of Goc/kmsg.goc.
 */
#include "kernelC.h"
#include "ghal.h"

#include <stdio.h>

Handle __goc_resource_handles[4];	/* the geode loader fills it */

extern ClassStruct KMsgProcessClass;
extern const GocResource __goc_resource_Objects, __goc_resource_Remote;
extern const ChunkHandle KButton, KRemote;
extern word kmsgAckCode;
extern optr kmsgAckCaller;

int KMsgLocal(optr button);
int KMsgRemote(optr proc, optr remote);
int KMsgTimer(optr proc);
int KMsgFlush(optr proc, optr other);

static word ProcessThread(word unused)
{
    (void) unused;
    ThreadAttachToQueue(GeodeAllocQueue(), &KMsgProcessClass);
    return 0;				/* not reached */
}

static ThreadHandle t2;
static word Quitter(word code)
{
    ThreadDestroy(code, ConstructOptr(t2, 0), 0x55);
    return 0;				/* not reached */
}

int main(void)
{
    ThreadHandle me;
    MemHandle objects, remote;
    int fails = 0;

    kernelCurrentGeode = 0x2340;
    me = KernelThreadInit(PRIORITY_STANDARD);
    KernelTimerInit();

    objects = LMemCreateResourceBlock(&__goc_resource_Objects, 0x2340,
				      HF_SWAPABLE, 0);
    remote = LMemCreateResourceBlock(&__goc_resource_Remote, 0x2340,
				     HF_SWAPABLE, 0);
    MemModifyOtherInfo(objects, me);	/* run by this thread */
    fails += KMsgLocal(ConstructOptr(objects, KButton));
    LMemValidate(objects);

    t2 = ThreadCreate(PRIORITY_LOW, 0, ProcessThread, 0, 0x2340);
    while (!ThreadGetInfo(t2, TGIT_QUEUE_HANDLE))
	TimerSleep(1);			/* let it attach (slow under QEMU) */
    MemModifyOtherInfo(remote, t2);
    fails += KMsgRemote(ConstructOptr(t2, 0), ConstructOptr(remote, KRemote));
    fails += KMsgTimer(ConstructOptr(t2, 0));
    fails += KMsgFlush(ConstructOptr(t2, 0), ConstructOptr(objects, KButton));
    LMemValidate(remote);

    /* ThreadDestroy acknowledges to the process thread */
    {
	ThreadHandle t3 = ThreadCreate(PRIORITY_STANDARD, 77, Quitter, 0,
				       0x2340);
	TimerSleep(10);
	if (!(kmsgAckCode == 77 &&
	      kmsgAckCaller == ConstructOptr(t3, 0x55))) {
	    fails++;
	    printf("FAIL ack: %u\n", kmsgAckCode);
	}
    }
    if (kernelEventsDropped) {
	fails++;
	printf("FAIL events dropped\n");
    }

    KernelTimerExit();
    printf("%s: %s: %d failure(s)\n", ghal_host_name(),
	   fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
