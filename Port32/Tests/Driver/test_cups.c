/*
 * test_cups.c -- the CUPS port driver (Driver/Stream/Cups) in a booted
 * kernel, against the stand-in for CUPS (fakecups.c).
 *
 * As the spooler drives a custom port (processCustom.asm):
 * STREAM_ESC_LOAD_OPTIONS with the printer's name, DR_STREAM_OPEN with
 * the CPP_info block, writes of the printer driver's output, then
 * DR_STREAM_CLOSE with STREAM_LINGER (printed) or STREAM_DISCARD
 * (dropped).  Nothing reaches a real printer.
 */
#define _GNU_SOURCE
#include "kernelC.h"
#include "ghal.h"
#include "fakecups.h"
#include "cupsDr.h"
#include <geosdriver.h>
#include <streamC.h>
#include <initfile.h>
#include <file.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char cups_geode[];
extern const unsigned long cups_geode_size;

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static char top[260], sep = '/';
static DriverInfoStruct *dis;

/* the output of a "printer driver": longer than the driver's buffer */
static char doc[3 * CUPS_BUFFER_SIZE + 123];

static void Put(const char *rel, const void *data, unsigned long size)
{
    char path[300], *p;
    ghal_file *f;
    size_t done = 0;

    sprintf(path, "%s%c%s", top, sep, rel);
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

static Boolean Call(word fn, DriverRegs *r)
{
    return DriverCall(dis, fn, r);
}

/* load options and open as the spooler does; the unit, or -1 */
static long OpenPort(const char *printer, const char *cppQueue)
{
    DriverRegs r;
    MemHandle info = 0;

    memset(&r, 0, sizeof(r));
    r.DR_dsSi = (void *) printer;
    CHECK(!Call(STREAM_ESC_LOAD_OPTIONS, &r));
    if (cppQueue) {
	char *p;
	info = MemAlloc(64, HF_DYNAMIC, HAF_ZERO_INIT);
	p = MemLock(info);
	strcpy(p, cppQueue);
	MemUnlock(info);
    }
    memset(&r, 0, sizeof(r));
    r.DR_ax = STREAM_OPEN_NO_BLOCK;
    r.DR_bx = info;
    r.DR_dx = 1024;
    if (Call(DR_STREAM_OPEN, &r)) {
	if (info)
	    MemFree(info);
	return -(long) r.DR_ax - 1;
    }
    if (info)
	MemFree(info);
    return (long) r.DR_bx;
}

static Boolean Write(long unit, const void *p, word n)
{
    DriverRegs r;
    memset(&r, 0, sizeof(r));
    r.DR_ax = STREAM_BLOCK;
    r.DR_bx = (dword) unit;
    r.DR_cx = n;
    r.DR_dsSi = (void *) p;
    if (Call(DR_STREAM_WRITE, &r))
	return TRUE;
    return r.DR_cx != n;
}

static void ClosePort(long unit, int linger)
{
    DriverRegs r;
    memset(&r, 0, sizeof(r));
    r.DR_ax = linger ? (dword) (word) STREAM_LINGER : STREAM_DISCARD;
    r.DR_bx = (dword) unit;
    CHECK(!Call(DR_STREAM_CLOSE, &r));
}

int main(int argc, char **argv)
{
    static const char ini[] =
	"[Office Laser]\r\nhostQueue = fake1\r\nhostDuplex = true\r\n";
    GeodeLoadError e;
    GeodeHandle drv;
    DriverRegs r;
    char got[32], *slash;
    long unit, u2;
    pid_t child;
    size_t i;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 0; i < sizeof(doc); i++)
	doc[i] = (char) ('A' + i % 26);
    memcpy(doc, "%!PS-Adobe-3.0\n", 15);

    strncpy(top, argv[0], sizeof(top) - 8);
    top[sizeof(top) - 8] = 0;
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash) {
	sep = *slash;
	strcpy(slash + 1, "GCUPSTOP");
    } else
	strcpy(top, "GCUPSTOP");
    ghal_fs_mkdir(top);
    Put("SYSTEM", NULL, 0);
    Put("SYSTEM/cups.geo", cups_geode, cups_geode_size);
    Put("geos.ini", ini, sizeof(ini) - 1);

    unsetenv("LPDEST");
    unsetenv("PRINTER");
    fakeCupsJob.uri = "ipp://localhost/printers/fake1";
    fakeCupsJob.format = "application/postscript";
    fakeCupsJob.name = "GEOS";
    fakeCupsJob.copies = NULL;
    fakeCupsJob.sides = "two-sided-long-edge";
    fakeCupsJob.doc = doc;
    fakeCupsJob.docLen = sizeof(doc);
    child = FakeCupsStart();
    if (!child) {
	printf("FAIL: no stand-in for CUPS\n");
	return 1;
    }

    CHECK(KernelBoot(top, "", &e) == 0 && e == GLE_OK);
    FileSetStandardPath(SP_SYSTEM);
    drv = GeodeUseDriver("cups.geo", 0, 0, &e);
    CHECK(drv != 0);
    if (!drv) {
	printf("cups.geo: GeodeLoadError %d\n", (int) e);
	FakeCupsStop(child, got, sizeof(got));
	return 1;
    }
    dis = GeodeInfoDriver(drv);
    CHECK(dis && dis->DIS_driverType == DRIVER_TYPE_STREAM);

    /* the escapes it knows */
    memset(&r, 0, sizeof(r));
    r.DR_ax = STREAM_ESC_LOAD_OPTIONS;
    CHECK(!Call(DRV_ESC_QUERY_ESC, &r) && r.DR_ax == STREAM_ESC_LOAD_OPTIONS);
    r.DR_ax = 0x8123;
    CHECK(!Call(DRV_ESC_QUERY_ESC, &r) && r.DR_ax == 0);

    /* a job of the printer "Office Laser": queue and duplex from the ini;
     * written in odd pieces and one byte, across the driver's buffer */
    unit = OpenPort("Office Laser", NULL);
    CHECK(unit >= 0);
    CHECK(!Write(unit, doc, 7));
    memset(&r, 0, sizeof(r));
    r.DR_bx = (dword) unit;
    r.DR_cx = (byte) doc[7];
    CHECK(!Call(DR_STREAM_WRITE_BYTE, &r));
    for (i = 8; i < sizeof(doc); ) {
	word n = (word) (sizeof(doc) - i < 1000 ? sizeof(doc) - i : 1000);
	CHECK(!Write(unit, doc + i, n));
	i += n;
    }
    memset(&r, 0, sizeof(r));
    r.DR_bx = (dword) unit;
    CHECK(!Call(DR_STREAM_GET_ERROR, &r) && r.DR_ax == 0);
    CHECK(Call(DR_STREAM_READ_BYTE, &r));		/* write only */
    ClosePort(unit, 1);
    r.DR_bx = (dword) unit;
    CHECK(Call(DR_STREAM_WRITE_BYTE, &r) && r.DR_ax == STREAM_CLOSED);

    /* a cancelled one (STREAM_DISCARD after data went out) */
    unit = OpenPort("Office Laser", NULL);
    CHECK(unit >= 0);
    CHECK(!Write(unit, doc, CUPS_BUFFER_SIZE));
    ClosePort(unit, 0);

    /* opened and closed without output: no job at all */
    unit = OpenPort("Office Laser", NULL);
    ClosePort(unit, 1);

    /* the queue from CPP_info wins; this one is not what the server
     * wants (fake2, one-sided): it refuses the job, the close says so */
    unit = OpenPort("Office Laser", "fake2");
    u2 = OpenPort("Other", "fake1");		/* a second unit */
    CHECK(unit >= 0 && u2 >= 0 && u2 != unit);
    CHECK(!Write(unit, doc, 100));
    ClosePort(unit, 1);
    ClosePort(u2, 0);

    /* no CUPS: the first full buffer fails, the error stays */
    setenv("CUPS_SERVER", "/nonexistent/cups.sock", 1);
    unit = OpenPort("Office Laser", NULL);
    CHECK(unit >= 0);
    CHECK(Write(unit, doc, CUPS_BUFFER_SIZE));
    memset(&r, 0, sizeof(r));
    r.DR_bx = (dword) unit;
    CHECK(!Call(DR_STREAM_GET_ERROR, &r) && r.DR_ax == CUPS_ERR_HOST);
    CHECK(Write(unit, doc, 1));
    ClosePort(unit, 1);
    /* and no default printer to fall back on */
    CHECK(OpenPort("Unknown", NULL) == -(long) STREAM_NO_DEVICE - 1);

    GeodeFreeDriver(drv);
    FakeCupsStop(child, got, sizeof(got));
    printf("the server saw: %s\n", got);
    CHECK(!strcmp(got, "JCu"));
    printf("%s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
