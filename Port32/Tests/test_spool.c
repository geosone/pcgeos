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
#include <Internal/spoolDr.h>
#include <stddef.h>

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
extern const unsigned char vidmem_geode[];
extern const unsigned long vidmem_geode_size;
extern const unsigned char berkeley_fnt[];
extern const unsigned long berkeley_fnt_size;
extern const unsigned char epson9_geode[];
extern const unsigned long epson9_geode_size;

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
#define pSpoolGetNumPaperSizes \
    ENTRY(5, void (*)(NumPaperSizesInfo *, PageType))
#define pSpoolGetPaperString	ENTRY(6, word (*)(char *, word, PageType))
#define pSpoolGetPaperSize \
    ENTRY(7, void (*)(PaperSizeInfo *, word, PageType))
#define pSpoolConvertPaperSize	ENTRY(8, word (*)(word, word, PageType))
#define pSpoolGetPaperSizeOrder \
    ENTRY(9, void (*)(byte *, byte *, PaperSizeOrderInfo *, PageType))
#define pSpoolSetPaperSizeOrder	ENTRY(10, void (*)(byte *, word, PageType))
#define pSpoolCreatePaperSize \
    ENTRY(11, word (*)(char *, word, word, PageLayout, PageType))
#define pSpoolDeletePaperSize	ENTRY(12, Boolean (*)(word, PageType))
#define pSpoolGetDefaultPageSizeInfo \
    ENTRY(32, void (*)(PageSizeReport *))
#define pSpoolSetDefaultPageSizeInfo \
    ENTRY(33, void (*)(PageSizeReport *))

#define pSpoolMapToPrinterFont \
    ENTRY(29, void (*)(const PrinterInfo *, FontID *, word *, byte *))
#define pSpoolUpdateTranslationTable \
    ENTRY(30, void (*)(PState *, const SpoolTransTables *))

/* the text modes' routines for printer drivers (libDriver) */
static void TestDriver(void)
{
    static struct {
	PrinterInfo pi;
	FontGeometry fg[5];
	byte pitch1[4], pitch2[2];
    } dev;
    static union {
	long long align;
	byte b[sizeof(PState) + sizeof(JobParameters)];
    } st;
    static const word subISO[32] = {
	0x2323, 0x2424, 0x40a4, 0x5b80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0x2323, 0x40a4, 0x5b80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    static const byte trans[] = { 'a', 'A', ';', ';' };
    SpoolTransTables tt;
    PState *ps = (PState *) st.b;
    PrintDriverUIData *ui =
	(PrintDriverUIData *) PS_JOB_PARAMS(ps)->JP_printerData;
    FontID id;
    word size;
    byte pitch;
    word p1 = (word) offsetof(__typeof__(dev), pitch1),
	p2 = (word) offsetof(__typeof__(dev), pitch2);

    dev.pi.PI_fontGeometries = (word) offsetof(__typeof__(dev), fg);
    dev.fg[0].FG_fontID = FID_DTC_URW_ROMAN;
    dev.fg[0].FG_pointSize = 10;
    dev.fg[0].FG_pitchTab = p1;
    dev.fg[1].FG_fontID = FID_DTC_URW_ROMAN;
    dev.fg[1].FG_pointSize = 12;
    dev.fg[1].FG_pitchTab = p1;
    dev.fg[2].FG_fontID = FID_DTC_URW_ROMAN;
    dev.fg[2].FG_pointSize = 24;
    dev.fg[2].FG_pitchTab = p2;
    dev.fg[3].FG_fontID = FID_DTC_URW_SANS;
    dev.fg[3].FG_pointSize = 12;
    dev.fg[3].FG_pitchTab = p1;
    dev.fg[4].FG_fontID = FID_INVALID;
    dev.pitch1[0] = 17;
    dev.pitch1[1] = 12;
    dev.pitch1[2] = 10;
    dev.pitch2[0] = 5;
#define MAP(f, s, p, ef, es, ep) \
    (id = (f), size = (s), pitch = (p), \
     pSpoolMapToPrinterFont(&dev.pi, &id, &size, &pitch), \
     id == (ef) && size == (es) && pitch == (ep))
    CHECK(MAP(FID_DTC_URW_SANS, 12, 10, FID_DTC_URW_SANS, 12, 10));
    CHECK(MAP(FID_DTC_URW_SANS, 12, 11, FID_DTC_URW_SANS, 12, 12));
    CHECK(MAP(FID_DTC_URW_ROMAN, 14, 0, FID_DTC_URW_ROMAN, 12, 0));
    CHECK(MAP(FID_DTC_URW_ROMAN, 11, 0, FID_DTC_URW_ROMAN, 10, 0));
    /* smaller than any: fixed, 1440 / 10 is beyond the widest pitch */
    CHECK(MAP(FID_DTC_URW_ROMAN, 8, 0, FID_DTC_URW_ROMAN, 10, 17));
    /* a font the printer lacks: its first */
    CHECK(MAP(0x5555, 30, 18, FID_DTC_URW_ROMAN, 24, 5));
#undef MAP

    /* IBM 437, Germany: the upper half from the table, the national
     * characters, the driver's own */
    ps->PS_curFont.FE_symbolSet = PSS_IBM437;
    ui->PUID_countryCode = PCC_GERMANY;
    tt.STT_subISO = subISO;
    tt.STT_trans = trans;
    tt.STT_transSize = sizeof(trans);
    pSpoolUpdateTranslationTable(ps, &tt);
    CHECK(ps->PS_asciiTrans['b'] == 'b' && ps->PS_asciiTrans['a'] == 'A');
    CHECK(ps->PS_asciiTrans[0x81] == 0x8f);	/* GEOS A ring */
    CHECK(ps->PS_asciiTrans[0xa4] == 0x40 && ps->PS_asciiTrans[0x40] == ' ');
    CHECK(ps->PS_asciiTrans[0x80] == 0x5b && ps->PS_asciiTrans[0x5b] == ' ');
    CHECK(ps->PS_asciiTrans[0x24] == 0x24);
    /* ASCII7 in the USA: no upper half, no national characters */
    ps->PS_curFont.FE_symbolSet = PSS_ASCII7;
    ui->PUID_countryCode = PCC_USA;
    pSpoolUpdateTranslationTable(ps, &tt);
    CHECK(ps->PS_asciiTrans[0x80] == ' ' && ps->PS_asciiTrans[0x5b] == 0x5b);
    /* PC/GEOS: all of it as it is */
    ps->PS_curFont.FE_symbolSet = PSS_PCGEOS;
    ui->PUID_countryCode = PCC_GERMANY;
    pSpoolUpdateTranslationTable(ps, NULL);
    CHECK(ps->PS_asciiTrans[0x80] == 0x80 && ps->PS_asciiTrans['a'] == 'a');
}

/* the page sizes (libPaper): the tables of libTables.asm, the order,
 * user-defined sizes, the default page size */
static void TestPaper(void)
{
    NumPaperSizesInfo num;
    PaperSizeInfo info;
    PaperSizeOrderInfo oi;
    PageSizeReport psr;
    PageLayout lay;
    static byte order[MAX_PAPER_SIZES], unused[MAX_PAPER_SIZES];
    byte mine[2];
    char buf[MAX_PAPER_STRING_LENGTH];
    word n;

    /* the C stubs are the same routines */
    CHECK(ProcGetLibraryEntry(spool, 34) == ProcGetLibraryEntry(spool, 32));
    CHECK(ProcGetLibraryEntry(spool, 36) == ProcGetLibraryEntry(spool, 5));
    CHECK(ProcGetLibraryEntry(spool, 55) == ProcGetLibraryEntry(spool, 28));

    pSpoolGetNumPaperSizes(&num, PT_PAPER);
    CHECK(num.NPSI_numSizes == 20 && num.NPSI_defaultSize == 0);
    CHECK(pSpoolGetPaperString(buf, 0, PT_PAPER) == 9 &&
	  !strcmp(buf, "US Letter"));
    pSpoolGetPaperSize(&info, 2, PT_PAPER);		/* A4 */
    CHECK(info.PSI_width == 595 && info.PSI_height == 842 &&
	  info.PSI_defaultLayout.PL_paper == 0);
    pSpoolGetPaperSize(&info, 0xff00 | 57, PT_PAPER);	/* E size */
    CHECK(info.PSI_width == 3168 && info.PSI_height == 2448 &&
	  info.PSI_defaultLayout.PL_paper == (PO_LANDSCAPE << 3));
    pSpoolGetNumPaperSizes(&num, PT_ENVELOPE);
    CHECK(num.NPSI_numSizes == 18);
    CHECK(pSpoolGetPaperString(buf, 2, PT_ENVELOPE) &&
	  !strcmp(buf, "Business Envelope (#10)"));
    pSpoolGetPaperSize(&info, 0, PT_LABEL);		/* 4 across, 20 down */
    CHECK(info.PSI_width == 126 && info.PSI_height == 36 &&
	  info.PSI_defaultLayout.PL_paper == ((20 << 9) | (4 << 3) | 4));
    pSpoolGetNumPaperSizes(&num, PT_POSTCARD);
    CHECK(num.NPSI_numSizes == 2);

    /* upright, on its side, none (no custom size in the order) */
    CHECK(pSpoolConvertPaperSize(612, 792, PT_PAPER) == 0);
    CHECK(pSpoolConvertPaperSize(842, 595, PT_PAPER) == 2);
    CHECK(pSpoolConvertPaperSize(100, 100, PT_PAPER) == 0xffff);

    /* a size of the user, and an order of the user */
    lay.PL_paper = PT_PAPER;
    CHECK(pSpoolCreatePaperSize("My Size", 300, 400, lay, PT_PAPER) == 128);
    CHECK(pSpoolCreatePaperSize("Other", 310, 410, lay, PT_PAPER) == 129);
    CHECK(pSpoolGetPaperString(buf, 0xff00 | 128, PT_PAPER) == 7 &&
	  !strcmp(buf, "My Size"));
    pSpoolGetPaperSize(&info, 0xff00 | 129, PT_PAPER);
    CHECK(info.PSI_width == 310 && info.PSI_height == 410);
    pSpoolGetPaperSizeOrder(order, unused, &oi, PT_PAPER);
    CHECK(oi.PSOI_numOrdered == 20 && oi.PSOI_numUnused == 60 + 2 - 20);
    CHECK(order[0] == 29 && order[2] == 24);
    CHECK(unused[128] && unused[129] && !unused[29] && unused[1]);
    mine[0] = 128;
    mine[1] = 24;
    pSpoolSetPaperSizeOrder(mine, 2, PT_PAPER);
    pSpoolGetNumPaperSizes(&num, PT_PAPER);
    CHECK(num.NPSI_numSizes == 2);
    CHECK(pSpoolGetPaperString(buf, 0, PT_PAPER) && !strcmp(buf, "My Size"));
    CHECK(pSpoolConvertPaperSize(300, 400, PT_PAPER) == 0);
    CHECK(!pSpoolDeletePaperSize(0xff00 | 128, PT_PAPER));
    CHECK(pSpoolDeletePaperSize(0xff00 | 128, PT_PAPER));
    CHECK(!InitFileReadInteger("paper", "count", &n) && n == 1);
    CHECK(InitFileReadInteger("paper128", "width", &n));
    CHECK(pSpoolCreatePaperSize("Again", 320, 420, lay, PT_PAPER) == 128);
    CHECK(!pSpoolDeletePaperSize(0xff00 | 128, PT_PAPER));
    CHECK(!pSpoolDeletePaperSize(0xff00 | 129, PT_PAPER));
    InitFileDeleteEntry("paper", "order");
    pSpoolGetNumPaperSizes(&num, PT_PAPER);
    CHECK(num.NPSI_numSizes == 20);

    /* the default page size: libTables.asm's, then the user's */
    pSpoolGetDefaultPageSizeInfo(&psr);
    CHECK(psr.PSR_width == 612 && psr.PSR_height == 792 &&
	  psr.PSR_layout.PL_paper == 0 && psr.PSR_margins.PCMP_left == 18 &&
	  psr.PSR_margins.PCMP_bottom == 18);
    psr.PSR_width = 420;
    psr.PSR_height = 595;
    psr.PSR_margins.PCMP_top = 36;
    pSpoolSetDefaultPageSizeInfo(&psr);
    memset(&psr, 0, sizeof(psr));
    pSpoolGetDefaultPageSizeInfo(&psr);
    CHECK(psr.PSR_width == 420 && psr.PSR_height == 595 &&
	  psr.PSR_margins.PCMP_top == 36 && psr.PSR_margins.PCMP_left == 18);
    /* back to none */
    InitFileDeleteCategory("spool");
    InitFileWriteBoolean("spool", "hostPrinters", FALSE);
}

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

#ifdef GEOS_TEST_CUPS
/* the spooler detached (MSG_META_DETACH to its process): TRUE when the
 * process is gone, within half a minute; the geode stays a library */
static Boolean Detach(void)
{
    struct { word callerID; optr ackOD; } d = { 0, 0 };
    int i;

    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(spool, 0),
		 KMSG_META_DETACH, &d, sizeof(d), NULL, 0);
    for (i = 0; i < 300; i++) {
	if (!GeodeFind("spool", 5, GA_PROCESS, 0))
	    return TRUE;
	TimerSleep(6);
    }
    return FALSE;
}
#endif

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


/* ---------------------------------------------------------------------
 * The Epson 9-pin driver: ESC/P back into dots
 * ------------------------------------------------------------------- */

/* the dots of a page at 240 x 216 dpi (the high resolution) */
#define EP_W	2048
#define EP_H	2600

typedef struct {
    int pages;
    int bad;			/* an unknown code or a truncated one */
    int left, top, right, bottom;	/* the dots of page 1 */
    long dots;
    int left2, top2, right2, bottom2;	/* and of page 2 */
    long dots2;
    int modes[4];		/* graphics codes per ESC * m */
    char text[256];		/* the text of page 1, '|' at ESC $ */
    int textLen;
    int underline;		/* ESC - 1 on page 1 */
    int absX[8];		/* the first ESC $ (not 0) of each page */
} EpsonDecode;

static void EpsonDot(EpsonDecode *d, int x, int y)
{
    if (d->pages == 1 && x >= 0 && y >= 0 && x < EP_W && y < EP_H) {
	if (!d->dots2 || x < d->left2)
	    d->left2 = x;
	if (!d->dots2 || x > d->right2)
	    d->right2 = x;
	if (!d->dots2 || y < d->top2)
	    d->top2 = y;
	if (!d->dots2 || y > d->bottom2)
	    d->bottom2 = y;
	d->dots2++;
	return;
    }
    if (d->pages != 0 || x < 0 || y < 0 || x >= EP_W || y >= EP_H)
	return;
    if (!d->dots || x < d->left)
	d->left = x;
    if (!d->dots || x > d->right)
	d->right = x;
    if (!d->dots || y < d->top)
	d->top = y;
    if (!d->dots || y > d->bottom)
	d->bottom = y;
    d->dots++;
}

static void EpsonDecodeFile(const byte *b, size_t n, EpsonDecode *d)
{
    size_t i = 0, k, cols;
    int x = 0, y = 0, scale, pin, c;

    memset(d, 0, sizeof(*d));
    while (i < n) {
	c = b[i++];
	if (c == 0 || c == 0x0f || c == 0x12 || c == 0x09)
	    continue;
	if (c == '\r') {
	    x = 0;
	    continue;
	}
	if (c == 0x0c) {
	    d->pages++;
	    x = y = 0;
	    continue;
	}
	if (c != 0x1b || i >= n) {
	    if (c >= ' ') {		/* text */
		if (!d->pages && d->textLen < (int) sizeof(d->text) - 1)
		    d->text[d->textLen++] = (char) c;
		continue;
	    }
	    d->bad++;
	    return;
	}
	c = b[i++];
	switch (c) {
	case '-':
	    if (!d->pages && i < n && b[i] == 1)
		d->underline++;
	    i++;
	    break;
	case '@': case 'O': case '6': case '8': case '4': case '5':
	case 'P': case 'M': case 'T': case 'E': case 'F': case 'G':
	case 'H':
	    break;
	case 't': case 'U': case 'r': case 'R': case 'x': case 'S':
	case 'W': case 'w': case 'k': case 'p': case '3':
	case 0x19:
	    i++;
	    break;
	case 'C':
	    if (i < n && b[i] == 0)
		i++;
	    i++;
	    break;
	case 'J':
	    if (i < n)
		y += b[i];
	    i++;
	    break;
	case '$':
	    if (i + 1 < n)
		x = (b[i] | b[i + 1] << 8) * 4;
	    i += 2;
	    if (!d->pages && d->textLen < (int) sizeof(d->text) - 1)
		d->text[d->textLen++] = '|';
	    if (x && d->pages < 8 && !d->absX[d->pages])
		d->absX[d->pages] = x;
	    break;
	case 'D':
	    while (i < n && b[i])
		i++;
	    i++;
	    break;
	case '*':
	    if (i + 2 >= n) {
		d->bad++;
		return;
	    }
	    c = b[i];
	    cols = (size_t) (b[i + 1] | b[i + 2] << 8);
	    i += 3;
	    scale = c == 0 ? 4 : c == 1 ? 2 : c == 3 ? 1 : 0;
	    if (!scale || i + cols > n) {
		d->bad++;
		return;
	    }
	    d->modes[c]++;
	    for (k = 0; k < cols; k++, x += scale)
		for (pin = 0; pin < 8; pin++)
		    if (b[i + k] & (0x80 >> pin))
			EpsonDot(d, x, y + pin * 3);
	    i += cols;
	    break;
	default:
	    d->bad++;
	    return;
	}
    }
}

/* the spool file of MakeSpoolFile through the Epson 9-pin driver to a
 * file in each graphics mode: two pages; on the first the rectangle and
 * the line, 72..500 by 72..400 points of the document */
static void TestEpson(void)
{
    static const word modes[] = { PM_GRAPHICS_LOW_RES, PM_GRAPHICS_MED_RES,
				  PM_GRAPHICS_HI_RES };
    JobParameters jp;
    EpsonDecode d;
    char name[13], rel[40];
    size_t n;
    byte *b;
    int m, w, h;

    for (m = 0; m < 3; m++) {
	snprintf(rel, sizeof(rel), "out%d.prn", 10 + m);
	MakeSpoolFile(name);
	FillJob(&jp, name, "Epson File", PPT_FILE, rel);
	strcpy(jp.JP_deviceName, "Epson FX-85 (Epson Mode)");
	jp.JP_printMode = modes[m];
	((PrintDriverUIData *) jp.JP_printerData)->PUID_countryCode = PCC_USA;
	CHECK(pSpoolAddJob(&jp) != 0xffff);
	CHECK(WaitIdle());
	snprintf(rel, sizeof(rel), "DOCUMENT/out%d.prn", 10 + m);
	b = (byte *) Get(rel, &n);
	CHECK(b != NULL && n > 100);
	if (!b)
	    continue;
	CHECK(n > 4 && b[0] == 0x1b && b[1] == '@');
	EpsonDecodeFile(b, n, &d);
	w = d.right - d.left;		/* 428 pt at 240 dpi: 1427 */
	h = d.bottom - d.top;		/* 328 pt at 216 dpi: 984 */
	printf("epson9 mode %d: %lu bytes, %d pages, %ld dots, %dx%d at "
	       "%d,%d, codes %d/%d/%d\n", modes[m], (unsigned long) n,
	       d.pages, d.dots, w, h, d.left, d.top, d.modes[0], d.modes[1],
	       d.modes[3]);
	CHECK(!d.bad);
	CHECK(d.pages == 2);
	CHECK(d.modes[m == 0 ? 0 : m == 1 ? 1 : 3] > 0);
	CHECK(w > 1427 - 12 && w < 1427 + 12);
	CHECK(h > 984 - 12 && h < 984 + 12);
	/* the document's 72 pt, less its margins (18 pt), from where the
	 * head starts: the printable area of the paper */
	CHECK(d.left > 54 * 240 / 72 - 12 && d.left < 54 * 240 / 72 + 12);
	CHECK(d.top > 54 * 3 - 12 && d.top < 54 * 3 + 12);
	/* page 2: the filled rectangle and the ellipse, 100..400 by
	 * 100..600 points, in the same place */
	printf("  page 2: %ld dots, %dx%d at %d,%d\n", d.dots2,
	       d.right2 - d.left2, d.bottom2 - d.top2, d.left2, d.top2);
	CHECK(d.left2 > 82 * 240 / 72 - 12 && d.left2 < 82 * 240 / 72 + 12);
	CHECK(d.top2 > 82 * 3 - 12 && d.top2 < 82 * 3 + 12);
	CHECK(d.right2 - d.left2 > 1000 - 12 && d.right2 - d.left2 < 1000 + 12);
	CHECK(d.bottom2 - d.top2 > 1500 - 12 && d.bottom2 - d.top2 < 1500 + 12);
	free(b);
    }
}

/* a text page through the Epson 9-pin driver in its draft mode: the
 * strings in the order of their lines, then left to right, each at its
 * place; underline switched on for the underlined one; two pages */
static void TestEpsonText(void)
{
    JobParameters jp;
    EpsonDecode d;
    GStateHandle gs;
    FileHandle fh;
    char name[13];
    word ch;
    size_t n;
    byte *b;

    fh = pSpoolCreateSpoolFile(name);
    CHECK(fh != 0);
    if (!fh)
	return;
    gs = GrCreateGString(fh, GST_STREAM, &ch);
    GrDrawText(gs, 300, 100, "Right", 0);
    GrDrawText(gs, 72, 100, "Hello World", 0);
    /* underlined: the FX-85 has bold only in 10 pitch, underline in
     * every pitch (its font entries) */
    GrSetTextStyle(gs, TS_UNDERLINE, 0);
    GrDrawText(gs, 72, 150, "Under", 0);
    GrSetTextStyle(gs, 0, TS_UNDERLINE);
    /* past the right edge (594 pt less the margin): cut there */
    GrDrawText(gs, 500, 200, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 0);
    GrNewPage(gs, PEC_FORM_FEED);
    GrDrawText(gs, 72, 72, "Page2", 0);
    GrNewPage(gs, PEC_FORM_FEED);
    CHECK(GrEndGString(gs) == GSET_NO_ERROR);
    GrDestroyGString(gs, 0, GSKT_LEAVE_DATA);
    FileClose(fh, FALSE);

    FillJob(&jp, name, "Epson File", PPT_FILE, "out13.prn");
    strcpy(jp.JP_deviceName, "Epson FX-85 (Epson Mode)");
    jp.JP_printMode = PM_TEXT_DRAFT;
    ((PrintDriverUIData *) jp.JP_printerData)->PUID_countryCode = PCC_USA;
    CHECK(pSpoolAddJob(&jp) != 0xffff);
    CHECK(WaitIdle());
    b = (byte *) Get("DOCUMENT/out13.prn", &n);
    CHECK(b != NULL && n > 20);
    if (!b)
	return;
    EpsonDecodeFile(b, n, &d);
    d.text[d.textLen] = 0;
    printf("epson9 text: %lu bytes, %d pages, \"%s\", underline %d, x %d %d\n",
	   (unsigned long) n, d.pages, d.text, d.underline, d.absX[0], d.absX[1]);
    CHECK(!d.bad);
    CHECK(d.pages == 2);
    /* the last '|': the form feed's return to the left edge */
    {
	static const char head[] = "|Hello World|Right|Under|";
	const char *cut = d.text + sizeof(head) - 1;
	size_t k = strlen(cut);

	CHECK(!strncmp(d.text, head, sizeof(head) - 1));
	/* "ABC...", then the form feed's '|'; not all of it */
	CHECK(k > 2 && k < 27 && cut[k - 1] == '|' &&
	      !strncmp(cut, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", k - 1));
    }
    CHECK(d.underline == 1);
    /* 72 pt less the document's margin, on both pages */
    CHECK(d.absX[0] == 45 * 4 && d.absX[1] == 45 * 4);
    free(b);
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
	"hostQueue = fake1\r\nhostDuplex = true\r\n"
	"[Epson File]\r\ndriver = epson9.geo\r\n"
	"device = Epson FX-85 (Epson Mode)\r\n"
	"[spool]\r\nhostPrinters = false\r\n";
    static const char patch[] = "% devpatch\r\n";
    GeodeLoadError e;
#ifdef GEOS_TEST_CUPS
#define CUPS_DOC_MAX 65536
    char *cupsDoc, got[16];
    FakeCupsJob *cupsJob = NULL;
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
    Put("SYSTEM/PRINTER/epson9.geo", epson9_geode, epson9_geode_size);
    Put("SYSTEM/VIDEO", NULL, 0);	/* the bitmaps of the raster drivers */
    Put("SYSTEM/VIDEO/vidmem.geo", vidmem_geode, vidmem_geode_size);
    Put("USERDATA", NULL, 0);		/* a font for the text modes */
    Put("USERDATA/FONT", NULL, 0);
    Put("USERDATA/FONT/Berkeley.fnt", berkeley_fnt, berkeley_fnt_size);
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
    fakeCupsJob.uri = "ipp://localhost/printers/fake1";
    fakeCupsJob.format = "application/postscript";
    fakeCupsJob.name = "GEOS";
    fakeCupsJob.copies = NULL;
    fakeCupsJob.sides = "two-sided-long-edge";
    cupsJob = FakeCupsShared(CUPS_DOC_MAX, &cupsDoc);
    cups = cupsJob ? FakeCupsStart() : 0;
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

    TestPaper();
    TestDriver();

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

    TestEpson();
    TestEpsonText();

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
	    cupsJob->docLen = n;
	}
	free(b);
	MakeSpoolFile(name1);
	FillJob(&jp, name1, "Host Laser", PPT_CUSTOM, NULL);
	CHECK(pSpoolAddJob(&jp) != 0xffff);
	CHECK(WaitIdle());
	CHECK(!SpoolFileExists(name1));
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
    for (i = 0; i < 300 && GeodeFind("spool", 5, GA_PROCESS, 0); i++)
	TimerSleep(6);				/* the process is gone */
    CHECK(!GeodeFind("spool", 5, GA_PROCESS, 0));
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

#ifdef GEOS_TEST_CUPS
    /* the host's printers at the next start: a host printer the host no
     * longer has goes, a printer of the user keeps its name (the host's
     * "Drucker 1" becomes "fake1"; fake2 has no printer-info), without a
     * default the host's default printer becomes it, and without a
     * default page size its paper (A4) */
    if (cups) {
	char buf[GEODE_MAX_DEVICE_NAME_SIZE];
	word n;
	Boolean b;
	int def;

	CHECK(pSpoolCreatePrinter("Old Host", PDT_PRINTER) != 0xffff);
	InitFileWriteBoolean("Old Host", "hostPrinter", TRUE);
	InitFileWriteString("Old Host", "hostQueue", "gone");
	CHECK(pSpoolCreatePrinter("Drucker 1", PDT_PRINTER) != 0xffff);
	CHECK(Detach());
	GeodeFreeLibrary(spool);
	InitFileDeleteEntry("printer", "defaultPrinter");
	InitFileWriteBoolean("spool", "hostPrinters", TRUE);

	spool = GeodeUseLibrary("spool.geo", 0, 0, &e);
	CHECK(spool != 0);
	if (!spool)
	    return 1;
	for (i = 0; i < 100 && pSpoolGetNumPrinters(PDT_PRINTER, 0) != 4;
	     i++)
	    TimerSleep(6);
	CHECK(pSpoolGetNumPrinters(PDT_PRINTER, 0) == 4);
	CHECK(!pSpoolGetPrinterString(&psi, 0) &&
	      !strcmp(psi.PSI_stringBuf, "PS File"));
	CHECK(!pSpoolGetPrinterString(&psi, 1) &&
	      !strcmp(psi.PSI_stringBuf, "Drucker 1"));
	CHECK(!pSpoolGetPrinterString(&psi, 2) &&
	      !strcmp(psi.PSI_stringBuf, "fake1"));
	CHECK(!pSpoolGetPrinterString(&psi, 3) &&
	      !strcmp(psi.PSI_stringBuf, "fake2"));
	def = pSpoolGetDefaultPrinter();
	CHECK(def == 3);
	CHECK(!InitFileReadStringBuffer("fake1", "hostQueue", buf,
					sizeof(buf), &n) &&
	      !strcmp(buf, "fake1"));
	CHECK(!InitFileReadStringBuffer("fake1", "device", buf,
					sizeof(buf), &n) &&
	      !strcmp(buf, "PostScript Compatible"));
	CHECK(!InitFileReadBoolean("fake1", "hostDuplex", &b) && !b);
	CHECK(!InitFileReadStringBuffer("fake2", "device", buf,
					sizeof(buf), &n) &&
	      !strcmp(buf, "HP Color LaserJet (PostScript)"));
	CHECK(!InitFileReadStringBuffer("fake2", "port", buf,
					sizeof(buf), &n) &&
	      !strcmp(buf, "CUSTOM"));
	CHECK(!InitFileReadBoolean("fake2", "hostDuplex", &b) && b);
	CHECK(InitFileReadBoolean("Drucker 1", "hostPrinter", &b));
	{
	    PageSizeReport psr;
	    pSpoolGetDefaultPageSizeInfo(&psr);
	    CHECK(psr.PSR_width == 595 && psr.PSR_height == 842 &&
		  psr.PSR_margins.PCMP_left == 18);
	    CHECK(!InitFileReadBoolean("spool", "hostPaper", &b) && b);
	}
	CHECK(InitFileReadStringBuffer("Old Host", "hostQueue", buf,
				       sizeof(buf), &n));

	/* a job to the host printer as the list has it */
	MakeSpoolFile(name1);
	FillJob(&jp, name1, "fake1", PPT_CUSTOM, NULL);
	cupsJob->sides = "one-sided";
	CHECK(pSpoolAddJob(&jp) != 0xffff);
	CHECK(WaitIdle());

	FakeCupsStop(cups, got, sizeof(got));
	/* the first job, the list and the default at the start, the
	 * second job */
	if (strcmp(got, "JLDJ"))
	    printf("CUPS stand-in: \"%s\"\n", got);
	CHECK(!strcmp(got, "JLDJ"));
    }
#endif

    GeodeFreeLibrary(spool);
    printf("%s: %d failure(s)\n", fails ? "FAILED" : "OK", fails);
    return fails != 0;
}
