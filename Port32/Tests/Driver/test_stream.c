/*
 * test_stream.c -- the stream driver (Driver/Stream/stream.c) in the C
 * kernel: stream.geo in SYSTEM, used by GeodeUseDriver.
 *   - create: too large, zero, a stream of 16 bytes (room 16, nothing to
 *     read), the structure as strDrInt.h has it;
 *   - writes and reads in bulk and byte by byte, across the end of the
 *     buffer; non-blocking short writes and reads, a byte with no room;
 *   - a data notifier (routine) with a threshold: called once it is
 *     reached, not again until a read acknowledges it;
 *   - a special reader (threshold 1): it takes some bytes itself (it
 *     is shown the first byte in the buffer);
 *   - errors: set by the writer, told to the reader's notifier, got and
 *     cleared;
 *   - flush;
 *   - a blocking read another thread fills, a blocking write another
 *     thread empties;
 *   - destroy with STREAM_LINGER, a thread reading the rest; destroy with
 *     STREAM_DISCARD wakes a reader blocked on it (short read).
 */
#define _GNU_SOURCE
#include "kernelC.h"
#include "ghal.h"
#include <geosdriver.h>
#include <Internal/strDrInt.h>
#include <heap.h>
#include <thread.h>
#include <timer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char stream_geode[];
extern const unsigned long stream_geode_size;

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static char top[260], sep = '/';

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

static DriverInfoStruct *dis;

static Boolean Call(word fn, DriverRegs *r)
{
    return DriverCall(dis, fn, r);
}

static StreamToken Create(word size, Boolean *fail, word *err)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_ax = size;
    r.DR_bx = GeodeGetProcessHandle();
    *fail = Call(DR_STREAM_CREATE, &r);
    *err = (word) r.DR_ax;
    return (StreamToken) r.DR_bx;
}

static word Write(StreamToken s, const void *p, word n, word block,
		  Boolean *fail)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_ax = block;
    r.DR_bx = s;
    r.DR_cx = n;
    r.DR_dsSi = (void *) p;
    *fail = Call(DR_STREAM_WRITE, &r);
    return (word) r.DR_cx;
}

static word Read(StreamToken s, void *p, word n, word block, Boolean *fail)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_ax = block;
    r.DR_bx = s;
    r.DR_cx = n;
    r.DR_dsSi = p;
    *fail = Call(DR_STREAM_READ, &r);
    return (word) r.DR_cx;
}

static word Query(StreamToken s, sword side)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) side;
    r.DR_bx = s;
    CHECK(!Call(DR_STREAM_QUERY, &r));
    return (word) r.DR_ax;
}

static void SetNotify(StreamToken s, word type, void *routine, word data)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_ax = type;
    r.DR_bx = s;
    r.DR_farPtr = routine;
    r.DR_bp = data;
    CHECK(!Call(DR_STREAM_SET_NOTIFY, &r));
}

static void Simple(word fn, StreamToken s, word ax, word cx)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_ax = ax;
    r.DR_bx = s;
    r.DR_cx = cx;
    CHECK(!Call(fn, &r));
}

/* notifiers */
static int dataCalls;
static word dataCount, dataSide, dataData;

static void _pascal DataNotify(word data, word count, word side)
{
    dataCalls++;
    dataData = data;
    dataCount = count;
    dataSide = side;
}

static int errCalls;
static word errValue;

static void _pascal ErrNotify(word data, word value, word side)
{
    (void) data; (void) side;
    errCalls++;
    errValue = value;
}

/* takes each 'x' itself */
static int taken;

static word _pascal TakeX(word data, word b, word side)
{
    (void) data; (void) side;
    if (b == 'x') {
	taken++;
	return 1;
    }
    return 0;
}

/* threads */
static StreamToken ts;
static SemaphoreHandle done;
static char got[64];
static word gotN;
static Boolean gotFail;

static word Writer(word v)
{
    Boolean f;

    (void) v;
    TimerSleep(3);
    (void) Write(ts, "0123456789", 10, STREAM_BLOCK, &f);
    ThreadVSem(done);
    return 0;
}

static word Reader(word n)
{
    TimerSleep(3);
    gotN = Read(ts, got, n, STREAM_BLOCK, &gotFail);
    ThreadVSem(done);
    return 0;
}

static word BlockedReader(word n)
{
    gotN = Read(ts, got, n, STREAM_BLOCK, &gotFail);
    ThreadVSem(done);
    return 0;
}

int main(int argc, char **argv)
{
    char *slash, buf[64];
    GeodeLoadError e;
    GeodeHandle drv;
    StreamToken s;
    StreamData *sd;
    Boolean fail;
    word err, i;
    DriverRegs r;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    strncpy(top, argv[0], sizeof(top) - 8);
    top[sizeof(top) - 8] = 0;
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash) {
	sep = *slash;
	slash[1] = 0;
    } else
	top[0] = 0;
    strcat(top, "GSTTOP");
    ghal_fs_mkdir(top);
    Put("SYSTEM", NULL, 0);
    Put("SYSTEM/stream.geo", stream_geode, stream_geode_size);
    CHECK(KernelBoot(top, "", &e) == 0 && e == GLE_OK);
    FileSetStandardPath(SP_SYSTEM);
    drv = GeodeUseDriver("stream.geo", 0, 0, &e);
    CHECK(drv != 0);
    if (!drv) {
	printf("stream.geo: GeodeLoadError %d\n", (int) e);
	return 1;
    }
    dis = GeodeInfoDriver(drv);
    CHECK(dis && dis->DIS_driverType == DRIVER_TYPE_STREAM);
    memset(&r, 0, sizeof(r));
    CHECK(!Call(DR_STREAM_GET_DEVICE_MAP, &r) && r.DR_ax == 0);

    /* create */
    (void) Create(40000, &fail, &err);
    CHECK(fail && err == STREAM_BUFFER_TOO_LARGE);
    (void) Create(0, &fail, &err);
    CHECK(fail && err == STREAM_CANNOT_ALLOC);
    s = Create(16, &fail, &err);
    CHECK(!fail && s);
    sd = StreamDataOf(s);
    CHECK(sd->SD_handle == (MemHandle) s);
    CHECK(sd->SD_max == SD_DATA_OFFSET + 16);
    CHECK(Query(s, STREAM_WRITE) == 16 && Query(s, STREAM_READ) == 0);

    /* bulk and bytes, across the end */
    CHECK(Write(s, "abcdefghij", 10, STREAM_BLOCK, &fail) == 10 && !fail);
    CHECK(Query(s, STREAM_READ) == 10 && Query(s, STREAM_WRITE) == 6);
    CHECK(Read(s, buf, 4, STREAM_BLOCK, &fail) == 4 && !fail);
    CHECK(!memcmp(buf, "abcd", 4));
    CHECK(Write(s, "0123456789", 10, STREAM_BLOCK, &fail) == 10 && !fail);
    CHECK(Query(s, STREAM_READ) == 16 && Query(s, STREAM_WRITE) == 0);
    memset(&r, 0, sizeof(r));
    r.DR_ax = STREAM_NO_BLOCK;
    r.DR_bx = s;
    r.DR_cx = 'Z';
    CHECK(Call(DR_STREAM_WRITE_BYTE, &r) && r.DR_ax == STREAM_WOULD_BLOCK);
    CHECK(Write(s, "zz", 2, STREAM_NO_BLOCK, &fail) == 0 && !fail);
    memset(buf, 0, sizeof(buf));
    CHECK(Read(s, buf, 20, STREAM_NO_BLOCK, &fail) == 16 && !fail);
    CHECK(!memcmp(buf, "efghij0123456789", 16));
    CHECK(sd->SD_reader.SSD_ptr == sd->SD_writer.SSD_ptr);
    memset(&r, 0, sizeof(r));
    r.DR_ax = STREAM_NO_BLOCK;
    r.DR_bx = s;
    CHECK(Call(DR_STREAM_READ_BYTE, &r) && r.DR_ax == STREAM_WOULD_BLOCK);
    Simple(DR_STREAM_WRITE_BYTE, s, STREAM_BLOCK, 'Q');
    memset(&r, 0, sizeof(r));
    r.DR_ax = STREAM_BLOCK;
    r.DR_bx = s;
    CHECK(!Call(DR_STREAM_READ_BYTE, &r) && (byte) r.DR_ax == 'Q');

    /* a data notifier on the reader with a threshold of 4 */
    Simple(DR_STREAM_SET_THRESHOLD, s, (word) STREAM_READ, 4);
    SetNotify(s, SNT_READER | (SNE_DATA << SNT_EVENT_OFFSET) | SNM_ROUTINE,
	      (void *) DataNotify, 0x55);
    CHECK(dataCalls == 0);
    (void) Write(s, "123", 3, STREAM_BLOCK, &fail);
    CHECK(dataCalls == 0);
    (void) Write(s, "4", 1, STREAM_BLOCK, &fail);
    CHECK(dataCalls == 1 && dataCount == 4 && dataData == 0x55 &&
	  dataSide == (word) STREAM_READ);
    (void) Write(s, "5", 1, STREAM_BLOCK, &fail);
    CHECK(dataCalls == 1);			/* the ack is pending */
    CHECK(Read(s, buf, 5, STREAM_BLOCK, &fail) == 5 && !memcmp(buf, "12345", 5));
    (void) Write(s, "6789", 4, STREAM_BLOCK, &fail);
    CHECK(dataCalls == 2 && dataCount == 4);
    (void) Read(s, buf, 4, STREAM_BLOCK, &fail);
    SetNotify(s, SNT_READER | (SNE_DATA << SNT_EVENT_OFFSET) | SNM_NONE,
	      NULL, 0);

    /* a special reader: it takes the x */
    Simple(DR_STREAM_SET_THRESHOLD, s, (word) STREAM_READ, 1);
    SetNotify(s, SNT_READER | (SNE_DATA << SNT_EVENT_OFFSET) | SNM_ROUTINE,
	      (void *) TakeX, 0);
    /* (it is shown the first byte in the buffer: once one is left, it
     * sees that one again, as in the asm) */
    for (i = 0; i < 4; i++)
	Simple(DR_STREAM_WRITE_BYTE, s, STREAM_BLOCK, "xxbx"[i]);
    CHECK(taken == 2 && Query(s, STREAM_READ) == 2);
    SetNotify(s, SNT_READER | (SNE_DATA << SNT_EVENT_OFFSET) | SNM_NONE,
	      NULL, 0);
    CHECK(Read(s, buf, 2, STREAM_BLOCK, &fail) == 2 && !memcmp(buf, "bx", 2));

    /* errors: the writer's for the reader */
    SetNotify(s, SNT_READER | (SNE_ERROR << SNT_EVENT_OFFSET) | SNM_ROUTINE,
	      (void *) ErrNotify, 0);
    Simple(DR_STREAM_SET_ERROR, s, (word) STREAM_WRITE, 0x123);
    CHECK(errCalls == 1 && errValue == 0x123);
    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) STREAM_READ;
    r.DR_bx = s;
    CHECK(!Call(DR_STREAM_GET_ERROR, &r) && r.DR_ax == 0x123);
    CHECK(!Call(DR_STREAM_GET_ERROR, &r) && r.DR_ax == 0);

    /* flush */
    (void) Write(s, "abcdefghijkl", 12, STREAM_BLOCK, &fail);
    Simple(DR_STREAM_FLUSH, s, 0, 0);
    CHECK(Query(s, STREAM_READ) == 0 && Query(s, STREAM_WRITE) == 16);
    CHECK(sd->SD_reader.SSD_ptr == sd->SD_writer.SSD_ptr);

    /* a blocking read another thread fills */
    ts = s;
    done = ThreadAllocSem(0);
    CHECK(ThreadCreate(PRIORITY_STANDARD, 0, Writer, 1024,
		       GeodeGetProcessHandle()) != 0);
    memset(buf, 0, sizeof(buf));
    CHECK(Read(s, buf, 10, STREAM_BLOCK, &fail) == 10 && !fail);
    CHECK(!memcmp(buf, "0123456789", 10));
    ThreadPSem(done);
    /* a blocking write another thread empties: 30 bytes into 16 */
    CHECK(ThreadCreate(PRIORITY_STANDARD, 30, Reader, 1024,
		       GeodeGetProcessHandle()) != 0);
    CHECK(Write(s, "abcdefghijklmnopqrstuvwxyz0123", 30, STREAM_BLOCK,
		&fail) == 30 && !fail);
    ThreadPSem(done);
    CHECK(gotN == 30 && !gotFail && !memcmp(got, "abcdefghijklmnopqrstuvwxyz0123",
					     30));

    /* destroy, lingering: a thread reads the rest */
    (void) Write(s, "linger", 6, STREAM_BLOCK, &fail);
    CHECK(ThreadCreate(PRIORITY_STANDARD, 6, Reader, 1024,
		       GeodeGetProcessHandle()) != 0);
    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) STREAM_LINGER;
    r.DR_bx = s;
    CHECK(!Call(DR_STREAM_DESTROY, &r));
    ThreadPSem(done);
    CHECK(gotN == 6 && !memcmp(got, "linger", 6));

    /* destroy, discarding: a reader blocked on it is let go short */
    s = Create(8, &fail, &err);
    CHECK(!fail);
    ts = s;
    (void) Write(s, "ab", 2, STREAM_BLOCK, &fail);
    CHECK(ThreadCreate(PRIORITY_STANDARD, 5, BlockedReader, 1024,
		       GeodeGetProcessHandle()) != 0);
    TimerSleep(3);
    memset(&r, 0, sizeof(r));
    r.DR_ax = STREAM_DISCARD;
    r.DR_bx = s;
    CHECK(!Call(DR_STREAM_DESTROY, &r));
    ThreadPSem(done);
    CHECK(gotFail && gotN == 2 && !memcmp(got, "ab", 2));
    ThreadFreeSem(done);

    GeodeFreeDriver(drv);
    printf("%s\n", fails ? "FAILED" : "stream: ok");
    return fails != 0;
}
