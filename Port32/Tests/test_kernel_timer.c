/*
 * test_kernel_timer.c -- timers, the timer interrupt and time slicing of
 * the C kernel (Library/Kernel/Timer/timerList.c, timerInt.c, and the
 * scheduler's preemption in Thread/threadThread.c); the date and time
 * and real-time timers (Timer/timerMisc.c).
 */
#include "kernelC.h"
#include "ghal.h"
#include <timedate.h>

#include <stdio.h>

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static volatile int fired, continual;
static volatile word lastData;
static SemaphoreHandle wake;

static void OneShot(word data, dword tick)
{
    (void) tick;
    lastData = data;
    fired++;
}

static void Continual(word data, dword tick)
{
    (void) data;
    (void) tick;
    continual++;
}

static void Waker(word data, dword tick)
{
    (void) data;
    (void) tick;
    ThreadVSem(wake);			/* from interrupt code */
}

static optr evDest;
static Message evMsg;
static int events;
static void EventHook(optr dest, Message msg, dword tick, word id)
{
    (void) tick;
    (void) id;
    evDest = dest;
    evMsg = msg;
    events++;
}

static volatile int rtFired;
static volatile dword rtDue;
static void RealTime(word data, dword due)
{
    if (data == 0x5a5a)
	rtFired++;
    rtDue = due;
}

#define TCD(y, m, d)	((word) (((y) - 1980) << 9 | (m) << 5 | (d)))

static void SetClock(word y, word mo, word d, word h, word mi, word s)
{
    TimerDateAndTime dt;

    dt.TDAT_year = y;
    dt.TDAT_month = mo;
    dt.TDAT_day = d;
    dt.TDAT_dayOfWeek = DOTW_SUNDAY;	/* ignored */
    dt.TDAT_hours = h;
    dt.TDAT_minutes = mi;
    dt.TDAT_seconds = s;
    TimerSetDateAndTime(TIME_SET_DATE | TIME_SET_TIME, &dt);
}

static void DatesAndRealTime(void)
{
    TimerDateAndTime dt;
    TimerFileDateTime fdt;
    word id;
    TimerHandle th;

    TimerGetDateAndTime(&dt);
    CHECK(dt.TDAT_year >= 2020 && dt.TDAT_month >= 1 && dt.TDAT_month <= 12
	  && dt.TDAT_day >= 1 && dt.TDAT_day <= 31 && dt.TDAT_hours < 24 &&
	  dt.TDAT_minutes < 60 && dt.TDAT_seconds < 60 &&
	  dt.TDAT_dayOfWeek <= DOTW_SATURDAY);

    /* the clock of GEOS moves; the host's does not */
    SetClock(2030, 6, 15, 12, 0, 0);
    TimerGetDateAndTime(&dt);
    CHECK(dt.TDAT_year == 2030 && dt.TDAT_month == 6 && dt.TDAT_day == 15);
    CHECK(dt.TDAT_hours == 12 && dt.TDAT_minutes == 0 &&
	  dt.TDAT_seconds <= 1);
    CHECK(dt.TDAT_dayOfWeek == DOTW_SATURDAY);
    fdt = TimerGetFileDateTime();
    CHECK(fdt.TFDT_fileDate == TCD(2030, 6, 15));
    CHECK(fdt.TFDT_fileTime >> 11 == 12);
    SetClock(2000, 2, 29, 23, 59, 30);	/* a leap day */
    TimerGetDateAndTime(&dt);
    CHECK(dt.TDAT_year == 2000 && dt.TDAT_month == 2 && dt.TDAT_day == 29 &&
	  dt.TDAT_hours == 23 && dt.TDAT_minutes == 59 &&
	  dt.TDAT_dayOfWeek == DOTW_TUESAY);

    /* a real-time timer goes off when the clock reaches its minute */
    SetClock(2030, 6, 15, 12, 0, 0);
    th = TimerStart(TIMER_ROUTINE_REAL_TIME, TimerRoutineOptr(RealTime),
		    TCD(2030, 6, 15), 0x5a5a, 12 << 8 | 5, &id);
    CHECK(th != 0);
    TimerSleep(3);
    CHECK(rtFired == 0);
    SetClock(2030, 6, 15, 12, 5, 0);
    CHECK(rtFired == 1);
    CHECK(rtDue == ((dword) TCD(2030, 6, 15) << 16 | 12 << 8 | 5));
    CHECK(TimerStop(th, id) == TRUE);	/* ended already */
    /* stopped before its time, it does not */
    th = TimerStart(TIMER_ROUTINE_REAL_TIME, TimerRoutineOptr(RealTime),
		    TCD(2030, 6, 16), 0x5a5a, 0, &id);
    CHECK(TimerStop(th, id) == FALSE);
    SetClock(2030, 6, 16, 0, 1, 0);
    CHECK(rtFired == 1);
    /* one in the past goes off at the next check (once a second) */
    TimerStart(TIMER_ROUTINE_REAL_TIME, TimerRoutineOptr(RealTime),
	       TCD(2029, 1, 1), 0x5a5a, 0, NULL);
    TimerSleep(75);
    CHECK(rtFired == 2);
}

/* two threads that only compute: time slices let both run */
static volatile int stop;
static volatile dword spins[2];
static volatile byte maxPrio[2];
static SemaphoreHandle done;

static word Spinner(word which)
{
    while (!stop) {
	byte p;
	spins[which]++;
	TimerGetCount();			/* a kernel entry */
	p = TGI_PRIORITY(ThreadGetInfo(0, TGIT_PRIORITY_AND_USAGE));
	if (p > maxPrio[which])
	    maxPrio[which] = p;
    }
    ThreadVSem(done);
    return 0;
}

int main(void)
{
    dword t0, t1;
    word id, id2;
    TimerHandle th, th2;
    int c;

    kernelCurrentGeode = 0x2340;
    KernelThreadInit(PRIORITY_STANDARD);
    KernelTimerInit();

    /* sleep and the tick count */
    t0 = TimerGetCount();
    TimerSleep(6);
    t1 = TimerGetCount();
    CHECK(t1 - t0 >= 6 && t1 - t0 < 30);

    /* routine, one shot */
    th = TimerStart(TIMER_ROUTINE_ONE_SHOT, TimerRoutineOptr(OneShot), 3,
		    0x4321, 0, &id);
    CHECK(th != 0 && id != 0);
    CHECK(fired == 0);
    TimerSleep(8);
    CHECK(fired == 1 && lastData == 0x4321);
    CHECK(TimerStop(th, id) == TRUE);	/* ended already */

    /* routine, continual every 2 ticks */
    th = TimerStart(TIMER_ROUTINE_CONTINUAL, TimerRoutineOptr(Continual), 2,
		    0, 2, &id);
    TimerSleep(30);
    c = continual;
    CHECK(c >= 11 && c <= 16);
    CHECK(TimerStop(th, (word) (id + 1)) == TRUE);	/* wrong id */
    CHECK(TimerStop(th, id) == FALSE);
    c = continual;
    TimerSleep(6);
    CHECK(continual == c);

    /* milliseconds */
    fired = 0;
    t0 = TimerGetCount();
    TimerStart(TIMER_MS_ROUTINE_ONE_SHOT, TimerRoutineOptr(OneShot), 50,
	       7, 0, NULL);
    while (!fired && TimerGetCount() - t0 < 60)
	TimerSleep(1);
    t1 = TimerGetCount();
    CHECK(fired == 1 && t1 - t0 >= 3 && t1 - t0 <= 8);

    /* an interrupt wakes a blocked thread */
    wake = ThreadAllocSem(0);
    t0 = TimerGetCount();
    TimerStart(TIMER_ROUTINE_ONE_SHOT, TimerRoutineOptr(Waker), 5, 0, 0,
	       NULL);
    CHECK(ThreadPTimedSem(wake, 120) == SE_NO_ERROR);
    t1 = TimerGetCount();
    CHECK(t1 - t0 >= 5 && t1 - t0 <= 12);

    /* events: dropped without a message system, else to the hook (the
     * message system's own hook is tested in kernel.msg) */
    kernelTimerEventHook = NULL;
    TimerStart(TIMER_EVENT_ONE_SHOT, 0x1234500000006ull, 1, 77, 0, NULL);
    TimerSleep(4);
    CHECK(kernelTimerEventsDropped == 1);
    kernelTimerEventHook = EventHook;
    th2 = TimerStart(TIMER_EVENT_CONTINUAL, 0x1234500000006ull, 1, 78, 3,
		     &id2);
    TimerSleep(10);
    CHECK(events >= 2 && evDest == 0x1234500000006ull && evMsg == 78);
    TimerStop(th2, id2);
    kernelTimerEventHook = NULL;

    /* time slices: two computing threads of the same priority both run
     * while main sleeps; their priority rises with CPU usage */
    done = ThreadAllocSem(0);
    ThreadCreate(PRIORITY_LOW, 0, Spinner, 0, 0x2340);
    ThreadCreate(PRIORITY_LOW, 1, Spinner, 0, 0x2340);
    TimerSleep(60);			/* main is better: it comes back */
    stop = 1;
    ThreadPSem(done);
    ThreadPSem(done);
    CHECK(spins[0] > 0 && spins[1] > 0);
    CHECK(maxPrio[0] > PRIORITY_LOW && maxPrio[1] > PRIORITY_LOW);
    printf("time slices: %lu / %lu spins, priority up to %u / %u\n",
	   (unsigned long) spins[0], (unsigned long) spins[1],
	   maxPrio[0], maxPrio[1]);

    DatesAndRealTime();

    KernelTimerExit();
    printf("%s: %s: %d failure(s)\n", ghal_host_name(),
	   fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
