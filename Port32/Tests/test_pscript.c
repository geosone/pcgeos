/*
 * test_pscript.c -- the PostScript printer driver (pscript.geo, the C
 * port of Driver/Printer/PScript and the PrintCom parts it uses) in a
 * booted kernel, with the translation library (eps.geo) it loads.
 *
 * As the spooler drives it: the device, the mode and the stream, a job
 * with a page (the page transform and a graphics string), the end of
 * the job; the output goes to a stream whose strategy (here) collects
 * it.  Then a job on a device with custom job routines (the PJL codes of
 * the LaserJet 4).  Where Ghostscript is on the host (GEOS_TEST_GS, or
 * gs in the PATH on linux), it must run the output without an error.
 */
#define _GNU_SOURCE			/* popen */
#include "kernelC.h"
#include "ghal.h"
#include <geosdriver.h>
#include <streamC.h>
#include <library.h>
#include <lmem.h>
#include <file.h>
#include <chunkarr.h>
#include <font.h>
#include <print.h>
#include <Internal/streamDr.h>
#include <Internal/printDr.h>
#include <xlatPS.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char eps_geode[];
extern const unsigned long eps_geode_size;
extern const unsigned char pscript_geode[];
extern const unsigned long pscript_geode_size;

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

/* ---------------------------------------------------------------------
 * The stream: everything written goes to out
 * ------------------------------------------------------------------- */

#define TOKEN 0x4321

static char out[65536];
static size_t outLen;

static void Collect(const void *p, size_t n)
{
    if (outLen + n < sizeof(out)) {
	memcpy(out + outLen, p, n);
	outLen += n;
    }
}

static Boolean Strategy(word fn, DriverRegs *r)
{
    byte b;

    if (r->DR_bx != TOKEN || r->DR_ax != STREAM_BLOCK)
	return TRUE;
    switch (fn) {
    case DR_STREAM_WRITE:
	Collect(r->DR_dsSi, r->DR_cx);
	return FALSE;
    case DR_STREAM_WRITE_BYTE:
	b = (byte) r->DR_cx;
	Collect(&b, 1);
	return FALSE;
    }
    return TRUE;
}

static size_t At(const char *s, size_t n)
{
    size_t i;

    for (i = 0; i + n <= outLen; i++)
	if (!memcmp(out + i, s, n))
	    return i;
    return (size_t) -1;
}

static int Has(const char *s)
{
    return At(s, strlen(s)) != (size_t) -1;
}

#define HAS(s) do { if (!Has(s)) { fails++; \
    printf("FAIL line %d: no \"%s\"\n", __LINE__, s); } } while (0)

/* ---------------------------------------------------------------------
 * The driver
 * ------------------------------------------------------------------- */

static DriverInfoStruct *dis;
static MemHandle pstate;

static Boolean Call(word fn, DriverRegs *r)
{
    r->DR_bx = pstate;
    return DriverCall(dis, fn, r);
}

static Boolean Call0(word fn)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    return Call(fn, &r);
}

static Boolean SetDevice(const char *name, DriverRegs *r)
{
    memset(r, 0, sizeof(*r));
    r->DR_dsSi = (void *) name;
    return Call(DRE_SET_DEVICE, r);
}

/* a new PState with its JobParameters, as the spooler makes it */
static PState *NewPState(void)
{
    PState *ps;

    pstate = MemAlloc(sizeof(PState) + sizeof(JobParameters), HF_DYNAMIC,
		      HAF_ZERO_INIT);
    ps = MemLock(pstate);
    return ps;
}

/* the stream: no stream driver here, the strategy is set directly */
static void SetStream(void)
{
    DriverRegs r;
    PState *ps;

    memset(&r, 0, sizeof(r));
    r.DR_cx = TOKEN;
    r.DR_dx = 0;
    r.DR_si = PPT_CUSTOM;
    CHECK(!Call(DR_PRINT_SET_STREAM, &r));
    ps = MemLock(pstate);
    CHECK(ps->PS_streamToken == TOKEN && ps->PS_streamType == PPT_CUSTOM);
    ps->PS_streamStrategy = Strategy;
    MemUnlock(pstate);
}

static void FillJob(const char *doc, byte copies, SpoolOptions opts)
{
    PState *ps = MemLock(pstate);
    JobParameters *jp = PS_JOB_PARAMS(ps);
    PrintDriverUIData *ui = (PrintDriverUIData *) jp->JP_printerData;

    strcpy(jp->JP_parent, "Port32 Test");
    strcpy(jp->JP_documentName, doc);
    jp->JP_numPages = 1;
    jp->JP_numCopies = copies;
    jp->JP_spoolOpts = opts;
    jp->JP_docSizeInfo.PSR_width = 612;
    jp->JP_docSizeInfo.PSR_height = 792;
    jp->JP_portInfo.PPI_type = PPT_CUSTOM;
    ui->PUID_paperInput = ASF_TRAY1 << PIO_ASF_OFFSET;
    MemUnlock(pstate);
}

/* Ghostscript runs the file without an error: 1, 0 if not, -1 if there
 * is no Ghostscript */
static int Ghostscript(const char *file)
{
    char cmd[600], line[256];
    const char *gs = getenv("GEOS_TEST_GS");
    FILE *p;
    int ok = 1;

#ifdef __linux__
    if (!gs && system("command -v gs >/dev/null 2>&1") == 0)
	gs = "gs";
#endif
    if (!gs || !*gs)
	return -1;
    snprintf(cmd, sizeof(cmd), "%s -q -dSAFER -dNOPAUSE -dBATCH "
	     "-sDEVICE=nullpage %s 2>&1", gs, file);
    p = popen(cmd, "r");
    if (!p)
	return -1;
    while (fgets(line, sizeof(line), p)) {
	printf("gs: %s", line);
	ok = 0;
    }
    if (pclose(p) != 0)
	ok = 0;
    return ok;
}

int main(int argc, char **argv)
{
    static const char patch[] = "% devpatch\r\n";
    static const char pjlStart[] = "\x1b%-12345X@PJL\r\n"
	"@PJL RDYMSG DISPLAY = \"GEOS PS JOB\"\r\n"
	"@PJL ENTER LANGUAGE = POSTSCRIPT\r\n%!PS-Adobe-3.0\r\n";
    static const char pjlEnd[] = "%%Trailer\r\n\x04" "\x1b%-12345X@PJL\r\n"
	"@PJL RDYMSG DISPLAY = \"\"\r\n\x1b%-12345X";
    GeodeLoadError e;
    GeodeHandle drv;
    DriverExtendedInfoStruct *deis;
    const PrintDriverInfo *pdi;
    const PrinterInfo *pi;
    const GraphicsProperties *gp;
    DriverRegs r;
    PState *ps;
    TransMatrix tm;
    MemHandle blk;
    GStateHandle gs;
    word ch = 0;
    char *slash, file[300];
    FILE *f;
    int res;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    strncpy(top, argv[0], sizeof(top) - 8);
    top[sizeof(top) - 8] = 0;
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash) {
	sep = *slash;
	strcpy(slash + 1, "GPSCTOP");
    } else
	strcpy(top, "GPSCTOP");
    ghal_fs_mkdir(top);
    Put("SYSTEM", NULL, 0);
    Put("SYSTEM/PRINTER", NULL, 0);
    Put("SYSTEM/IMPEX", NULL, 0);
    Put("SYSTEM/PRINTER/pscript.geo", pscript_geode, pscript_geode_size);
    Put("SYSTEM/IMPEX/eps.geo", eps_geode, eps_geode_size);
    Put("SYSTEM/devpatch.ps", patch, sizeof(patch) - 1);

    CHECK(KernelBoot(top, "", &e) == 0 && e == GLE_OK);
    FileSetStandardPath(SP_PRINTER_DRIVERS);
    drv = GeodeUseDriver("pscript.geo", 0, 0, &e);
    CHECK(drv != 0);
    if (!drv) {
	printf("pscript.geo: GeodeLoadError %d\n", (int) e);
	return 1;
    }
    dis = GeodeInfoDriver(drv);
    deis = (DriverExtendedInfoStruct *) dis;
    CHECK(dis->DIS_driverType == DRIVER_TYPE_PRINTER &&
	  (dis->DIS_driverAttributes & DA_HAS_EXTENDED_INFO) &&
	  deis->DEIS_resource == 0);

    /* without a PState */
    memset(&r, 0, sizeof(r));
    CHECK(!DriverCall(dis, DRE_TEST_DEVICE, &r) && r.DR_ax == DP_CANT_TELL);

    /* from DRE_SET_DEVICE on, bx is the PState (DR_PRINT_DRIVER_INFO
     * too, as in the asm) */
    ps = NewPState();
    MemUnlock(pstate);
    memset(&r, 0, sizeof(r));
    CHECK(!Call(DR_PRINT_DRIVER_INFO, &r) && r.DR_dsSi);
    pdi = r.DR_dsSi;
    CHECK(pdi->PDI_timeoutValue == 90 && pdi->PDI_resendOrNot == PR_RESEND &&
	  pdi->PDI_driverType == PDT_PRINTER);

    /* the device */
    CHECK(SetDevice("No Such Printer", &r) && r.DR_ax == DP_INVALID_DEVICE);
    CHECK(!SetDevice("PostScript Compatible", &r) && r.DR_ax == DP_PRESENT);
    ps = MemLock(pstate);
    CHECK(ps->PS_device == 28 * 2);	/* PD_GENERIC_PS, the 29th */
    MemUnlock(pstate);
    memset(&r, 0, sizeof(r));
    CHECK(!Call(DR_PRINT_DEVICE_INFO, &r) && r.DR_dsSi && r.DR_dx == 0);
    pi = r.DR_dsSi;
    CHECK(pi->PI_smarts == PS_PDL && pi->PI_paperWidth == 612 &&
	  (pi->PI_type & PT_COLOR) == BMF_MONO &&
	  (pi->PI_connect & (PC_CENTRONICS | PC_FILE | PC_CUSTOM)) ==
	  (PC_CENTRONICS | PC_FILE) &&
	  pi->PI_customEntry == 0 && pi->PI_marginASF.PM_top == 18);
    /* esp filled the record by position: MF_MANUAL1 into PIO_MANUAL,
     * TF_NO_TRACTOR into PIO_ASF, ASF_TRAY1 into PIO_TRACTOR */
    CHECK(pi->PI_paperInput == 0x21 && pi->PI_paperOutput == 0x80);
    gp = (const GraphicsProperties *) ((const byte *) pi + pi->PI_hiRes);
    CHECK(pi->PI_lowRes == 0 && gp->GP_xres == 300 && gp->GP_yres == 300);

    /* the mode, the stream */
    memset(&r, 0, sizeof(r));
    r.DR_cx = PM_GRAPHICS_HI_RES;
    r.DR_ax = 612;
    r.DR_si = 792;
    CHECK(!Call(DR_PRINT_SET_MODE, &r));
    SetStream();
    memset(&r, 0, sizeof(r));
    CHECK(!Call(DR_PRINT_GET_COLOR_FORMAT, &r) && r.DR_ax == BMF_MONO);
    memset(&r, 0, sizeof(r));
    CHECK(!Call(DR_PRINT_GET_PAPER_PATH, &r) && r.DR_ax == 0x8021);

    /* the job: two copies, not collated, so PostScript makes them */
    FillJob("Shapes", 2, 0);
    CHECK(!Call0(DR_PRINT_START_JOB));
    ps = MemLock(pstate);
    CHECK(ps->PS_epsLibrary != 0 && ps->PS_expansionInfo != 0 &&
	  PS_JOB_PARAMS(ps)->JP_numCopies == 1 &&
	  ps->PS_currentMargins.PM_left == 18);
    MemUnlock(pstate);
    CHECK(outLen > 1000 && !memcmp(out, "%!PS-Adobe-3.0\r\n", 16));
    HAS("%%Creator: Port32 Test\r\n");
    HAS("%%Title: Shapes\r\n");
    HAS("%%Requirements: numcopies(2)\r\n");
    HAS("%%BeginProlog");
    HAS("GWDict begin\r\n/SDC { 85 35 currentscreen");	/* the device's */
    HAS("% devpatch\r\n");
    CHECK(At("% devpatch", 10) > At("/SDC {", 6));

    memset(&r, 0, sizeof(r));
    CHECK(!Call(DR_PRINT_GET_PRINT_AREA, &r) && r.DR_ax == 18 &&
	  r.DR_si == 18 && r.DR_cx == 612 - 18 && r.DR_dx == 792 - 18);
    memset(&r, 0, sizeof(r));
    CHECK(!Call(DR_PRINT_GET_MARGINS, &r) && r.DR_ax == 18 &&
	  r.DR_cx == 18);

    /* the page */
    CHECK(!Call0(DR_PRINT_START_PAGE));
    HAS("%%EndProlog");
    HAS("/#copies 2 def");
    HAS("%%Page: 1 1\r\n");
    memset(&tm, 0, sizeof(tm));
    tm.TM_e11.WWF_int = 1;
    tm.TM_e12.WWF_frac = 0x5555;		/* 1/3 */
    tm.TM_e22.WWF_frac = 0x8000;		/* 0.5 */
    tm.TM_e31.DWF_int = 10;
    tm.TM_e31.DWF_frac = 0x4000;		/* 10.25 */
    tm.TM_e32.DWF_int = -20;
    memset(&r, 0, sizeof(r));
    r.DR_dsSi = &tm;
    CHECK(!Call(DR_PRINT_ESC_SET_PAGE_TRANSFORM, &r));
    HAS("[1 0.3334 0 0.5 10.25 -20] SPT\r\n612 792 SPS\r\n");

    blk = MemAllocLMem(LMEM_TYPE_GENERAL, 0);
    gs = GrCreateGString(blk, GST_CHUNK, &ch);
    GrDrawLine(gs, 10, 10, 200, 10);
    GrSetAreaColor(gs, CF_RGB, 0xff, 0, 0);
    GrFillRect(gs, 220, 20, 300, 80);
    CHECK(GrEndGString(gs) == GSET_NO_ERROR);
    GrDestroyGString(gs, 0, GSKT_LEAVE_DATA);
    gs = GrLoadGString(blk, GST_CHUNK, ch);
    memset(&r, 0, sizeof(r));
    r.DR_cx = 0;
    r.DR_si = gs;
    CHECK(!Call(DR_PRINT_ESC_PRINT_GSTRING, &r) &&
	  r.DR_ax == GSRT_COMPLETE);
    CHECK(At("%%EndPageSetup", 14) > At("SPS\r\n", 5));
    HAS("200 10 10 10  DL");
    HAS("300 80 220 20  FR");
    GrDestroyGString(gs, 0, GSKT_LEAVE_DATA);
    MemFree(blk);
    CHECK(!Call0(DR_PRINT_END_PAGE));
    HAS("%%PageTrailer");

    /* what the driver does not do */
    memset(&r, 0, sizeof(r));
    r.DR_di = 1;
    CHECK(!Call(DR_PRINT_ESC_PRINT_BITMAP, &r) && r.DR_di == 0);
    CHECK(Call0(DR_PRINT_SWATH));
    CHECK(Call0(0x60));

    /* the end */
    CHECK(!Call0(DR_PRINT_END_JOB));
    CHECK(outLen > 12 && !memcmp(out + outLen - 12, "%%Trailer\r\n\x04", 12));
    ps = MemLock(pstate);
    CHECK(ps->PS_epsLibrary == 0 && ps->PS_expansionInfo == 0);
    MemUnlock(pstate);

    /* the file, without the ^D of the print trailer, for Ghostscript */
    snprintf(file, sizeof(file), "%s%cout.ps", top, sep);
    f = fopen(file, "wb");
    CHECK(f != NULL);
    if (f) {
	fwrite(out, 1, outLen - 1, f);
	fclose(f);
	res = Ghostscript(file);
	if (res < 0)
	    printf("no Ghostscript: not run\n");
	else
	    CHECK(res == 1);
    }
    MemFree(pstate);

    /* a LaserJet 4: PJL before and after; collated copies are the
     * spooler's */
    outLen = 0;
    NewPState();
    MemUnlock(pstate);
    CHECK(!SetDevice("HP LaserJet 4 (PostScript)", &r));
    SetStream();
    FillJob("PJL", 3, SO_COLLATE);
    CHECK(!Call0(DR_PRINT_START_JOB));
    CHECK(outLen > sizeof(pjlStart) &&
	  !memcmp(out, pjlStart, sizeof(pjlStart) - 1));
    ps = MemLock(pstate);
    CHECK(PS_JOB_PARAMS(ps)->JP_numCopies == 3);
    MemUnlock(pstate);
    CHECK(!Has("numcopies"));
    CHECK(!Call0(DR_PRINT_END_JOB));
    CHECK(outLen > sizeof(pjlEnd) &&
	  !memcmp(out + outLen - (sizeof(pjlEnd) - 1), pjlEnd,
		  sizeof(pjlEnd) - 1));
    MemFree(pstate);

    GeodeFreeDriver(drv);
    printf("%s (%d failures)\n", fails ? "FAILED" : "passed", fails);
    return fails ? 1 : 0;
}
