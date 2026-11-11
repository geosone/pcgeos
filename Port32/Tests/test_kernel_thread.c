/*
 * test_kernel_thread.c -- threads, scheduler, semaphores and thread locks
 * of the C kernel (Library/Kernel/Thread/threadThread.c, threadSem.c) and
 * HandleP/MemThreadGrab (Heap/heapHigh.c), on ghal_ctx.
 */
#include "kernelC.h"
#include "ghal.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static jmp_buf catcher;
static KernelFatalError caught;

static void Catch(KernelFatalError code)
{
    caught = code;
    longjmp(catcher, 1);
}

#define EXPECT_FATAL(code, stmt) \
    do { \
	caught = KFE_NONE; \
	if (setjmp(catcher) == 0) { stmt; } \
	if (caught != (code)) { \
	    fails++; \
	    printf("FAIL line %d: expected %s, got %s\n", __LINE__, \
		   KernelFatalErrorName(code), KernelFatalErrorName(caught)); \
	} \
    } while (0)

/* what happened, in order */
static char trace[128];
static int tpos;
static void Note(char c)
{
    if (tpos < (int) sizeof(trace) - 1)
	trace[tpos++] = c;
    trace[tpos] = 0;
}
static void ResetTrace(void)
{
    tpos = 0;
    trace[0] = 0;
}
#define CHECK_TRACE(want) \
    do { if (strcmp(trace, want) != 0) { fails++; \
	printf("FAIL line %d: trace \"%s\", want \"%s\"\n", __LINE__, \
	       trace, want); } ResetTrace(); } while (0)

static SemaphoreHandle done, ping, pong;
static ThreadLockHandle lock;
static MemHandle block;

/* ping-pong: two threads of the same priority take turns */
static word Pinger(word n)
{
    word i;
    for (i = 0; i < n; i++) {
	ThreadPSem(ping);
	Note('p');
	ThreadVSem(pong);
    }
    ThreadVSem(done);
    return 11;
}

static word Ponger(word n)
{
    word i;
    for (i = 0; i < n; i++) {
	ThreadPSem(pong);
	Note('q');
	ThreadVSem(ping);
    }
    ThreadVSem(done);
    return 22;
}

/* notes its letter; deep recursion checks its own stack */
static int Deep(int n)
{
    volatile char pad[128];
    pad[0] = (char) n;
    return n ? Deep(n - 1) + 1 + (pad[0] != (char) n) : 0;
}

static word Noter(word c)
{
    Note((char) c);
    CHECK(Deep(200) == 200);
    ThreadVSem(done);
    return c;
}

/* takes the thread lock held by main, twice (nested) */
static word Locker(word c)
{
    Note('w');
    ThreadGrabThreadLock(lock);
    ThreadGrabThreadLock(lock);
    Note((char) c);
    ThreadReleaseThreadLock(lock);
    ThreadReleaseThreadLock(lock);
    ThreadVSem(done);
    return 0;
}

static word Grabber(word c)
{
    char *p;
    Note('w');
    p = MemThreadGrab(block);
    Note(p[0]);
    p[0] = (char) c;
    MemThreadRelease(block);
    ThreadVSem(done);
    return 0;
}

static word Waker(word c)
{
    (void) c;
    Note('v');
    ThreadVSem(ping);
    ThreadVSem(done);
    return 0;
}

int main(void)
{
    ThreadHandle me, t1, t2, t3;
    dword base, start;
    char *p;
    int i;

    kernelFatalHook = Catch;
    kernelCurrentGeode = 0x2340;
    base = HandleCountUsed();
    me = KernelThreadInit(PRIORITY_STANDARD);
    CHECK(me != 0 && KernelThisThread() == me);
    CHECK(ThreadGetInfo(0, TGIT_THREAD_HANDLE) == me);
    CHECK(TGI_PRIORITY(ThreadGetInfo(me, TGIT_PRIORITY_AND_USAGE))
	  == PRIORITY_STANDARD);
    done = ThreadAllocSem(0);
    ping = ThreadAllocSem(1);
    pong = ThreadAllocSem(0);

    /* same priority: they run when main blocks, and take turns */
    t1 = ThreadCreate(PRIORITY_STANDARD, 3, Pinger, 512, 0x2340);
    t2 = ThreadCreate(PRIORITY_STANDARD, 3, Ponger, 512, 0x2340);
    CHECK(t1 && t2 && t1 != t2);
    CHECK(MemOwner(t1) == 0x2340);
    CHECK_TRACE("");			/* nothing ran yet */
    ThreadPSem(done);
    ThreadPSem(done);
    CHECK_TRACE("pqpqpq");
    ThreadPSem(ping);			/* back to 0 */

    /* better priority runs at once, worse only when main blocks */
    ThreadCreate(PRIORITY_LOW, 'L', Noter, 0, 0x2340);
    Note('1');
    ThreadCreate(PRIORITY_UI, 'U', Noter, 0, 0x2340);
    Note('2');
    ThreadPSem(done);			/* U's V: no switch needed */
    ThreadPSem(done);			/* blocks: L runs */
    CHECK_TRACE("1U2L");

    /* yield: same priority goes first, ThreadModify reorders */
    ThreadCreate(PRIORITY_STANDARD, 'a', Noter, 0, 0x2340);
    t3 = ThreadCreate(PRIORITY_STANDARD, 'b', Noter, 0, 0x2340);
    ThreadModify(t3, PRIORITY_HIGH, TMF_BASE_PRIO);	/* b runs now */
    Note('m');
    KernelYield();			/* a */
    Note('n');
    ThreadPSem(done);
    ThreadPSem(done);
    CHECK_TRACE("bamn");		/* a was ready before main */

    /* ended threads are released; L was preempted by main after its V
     * and ends when main sleeps */
    ThreadPTimedSem(pong, 1);
    CHECK(HandleCountUsed() == base + 4);	/* me, 3 semaphores */

    /* thread lock: nested, the other thread waits */
    lock = ThreadAllocThreadLock();
    ThreadGrabThreadLock(lock);
    ThreadGrabThreadLock(lock);
    ThreadCreate(PRIORITY_UI, 'L', Locker, 0, 0x2340);	/* blocks */
    Note('r');
    ThreadReleaseThreadLock(lock);
    Note('s');
    ThreadReleaseThreadLock(lock);	/* Locker (better) runs now */
    Note('t');
    ThreadPSem(done);
    CHECK_TRACE("wrsLt");
    EXPECT_FATAL(UNLOCK_MODULE_BY_WRONG_THREAD,
		 ThreadReleaseThreadLock(lock));
    ThreadFreeThreadLock(lock);

    /* MemThreadGrab: nested for the owner, the other thread waits */
    block = MemAlloc(16, HF_SWAPABLE, HAF_ZERO_INIT);
    p = MemThreadGrab(block);
    CHECK(MemThreadGrab(block) == p);
    p[0] = 'G';
    ThreadCreate(PRIORITY_UI, 'X', Grabber, 0, 0x2340);
    MemThreadRelease(block);
    Note('r');
    MemThreadRelease(block);		/* Grabber runs */
    Note('t');
    ThreadPSem(done);
    CHECK_TRACE("wrGt");
    CHECK(((char *) MemDeref(block))[0] == 'X');
    CHECK(MGI_LOCK_COUNT(MemGetInfo(block, MGIT_FLAGS_AND_LOCK_COUNT)) == 0);
    EXPECT_FATAL(THREAD_RELEASE_NOT_OWNER, MemThreadRelease(block));
    p = MemPLock(block);
    CHECK(p && MemThreadGrabNB(block) == (void *) BLOCK_GRABBED);
    MemUnlockV(block);
    CHECK(MemThreadGrabNB(block) == p);
    MemThreadRelease(block);
    MemFree(block);

    /* timed P: no wait, timeout, woken in time */
    CHECK(ThreadPTimedSem(ping, 0) == SE_TIMEOUT);
    start = KernelTicks();
    CHECK(ThreadPTimedSem(ping, 6) == SE_TIMEOUT);
    i = (int) (KernelTicks() - start);
    CHECK(i >= 6 && i < 60);
    ThreadCreate(PRIORITY_LOW, 0, Waker, 0, 0x2340);
    CHECK(ThreadPTimedSem(ping, 600) == SE_NO_ERROR);
    CHECK_TRACE("v");
    ThreadPSem(done);
    CHECK(ThreadPTimedSem(ping, 0) == SE_TIMEOUT);	/* value back to 0 */

    /* error checks */
    EXPECT_FATAL(NON_SEMAPHORE_PASSED_TO_SEM_ROUTINE, ThreadPSem(me));
    EXPECT_FATAL(ILLEGAL_THREAD, ThreadGetInfo(done, TGIT_THREAD_HANDLE));
    EXPECT_FATAL(THREAD_GET_INFO_BAD_PARAMETER,
		 ThreadGetInfo(me, (ThreadGetInfoType) 6));

    ThreadFreeSem(done);
    ThreadFreeSem(ping);
    ThreadPTimedSem(pong, 1);		/* let Waker (low) end */
    ThreadFreeSem(pong);
    CHECK(HandleCountUsed() == base + 1);	/* me */
    printf("%s: %s: %d failure(s)\n", ghal_host_name(),
	   fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
