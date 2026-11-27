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
 *   9. the character attribute;
 *  10. the lines (the system is booted from a GEOS top, GTXTOP, for
 *      Berkeley of USERDATA/FONT): word wrap, justification, a
 *      carriage return, a tab.
 *  11. the sizes for the geometry.
 *  12. the filters.
 *  13. the character attribute runs.
 *  14. the paragraph attribute runs.
 *  15. a position shown (not on a screen: nothing to scroll).
 *  16. the notifications for the controllers.
 *  17. overstrike mode, smart quotes.
 *  18. scrolling a view on whole lines.
 *  19. a paragraph's border and background drawn.
 *  20. a tab's leader and line drawn.
 */
#include "kernelC.h"
#include "ghal.h"
#include <Internal/videoDr.h>
#include <driver.h>

#include <stdio.h>
#include <string.h>

extern const unsigned char ui_geode[], text_geode[], textobj_geode[],
			   berkeley_fnt[], vidmem_geode[];
extern const unsigned long vidmem_geode_size;
extern const unsigned long berkeley_fnt_size;
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
    word	TOS_line[16];
    word	TOS_geo[6];
    word	TOS_filter[2];
    word	TOS_shown;
    word	TOS_generated;
    word	TOS_types, TOS_send;
    MemHandle	TOS_countBlock;
    word	TOS_border[5];
    word	TOS_tab[2];
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

static char top[260], sep = '/';

/* a directory (no data) or a file of the GEOS top */
static void Put(const char *rel, const void *data, unsigned long size)
{
    char path[300], *p;
    ghal_file *f;
    size_t done = 0;

    snprintf(path, sizeof(path), "%s%c%s", top, sep, rel);
    for (p = path; *p; p++)
	if (*p == '/')
	    *p = sep;
    if (!size) {
	ghal_fs_mkdir(path);
	return;
    }
    CHECK(ghal_fs_open(path, GHAL_FS_WRITE | GHAL_FS_CREATE |
				 GHAL_FS_TRUNCATE, &f) == GHAL_OK);
    ghal_fs_write(f, data, size, &done);
    CHECK(done == size);
    ghal_fs_close(f);
}

int main(int argc, char **argv)
{
    static const char *const text[] = {
	"Hello, world!", "bye, world!?", "!!?bye!!", "abcdefgh", "bh",
	"in aa blockin", "aXYde", "aXYde", "aXYde", "Hi Hi Hi",
	"Hi Hi Hi Hi Hi", "a12b-cE -.F14y#", "a12b-cE -.F14y#",
	"a12b-cE -.F14y#", "a12b-cE -.F14y#", "a12b-cE -.F14y#",
	"a12b-cE -.F14y#", "a12b-cE -.F14y#", "a12b-cE -.F14y#",
	"a12b-cE -.F14y#"
    };
    GeodeHandle app, mdrv;
    GeodeLoadError e;
    TOState *st;
    void (*step)(word);
    struct { word callerID; optr ackOD; } dp = { 0, 0 };
    char *slash;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    (void) argc;
    strncpy(top, argv[0], sizeof(top) - 8);
    top[sizeof(top) - 8] = 0;
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash) {
	sep = *slash;
	strcpy(slash + 1, "GTXTOP");
    } else
	strcpy(top, "GTXTOP");
    ghal_fs_mkdir(top);
    Put("USERDATA", NULL, 0);
    Put("USERDATA/FONT", NULL, 0);
    Put("USERDATA/FONT/Berkeley.fnt", berkeley_fnt, berkeley_fnt_size);
    Put("SYSTEM", NULL, 0);
    Put("SYSTEM/VIDEO", NULL, 0);
    Put("SYSTEM/VIDEO/vidmem.geo", vidmem_geode, vidmem_geode_size);
    CHECK(KernelBoot(top, "", &e) == 0 && e == GLE_OK);
    kernelCurrentGeode = GEODE;
    GeodeRegisterImage("ui", ui_geode, ui_geode_size);
    GeodeRegisterImage("text", text_geode, text_geode_size);
    GeodeRegisterImage("textobj", textobj_geode, textobj_geode_size);
    /* bitmaps to draw into (step 19) */
    FileSetStandardPath(SP_VIDEO_DRIVERS);
    mdrv = GeodeUseDriver("vidmem.geo", VIDEO_PROTO_MAJOR,
			  VIDEO_PROTO_MINOR, &e);
    CHECK(mdrv != 0);
    GeodeSetDefaultDriver(GDDT_MEMORY_VIDEO, mdrv);
    FileSetStandardPath(SP_TOP);

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

    for (i = 1; i <= 20; i++) {
	step((word) i);
	if (strcmp(st->TOS_text, text[i - 1]))
	    printf("step %d: \"%s\"\n", i, st->TOS_text);
	CHECK(!strcmp(st->TOS_text, text[i - 1]));
    }
    printf("lines:");
    for (i = 0; i < 16; i++)
	printf(" %u", st->TOS_line[i]);
    printf("\n");
    printf("geometry: line %u, average %u, min %u, simple min %u; "
	   "height at 100: %u, cached %u\n",
	   st->TOS_geo[0], st->TOS_geo[1], st->TOS_geo[2], st->TOS_geo[3],
	   st->TOS_geo[4], st->TOS_geo[5]);
    printf("modified %u, not modified %u, empty %u, not empty %u\n",
	   st->TOS_modified, st->TOS_notModified, st->TOS_empty,
	   st->TOS_notEmpty);
    /* (an attribute change marks the text user modified, as the asm's
     * ReflectChange: once in step 9, once in step 11) */
    CHECK(st->TOS_modified == 5 && st->TOS_empty == 1);

    printf("border: height %u, baseline %u, left at %u, pixels %x %x\n",
	   st->TOS_border[0], st->TOS_border[1], st->TOS_border[2],
	   st->TOS_border[3], st->TOS_border[4]);
    printf("tab: leader %x, line %x\n", st->TOS_tab[0], st->TOS_tab[1]);
    printf("notify: generated %u, types %x, send %x\n", st->TOS_generated, st->TOS_types, st->TOS_send);
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
