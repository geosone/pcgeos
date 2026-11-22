/*
 * test_text.c -- the text library (Library/Text): text objects of the
 * process geode textobj (Geode/textobj.goc), not on a screen.
 *
 * The steps (checked in the geode):
 *   1. the text of a resource read; appended to;
 *   2. the selection replaced, moved by changes before it;
 *   3. replaced by the user: modified, and no longer;
 *   4. deleted: empty, and no longer; a range read;
 *   5. the selection set and deleted;
 *   6. text from and to chunks and blocks;
 *   7. the maximum length;
 *   8. an object without a text, a new object, the instance data.
 */
#include "kernelC.h"

#include <stdio.h>
#include <string.h>

extern const unsigned char ui_geode[], text_geode[], textobj_geode[];
extern const unsigned long ui_geode_size, text_geode_size,
			   textobj_geode_size;

/* as textobj.goc */
typedef struct {
    GeodeHandle	TOS_process;
    word	TOS_attached;
    word	TOS_checks, TOS_fails, TOS_failLine;
    word	TOS_failLines[8];
    word	TOS_modified, TOS_notModified;
    word	TOS_empty, TOS_notEmpty;
    char	TOS_text[40];
} TOState;

static int fails;

static void Check(int ok, int line, const char *what)
{
    if (!ok) {
	fails++;
	printf("FAIL line %d: %s\n", line, what);
    }
}
#define CHECK(c) Check(!!(c), __LINE__, #c)

#define WAIT_FOR(cond) \
    do { int n_; for (n_ = 0; n_ < 500 && !(cond); n_++) TimerSleep(1); \
    } while (0)

#define GEODE	0x2340

int main(void)
{
    static const char *const text[] = {
	"Hello, world!", "bye, world!?", "!!?bye!!", "abcdefgh", "bh",
	"in aa blockin", "aXYde", "aXYde", "aXYde"
    };
    GeodeHandle app;
    GeodeLoadError e;
    TOState *st;
    void (*step)(word);
    struct { word callerID; optr ackOD; } dp = { 0, 0 };
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    kernelCurrentGeode = GEODE;
    KernelThreadInit(PRIORITY_STANDARD);
    KernelTimerInit();
    GeodeRegisterImage("ui", ui_geode, ui_geode_size);
    GeodeRegisterImage("text", text_geode, text_geode_size);
    GeodeRegisterImage("textobj", textobj_geode, textobj_geode_size);

    app = GeodeLoad("textobj", GA_PROCESS, GA_LIBRARY, 0, 0, &e);
    CHECK(app != 0);
    if (!app) {
	printf("GeodeLoad error %d\n", (int) e);
	return 1;
    }
    CHECK(GeodeFind("text    ", GEODE_NAME_SIZE, 0, 0) != 0);
    st = ProcGetLibraryEntry(app, 0);
    step = (void (*)(word)) ProcGetLibraryEntry(app, 1);
    WAIT_FOR(st->TOS_attached);
    CHECK(st->TOS_attached == 1 && st->TOS_process == app);

    for (i = 1; i <= 9; i++) {
	step((word) i);
	if (strcmp(st->TOS_text, text[i - 1]))
	    printf("step %d: \"%s\"\n", i, st->TOS_text);
	CHECK(!strcmp(st->TOS_text, text[i - 1]));
    }
    printf("modified %u, not modified %u, empty %u, not empty %u\n",
	   st->TOS_modified, st->TOS_notModified, st->TOS_empty,
	   st->TOS_notEmpty);
    CHECK(st->TOS_modified == 3 && st->TOS_empty == 1);

    printf("geode checks: %u, failed %u (first at textobj.goc line %u)\n",
	   st->TOS_checks, st->TOS_fails, st->TOS_failLine);
    for (i = 0; i < st->TOS_fails && i < 8; i++)
	printf("  failed: textobj.goc line %u\n", st->TOS_failLines[i]);
    CHECK(st->TOS_checks >= 100 && !st->TOS_fails);

    /* detach: the process goes, and with it the libraries */
    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(app, 0), KMSG_META_DETACH,
		 &dp, sizeof(dp), NULL, 0);
    WAIT_FOR(!GeodeFind("textobj ", GEODE_NAME_SIZE, 0, 0));
    CHECK(!GeodeFind("textobj ", GEODE_NAME_SIZE, 0, 0));
    CHECK(!GeodeFind("text    ", GEODE_NAME_SIZE, 0, 0));
    CHECK(!GeodeFind("ui      ", GEODE_NAME_SIZE, 0, 0));

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
