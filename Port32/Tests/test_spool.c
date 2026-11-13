/*
 * test_spool.c -- the print spool library (Library/Spool, spool.geo) with
 * the PostScript printer driver, the PostScript translation library and
 * the file stream driver: the printers of the ini file, spool files, and
 * jobs printed by the spooler's threads to files (Port32/ABI.md section
 * 13w).
 *
 * The spool files are graphics strings of two pages, as an application
 * prints them; the jobs go to the file port (DOCUMENT/out.ps ...), to
 * the nothing port, and to a printer whose driver is missing.  Where
 * Ghostscript is on the host (GEOS_TEST_GS, or gs in the PATH on
 * linux), it must run the output without an error.  Nothing is printed
 * on a real printer.
 */
#define _GNU_SOURCE			/* popen */
#include "kernelC.h"
#include "ghal.h"
#include <geosdriver.h>
#include <library.h>
#include <resource.h>
#include <file.h>
#include <graphics.h>
#include <gstring.h>
#include <print.h>
#include <timer.h>
#include <initfile.h>
#include <object.h>
#include <Internal/printDr.h>
#include <spoolC.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef GEOS_TEST_CUPS
/* a job through the CUPS port driver to the stand-in for CUPS */
#include "fakecups.h"
extern const unsigned char cups_geode[];
extern const unsigned long cups_geode_size;
#endif

extern const unsigned char spool_geode[];
extern const unsigned long spool_geode_size;
extern const unsigned char filestr_geode[];
extern const unsigned long filestr_geode_size;
extern const unsigned char eps_geode[];
extern const unsigned long eps_geode_size;
extern const unsigned char pscript_geode[];
extern const unsigned long pscript_geode_size;

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static char top[260], sep = '/';

static void HostPath(char *path, size_t size, const char *rel)
{
    char *p;

    snprintf(path, size, "%s%c%s", top, sep, rel);
    for (p = path; *p; p++)
	if (*p == '/')
	    *p = sep;
}

static void Put(const char *rel, const void *data, unsigned long size)
{
    char path[300];
    ghal_file *f;
    size_t done = 0;

    HostPath(path, sizeof(path), rel);
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

/* a file of the host top, whole; NULL if there is none */
static char *Get(const char *rel, size_t *len)
{
    char path[300], *buf;
    FILE *f;
    long n;

    HostPath(path, sizeof(path), rel);
    f = fopen(path, "rb");
    if (!f)
	return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t) n + 1);
    *len = fread(buf, 1, (size_t) n, f);
    buf[*len] = 0;
    fclose(f);
    return buf;
}

static int Exists(const char *rel)
{
    size_t n;
    char *b = Get(rel, &n);

    free(b);
    return b != NULL;
}

static int Count(const char *buf, size_t len, const char *s)
{
    size_t i, n = strlen(s);
    int c = 0;

    for (i = 0; i + n <= len; i++)
	if (!memcmp(buf + i, s, n))
	    c++;
    return c;
}

/* a spool file, seen by GEOS (on DOS hosts its host name differs) */
static int SpoolFileExists(const char *name)
{
    FileHandle fh;

    FilePushDir();
    FileSetStandardPath(SP_SPOOL);
    fh = FileOpen(name, FILE_DENY_NONE | FILE_ACCESS_R);
    if (fh)
	FileClose(fh, TRUE);
    FilePopDir();
    return fh != 0;
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

/* ---------------------------------------------------------------------
 * The library's routines, by their entry numbers (spool32.gp)
 * ------------------------------------------------------------------- */

static GeodeHandle spool;

#define ENTRY(n, type)	((type) ProcGetLibraryEntry(spool, (n)))
#define pSpoolGetNumPrinters \
    ENTRY(13, word (*)(PrinterDriverType, byte))
#define pSpoolGetPrinterString \
    ENTRY(14, Boolean (*)(PrinterStringInfo *, word))
#define pSpoolCreatePrinter \
    ENTRY(16, word (*)(const char *, PrinterDriverType))
#define pSpoolDeletePrinter	ENTRY(17, void (*)(word))
#define pSpoolGetDefaultPrinter	ENTRY(18, int (*)(void))
#define pSpoolSetDefaultPrinter	ENTRY(19, void (*)(word))
#define pSpoolCreateSpoolFile	ENTRY(20, FileHandle (*)(char *))
#define pSpoolAddJob		ENTRY(22, word (*)(const JobParameters *))
#define pSpoolDelJob		ENTRY(23, SpoolOpStatus (*)(word))
#define pSpoolInfo \
    ENTRY(24, SpoolOpStatus (*)(SpoolInfoType, SpoolInfoParams *))
#define pSpoolHurryJob		ENTRY(25, SpoolOpStatus (*)(word))
#define pSpoolVerifyPrinterPort \
    ENTRY(28, SpoolOpStatus (*)(PrintPortInfo *))

/* a spool file of two pages; its name into name */
static void MakeSpoolFile(char *name)
{
    FileHandle fh;
    GStateHandle gs;
    word ch;

    fh = pSpoolCreateSpoolFile(name);
    CHECK(fh != 0);
    if (!fh)
	return;
    gs = GrCreateGString(fh, GST_STREAM, &ch);
    GrDrawRect(gs, 72, 72, 300, 200);
    GrDrawLine(gs, 72, 400, 500, 400);
    GrNewPage(gs, PEC_FORM_FEED);
    GrSetAreaColor(gs, CF_RGB, 0, 0, 0xff);
    GrFillRect(gs, 100, 100, 400, 300);
    GrDrawEllipse(gs, 100, 400, 400, 600);
    GrNewPage(gs, PEC_FORM_FEED);
    CHECK(GrEndGString(gs) == GSET_NO_ERROR);
    GrDestroyGString(gs, 0, GSKT_LEAVE_DATA);
    FileClose(fh, FALSE);
}

static void FillJob(JobParameters *jp, const char *spoolFile,
		    const char *printer, PrinterPortType port,
		    const char *outFile)
{
    PrintDriverUIData *ui = (PrintDriverUIData *) jp->JP_printerData;

    memset(jp, 0, sizeof(*jp));
    strcpy(jp->JP_fname, spoolFile);
    strcpy(jp->JP_parent, "Port32 Test");
    strcpy(jp->JP_documentName, "Spool Test");
    jp->JP_numPages = 2;
    strcpy(jp->JP_printerName, printer);
    strcpy(jp->JP_deviceName, "PostScript Compatible");
    jp->JP_portInfo.PPI_type = port;
    if (port == PPT_FILE) {
	strcpy(jp->JP_portInfo.PPI_params.PP_file.FPP_fileName, outFile);
	jp->JP_portInfo.PPI_params.PP_file.FPP_diskHandle = SP_DOCUMENT;
    }
    jp->JP_printMode = PM_GRAPHICS_HI_RES;
    jp->JP_paperSizeInfo.PSR_width = 612;
    jp->JP_paperSizeInfo.PSR_height = 792;
    jp->JP_paperSizeInfo.PSR_margins.PCMP_left = 18;
    jp->JP_paperSizeInfo.PSR_margins.PCMP_top = 18;
    jp->JP_paperSizeInfo.PSR_margins.PCMP_right = 18;
    jp->JP_paperSizeInfo.PSR_margins.PCMP_bottom = 18;
    jp->JP_docSizeInfo = jp->JP_paperSizeInfo;
    jp->JP_spoolOpts = SO_DELETE;
    jp->JP_numCopies = 1;
    jp->JP_retries = 3;
    jp->JP_size = sizeof(JobParameters);
    ui->PUID_paperInput = ASF_TRAY1 << PIO_ASF_OFFSET;
}

/* the queues are empty: TRUE when they are, within half a minute */
static Boolean WaitIdle(void)
{
    SpoolInfoParams p;
    int i;

    for (i = 0; i < 300; i++) {
	memset(&p, 0, sizeof(p));
	p.SIP_queueParams.SIQP_activityQuery = TRUE;
	if (pSpoolInfo(SIT_QUEUE_INFO, &p) == SPOOL_QUEUE_EMPTY)
	    return TRUE;
	TimerSleep(6);
    }
    return FALSE;
}

/* the output: PostScript of the pages; Ghostscript runs it */
static void CheckOutput(const char *rel, int pages)
{
    char *ps, path[300];
    size_t len;
    FILE *f;
    int res;

    ps = Get(rel, &len);
    CHECK(ps != NULL);
    if (!ps)
	return;
    CHECK(len > 1000 && !memcmp(ps, "%!PS-Adobe-3.0\r\n", 16));
    CHECK(Count(ps, len, "%%Title: Spool Test\r\n") == 1);
    CHECK(Count(ps, len, "%%Page: ") == pages);
    CHECK(Count(ps, len, "%%PageTrailer") == pages);
    CHECK(Count(ps, len, " FR") >= 1);		/* the filled rectangle */
    CHECK(len > 12 && !memcmp(ps + len - 12, "%%Trailer\r\n\x04", 12));
    if (Count(ps, len, "%%Page: ") != pages)
	printf("%s: %d pages\n", rel, Count(ps, len, "%%Page: "));

    /* without the ^D of the print trailer, for Ghostscript */
    HostPath(path, sizeof(path), "gs.ps");
    f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) {
	fwrite(ps, 1, len - 1, f);
	fclose(f);
	res = Ghostscript(path);
	if (res < 0)
	    printf("no Ghostscript: not run\n");
	else
	    CHECK(res == 1);
    }
    free(ps);
}

int main(int argc, char **argv)
{
    static const char ini[] =
	"[PS File]\r\ndriver = pscript.geo\r\n"
	"device = PostScript Compatible\r\n"
	"[No Driver]\r\ndriver = nosuch.geo\r\n"
	"device = PostScript Compatible\r\n"
	"[Host Laser]\r\ndriver = pscript.geo\r\n"
	"device = PostScript Compatible\r\nportDriver = cups.geo\r\n"
	"hostQueue = fake1\r\nhostDuplex = true\r\n";
    static const char patch[] = "% devpatch\r\n";
    GeodeLoadError e;
#ifdef GEOS_TEST_CUPS
#define CUPS_DOC_MAX 65536
    char *cupsDoc, got[16];
    size_t *cupsDocLen;
    pid_t cups;
#endif
    PrinterStringInfo psi;
    JobParameters jp;
    SpoolInfoParams sip;
    PrintPortInfo port;
    JobStatus *js;
    char name1[13], name2[13], name3[13], name4[13], *slash;
    char rel[40];
    word id1, id2, id3, id4;
    int i;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    strncpy(top, argv[0], sizeof(top) - 10);
    top[sizeof(top) - 10] = 0;
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash) {
	sep = *slash;
	strcpy(slash + 1, "GSPOOLTOP");
    } else
	strcpy(top, "GSPOOLTOP");
    ghal_fs_mkdir(top);
    /* a fresh start: the test ends without a shutdown, so the last run
     * left GEOS_ACT.IVE (a crash to the spooler's restart) */
    {
	char path[300];
	HostPath(path, sizeof(path), "GEOS_ACT.IVE");
	remove(path);
	HostPath(path, sizeof(path), "PRIVDATA/SPOOL/spoolsta.dat");
	remove(path);
    }
    Put("SYSTEM", NULL, 0);
    Put("SYSTEM/PRINTER", NULL, 0);
    Put("SYSTEM/IMPEX", NULL, 0);
    Put("PRIVDATA", NULL, 0);
    Put("PRIVDATA/SPOOL", NULL, 0);
    Put("DOCUMENT", NULL, 0);
    Put("SYSTEM/spool.geo", spool_geode, spool_geode_size);
    Put("SYSTEM/filestr.geo", filestr_geode, filestr_geode_size);
    Put("SYSTEM/PRINTER/pscript.geo", pscript_geode, pscript_geode_size);
    Put("SYSTEM/IMPEX/eps.geo", eps_geode, eps_geode_size);
    Put("SYSTEM/devpatch.ps", patch, sizeof(patch) - 1);
    Put("geos.ini", ini, sizeof(ini) - 1);
    /* a spool file of an earlier session, which the spooler deletes */
    Put("PRIVDATA/SPOOL/spool005.dat", "old", 3);
    for (i = 1; i <= 3; i++) {
	snprintf(rel, sizeof(rel), "DOCUMENT/out%d.ps", i);
	Put(rel, "x", 1);			/* truncated by the job */
    }

#ifdef GEOS_TEST_CUPS
    /* before the kernel's threads: the stand-in is a child process; the
     * document it expects is the file job's output, known later */
    Put("SYSTEM/cups.geo", cups_geode, cups_geode_size);
    unsetenv("LPDEST");
    unsetenv("PRINTER");
    cupsDoc = FakeCupsShared(CUPS_DOC_MAX, &cupsDocLen);
    fakeCupsJob.uri = "ipp://localhost/printers/fake1";
    fakeCupsJob.format = "application/postscript";
    fakeCupsJob.name = "GEOS";
    fakeCupsJob.copies = NULL;
    fakeCupsJob.sides = "two-sided-long-edge";
    cups = cupsDoc ? FakeCupsStart() : 0;
    CHECK(cups != 0);
#endif

    CHECK(KernelBoot(top, "", &e) == 0 && e == GLE_OK);
    FileSetStandardPath(SP_SYSTEM);
    spool = GeodeUseLibrary("spool.geo", 0, 0, &e);
    CHECK(spool != 0);
    if (!spool) {
	printf("spool.geo: GeodeLoadError %d\n", (int) e);
	return 1;
    }

    /* the spooler's process cleans up the spool directory */
    for (i = 0; i < 100 && Exists("PRIVDATA/SPOOL/spool005.dat"); i++)
	TimerSleep(6);
    CHECK(!Exists("PRIVDATA/SPOOL/spool005.dat"));

    /* the printers */
    CHECK(pSpoolGetNumPrinters(PDT_PRINTER, 0) == 0);
    CHECK(pSpoolCreatePrinter("PS File", PDT_PRINTER) == 0);
    CHECK(pSpoolCreatePrinter("PS File", PDT_PRINTER) == 0xffff);
    CHECK(pSpoolCreatePrinter("Fax", PDT_FACSIMILE) == 1);
    CHECK(pSpoolGetNumPrinters(PDT_PRINTER, 0) == 1);
    CHECK(pSpoolGetNumPrinters(PDT_FACSIMILE, 1) == 1);
    CHECK(pSpoolGetNumPrinters((PrinterDriverType) PDT_ALL, 0) == 2);
    memset(&psi, 0, sizeof(psi));
    CHECK(!pSpoolGetPrinterString(&psi, 1) &&
	  !strcmp(psi.PSI_stringBuf, "Fax") && psi.PSI_stringLength == 3 &&
	  psi.PSI_driverType == PDT_FACSIMILE);
    CHECK(pSpoolGetPrinterString(&psi, 2));
    pSpoolSetDefaultPrinter(0);
    CHECK(pSpoolGetDefaultPrinter() == 0);
    pSpoolDeletePrinter(1);
    CHECK(pSpoolGetNumPrinters((PrinterDriverType) PDT_ALL, 0) == 1);
    CHECK(pSpoolGetNumPrinters(PDT_FACSIMILE, 0) == 0);
    CHECK(!pSpoolGetPrinterString(&psi, 0) &&
	  !strcmp(psi.PSI_stringBuf, "PS File") &&
	  psi.PSI_driverType == PDT_PRINTER);

    /* the ports that need no checking */
    memset(&port, 0, sizeof(port));
    port.PPI_type = PPT_FILE;
    CHECK(pSpoolVerifyPrinterPort(&port) == SPOOL_OPERATION_SUCCESSFUL);
    port.PPI_type = PPT_NOTHING;
    CHECK(pSpoolVerifyPrinterPort(&port) == SPOOL_OPERATION_SUCCESSFUL);

    /* nothing queued */
    memset(&sip, 0, sizeof(sip));
    sip.SIP_jobParams.SIJP_jobID = 1234;
    CHECK(pSpoolInfo(SIT_JOB_INFO, &sip) == SPOOL_QUEUE_EMPTY);
    CHECK(pSpoolDelJob(1234) == SPOOL_QUEUE_EMPTY);

    /* the spool files: spool000.dat, then the next free names */
    MakeSpoolFile(name1);
    CHECK(!strcmp(name1, "spool000.dat"));
    MakeSpoolFile(name2);
    CHECK(!strcmp(name2, "spool001.dat"));
    MakeSpoolFile(name3);
    MakeSpoolFile(name4);
    CHECK(SpoolFileExists(name1));

    /* a job to a file: the queue of the file port and its thread */
    FillJob(&jp, name1, "PS File", PPT_FILE, "out1.ps");
    id1 = pSpoolAddJob(&jp);
    CHECK(id1 != 0xffff);
    /* two collated copies to another file, in the same queue */
    FillJob(&jp, name2, "PS File", PPT_FILE, "out2.ps");
    jp.JP_numCopies = 2;
    jp.JP_spoolOpts |= SO_COLLATE;
    id2 = pSpoolAddJob(&jp);
    CHECK(id2 == (word) (id1 + 1));
    /* the job's information while it waits or prints */
    memset(&sip, 0, sizeof(sip));
    sip.SIP_jobParams.SIJP_jobID = id2;
    if (pSpoolInfo(SIT_JOB_INFO, &sip) == SPOOL_OPERATION_SUCCESSFUL) {
	js = MemLock(sip.SIP_jobParams.SIJP_retBlock);
	CHECK(!strcmp(js->JS_fname, name2) && js->JS_numPages == 2 &&
	      !strcmp(js->JS_documentName, "Spool Test"));
	MemFree(sip.SIP_jobParams.SIJP_retBlock);
    }
    sip.SIP_jobParams.SIJP_jobID = id2;
    if (pSpoolInfo(SIT_JOB_PARAMETERS, &sip) ==
	SPOOL_OPERATION_SUCCESSFUL) {
	JobParameters *p = MemLock(sip.SIP_jobParams.SIJP_retBlock);
	CHECK(p->JP_numCopies == 2 && !strcmp(p->JP_printerName, "PS File"));
	MemFree(sip.SIP_jobParams.SIJP_retBlock);
    }

    /* the nothing port, and a printer without its driver */
    FillJob(&jp, name3, "PS File", PPT_NOTHING, NULL);
    id3 = pSpoolAddJob(&jp);
    FillJob(&jp, name4, "No Driver", PPT_FILE, "out3.ps");
    id4 = pSpoolAddJob(&jp);
    CHECK(id3 != 0xffff && id4 != 0xffff);

    CHECK(WaitIdle());
    sip.SIP_jobParams.SIJP_jobID = id1;
    CHECK(pSpoolInfo(SIT_JOB_INFO, &sip) == SPOOL_QUEUE_EMPTY);
    CHECK(pSpoolHurryJob(id1) == SPOOL_QUEUE_EMPTY);

    CheckOutput("DOCUMENT/out1.ps", 2);
    CheckOutput("DOCUMENT/out2.ps", 4);
    /* the job without a driver did not print; its file is gone too */
    {
	size_t n;
	char *b = Get("DOCUMENT/out3.ps", &n);
	CHECK(b && n == 1);
	free(b);
    }
    CHECK(!SpoolFileExists("spool000.dat"));
    CHECK(!SpoolFileExists("spool001.dat"));
    CHECK(!SpoolFileExists("spool002.dat"));
    CHECK(!SpoolFileExists("spool003.dat"));

#ifdef GEOS_TEST_CUPS
    /* the same document to the host's queue: the custom port loads
     * cups.geo (portDriver), which sends the printer driver's output to
     * CUPS as it is; the stand-in compares it with the file job's */
    if (cups) {
	size_t n;
	char *b = Get("DOCUMENT/out1.ps", &n);

	CHECK(b && n <= CUPS_DOC_MAX);
	if (b && n <= CUPS_DOC_MAX) {
	    memcpy(cupsDoc, b, n);
	    *cupsDocLen = n;
	}
	free(b);
	MakeSpoolFile(name1);
	FillJob(&jp, name1, "Host Laser", PPT_CUSTOM, NULL);
	CHECK(pSpoolAddJob(&jp) != 0xffff);
	CHECK(WaitIdle());
	CHECK(!SpoolFileExists(name1));
	FakeCupsStop(cups, got, sizeof(got));
	CHECK(!strcmp(got, "J"));
	if (strcmp(got, "J"))
	    printf("CUPS stand-in: \"%s\"\n", got);
    }
#endif

    /* a detach while jobs wait ([spool] noPrinting): their queue goes
     * into the state file, their spool files stay; the next start of the
     * spooler prints them under their IDs */
    InitFileWriteBoolean("spool", "noPrinting", TRUE);
    MakeSpoolFile(name1);
    MakeSpoolFile(name2);
    FillJob(&jp, name1, "PS File", PPT_FILE, "out4.ps");
    id1 = pSpoolAddJob(&jp);
    FillJob(&jp, name2, "PS File", PPT_FILE, "out5.ps");
    jp.JP_numPages = 3;			/* marks the job in the state */
    id2 = pSpoolAddJob(&jp);
    CHECK(id1 != 0xffff && id2 == (word) (id1 + 1));
    {
	struct { word callerID; optr ackOD; } d = { 0, 0 };
	CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(spool, 0),
		     KMSG_META_DETACH, &d, sizeof(d), NULL, 0);
    }
    for (i = 0; i < 100 && !SpoolFileExists("spoolsta.dat"); i++)
	TimerSleep(6);
    CHECK(SpoolFileExists("spoolsta.dat"));
    CHECK(pSpoolAddJob(&jp) == 0xffff);		/* detaching */
    InitFileWriteBoolean("spool", "noPrinting", FALSE);
    CHECK(WaitIdle());				/* the thread is gone */
    TimerSleep(30);
    CHECK(SpoolFileExists(name1) && SpoolFileExists(name2));
    CHECK(SpoolFileExists("spoolsta.dat"));
    GeodeFreeLibrary(spool);

    spool = GeodeUseLibrary("spool.geo", 0, 0, &e);
    CHECK(spool != 0);
    if (!spool) {
	printf("spool.geo again: GeodeLoadError %d\n", (int) e);
	return 1;
    }
    for (i = 0; i < 100 && SpoolFileExists("spoolsta.dat"); i++)
	TimerSleep(6);
    CHECK(!SpoolFileExists("spoolsta.dat"));
    memset(&sip, 0, sizeof(sip));
    sip.SIP_jobParams.SIJP_jobID = id2;
    if (pSpoolInfo(SIT_JOB_PARAMETERS, &sip) ==
	SPOOL_OPERATION_SUCCESSFUL) {
	JobParameters *p = MemLock(sip.SIP_jobParams.SIJP_retBlock);
	CHECK(p->JP_numPages == 3 && !strcmp(p->JP_fname, name2));
	MemFree(sip.SIP_jobParams.SIJP_retBlock);
    }
    CHECK(WaitIdle());
    CheckOutput("DOCUMENT/out4.ps", 2);
    CheckOutput("DOCUMENT/out5.ps", 2);
    CHECK(!SpoolFileExists(name1) && !SpoolFileExists(name2));
    /* the IDs go on from the state file */
    MakeSpoolFile(name3);
    FillJob(&jp, name3, "PS File", PPT_NOTHING, NULL);
    CHECK(pSpoolAddJob(&jp) == (word) (id2 + 1));
    CHECK(WaitIdle());

    GeodeFreeLibrary(spool);
    printf("%s: %d failure(s)\n", fails ? "FAILED" : "OK", fails);
    return fails != 0;
}
