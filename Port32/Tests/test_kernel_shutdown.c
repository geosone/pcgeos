/*
 * test_kernel_shutdown.c -- SysShutdown and EndGeos (Sys/sysMisc.c,
 * Boot/bootBoot.c; Port32/ABI.md section 13i).
 *
 * Queues stand in for the objects on GCNSLT_SHUTDOWN_CONTROL and for the
 * object that gets the answer: the test reads their events.
 *   - SST_CLEAN: MSG_META_CONFIRM_SHUTDOWN goes to the list, a second
 *     shutdown is refused while it runs, and the answer (TRUE) comes when
 *     the last object has confirmed.
 *   - SST_SUSPEND refused by one object: the other one learns it at
 *     SST_CONFIRM_START, and the answer is FALSE.
 *   - SST_RESTART fails; SST_DIRTY from another thread ends GEOS:
 *     KernelRun returns KEF_DIRTY and the reason, the open file is
 *     closed, the thread does not come back.
 */
#include "kernelC.h"
#include "ghal.h"
#include <system.h>

#include <stdio.h>
#include <string.h>

#define CONFIRM_SHUTDOWN	702	/* MSG_META_CONFIRM_SHUTDOWN */
#define SHUTDOWN_CONTROL	14	/* GCNSLT_SHUTDOWN_CONTROL */
#define ACK_MSG			0x6000

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static int returned;

/* the next event of a queue (0 if none): its message and first word,
 * and the second parameter (the type of MSG_META_CONFIRM_SHUTDOWN) */
static Message Next(QueueHandle q, word *w0, int *type)
{
    EventHandle ev;
    const byte *p;
    dword size;
    Message m;

    if (!GeodeInfoQueue(q))
	return 0;
    ev = QueueGetMessage(q);
    m = ObjGetMessageInfo(ev, NULL);
    p = KernelEventParams(ev, &size);
    *w0 = size >= 2 ? *(const word *) p : 0xffff;
    if (type)
	*type = size >= 8 ? *(const int *) (p + 4) : -1;
    ObjFreeMessage(ev);
    return m;
}

static word Ender(word v)
{
    (void) v;
    SysShutdown(SST_DIRTY, "the test ends GEOS");
    returned = 1;
    return 0;
}

static int OpenFiles(Handle h, HandleEntry *he, void *ctx)
{
    (void) h;
    if (he->HE_type == SIG_FILE)
	(*(int *) ctx)++;
    return 0;
}

int main(int argc, char **argv)
{
    char top[260], *slash;
    QueueHandle q1, q2, qa;
    GeodeLoadError e;
    word w;
    int type, nfiles = 0;
    FileHandle fh;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    strncpy(top, argv[0], sizeof(top) - 8);
    top[sizeof(top) - 8] = 0;
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash)
	strcpy(slash + 1, "GSDTOP");
    else
	strcpy(top, "GSDTOP");
    ghal_fs_mkdir(top);
    CHECK(KernelBoot(top, "", &e) == 0 && e == GLE_OK);

    q1 = GeodeAllocQueue();
    q2 = GeodeAllocQueue();
    qa = GeodeAllocQueue();
    CHECK(GCNListAdd(ConstructOptr(q1, 0), MANUFACTURER_ID_GEOWORKS,
		     SHUTDOWN_CONTROL));

    /* a clean shutdown, confirmed */
    CHECK(!SysShutdown(SST_CLEAN, ConstructOptr(qa, 0), ACK_MSG));
    CHECK(Next(q1, &w, &type) == CONFIRM_SHUTDOWN && type == 1);
    CHECK(SysShutdown(SST_SUSPEND, ConstructOptr(qa, 0), ACK_MSG));
    CHECK(GeodeInfoQueue(qa) == 0);
    CHECK(!SysShutdown(SST_CONFIRM_START));
    CHECK(!SysShutdown(SST_CONFIRM_END, TRUE));
    CHECK(Next(qa, &w, NULL) == ACK_MSG && w == (word) TRUE);

    /* a suspend, refused by one of two */
    CHECK(GCNListAdd(ConstructOptr(q2, 0), MANUFACTURER_ID_GEOWORKS,
		     SHUTDOWN_CONTROL));
    CHECK(!SysShutdown(SST_SUSPEND, ConstructOptr(qa, 0), ACK_MSG));
    CHECK(Next(q1, &w, &type) == CONFIRM_SHUTDOWN && type == 0);
    CHECK(Next(q2, &w, &type) == CONFIRM_SHUTDOWN && type == 0);
    CHECK(!SysShutdown(SST_CONFIRM_START));
    CHECK(!SysShutdown(SST_CONFIRM_END, FALSE));
    CHECK(GeodeInfoQueue(qa) == 0);
    CHECK(SysShutdown(SST_CONFIRM_START));	/* refused already */
    CHECK(Next(qa, &w, NULL) == ACK_MSG && w == (word) FALSE);

    /* the next one may start again */
    CHECK(!SysShutdown(SST_CLEAN, ConstructOptr(qa, 0), ACK_MSG));
    CHECK(Next(q1, &w, &type) == CONFIRM_SHUTDOWN);
    CHECK(Next(q2, &w, &type) == CONFIRM_SHUTDOWN);
    CHECK(!SysShutdown(SST_CONFIRM_START));
    CHECK(!SysShutdown(SST_CONFIRM_END, TRUE));
    CHECK(!SysShutdown(SST_CONFIRM_START));
    CHECK(!SysShutdown(SST_CONFIRM_END, TRUE));
    CHECK(Next(qa, &w, NULL) == ACK_MSG && w == (word) TRUE);

    CHECK(SysShutdown(SST_RESTART));
    CHECK(!KernelEnded());

    /* a dirty end from another thread */
    FileSetStandardPath(SP_TOP);
    fh = FileCreate("open.txt", FILE_CREATE_TRUNCATE | FCF_NATIVE |
				FILE_ACCESS_RW, 0);
    CHECK(fh != 0);
    ThreadCreate(PRIORITY_STANDARD, 0, Ender, 1024, 0);
    {
	/* no process runs, so KernelRun would return at once */
	int n;
	for (n = 0; n < 300 && !KernelEnded(); n++)
	    TimerSleep(1);
    }
    CHECK(KernelRun() == KEF_DIRTY);
    CHECK(KernelEnded());
    CHECK(!strcmp(KernelExitMessage(), "the test ends GEOS"));
    HandleEnum(OpenFiles, &nfiles);
    CHECK(nfiles == 0);
    TimerSleep(5);
    CHECK(!returned);

    printf("%s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
