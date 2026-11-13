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
 * linux), it must run the output without an error.  The raster drivers'
 * output (ESC/P, PCL) is decoded back into dots and text.  Nothing is
 * printed on a real printer.  "test_spool deskjet" (driver.deskjet)
 * prints through the DeskJet driver only.
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
#include <fontID.h>
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
extern const unsigned char truetype_geode[];
extern const unsigned long truetype_geode_size;
extern const unsigned char cooper_ttf[];
extern const unsigned long cooper_ttf_size;
extern const unsigned char berkeley_fnt[];
extern const unsigned long berkeley_fnt_size;
extern const unsigned char epson24_geode[];
extern const unsigned long epson24_geode_size;
extern const unsigned char epson9_geode[];
extern const unsigned long epson9_geode_size;
extern const unsigned char deskjet_geode[];
extern const unsigned long deskjet_geode_size;
extern const unsigned char pcl4_geode[];
extern const unsigned long pcl4_geode_size;
#define NINEPIN(n) \
    extern const unsigned char n##_geode[]; \
    extern const unsigned long n##_geode_size;
NINEPIN(epmx9) NINEPIN(eplx9) NINEPIN(eprx9) NINEPIN(bchip9)
NINEPIN(diconix9) NINEPIN(star9) NINEPIN(grpr9)
NINEPIN(nec24) NINEPIN(epshi24) NINEPIN(epson48)
NINEPIN(prop9) NINEPIN(propx24) NINEPIN(ppds24) NINEPIN(canon48)
NINEPIN(oki9) NINEPIN(citoh9) NINEPIN(tosh24)
NINEPIN(dj500c) NINEPIN(mercury) NINEPIN(canonbjc) NINEPIN(quietjet)

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
 * The Epson drivers: ESC/P back into dots
 * ------------------------------------------------------------------- */

/* the dots of a page at the high resolution across (240 dpi: 9 pins,
 * 360 dpi: 24 pins) and in line feed units down (1/216", 1/180") */
#define EP_W	4200
#define EP_H	8000

typedef struct {
    int pages;
    int bad;			/* an unknown code or a truncated one */
    int left, top, right, bottom;	/* the dots of page 1 */
    long dots;
    int left2, top2, right2, bottom2;	/* and of page 2 */
    long dots2;
    int modes[80];		/* graphics codes per ESC * m */
    char text[256];		/* the text of page 1, '|' at ESC $ */
    int textLen;
    int underline;		/* ESC - 1 on page 1 */
    int absX[8];		/* the first ESC $ (not 0) of each page */
    int tabs;			/* tabs taken (ESC D, HT) */
    int gfx;			/* rasters of ESC K, L, Y, Z, z */
} EpsonDecode;

/* how the other 9-pin printers differ: the dots an inch of ESC K, L, Y,
 * Z and z ("K60L120Z240"), the line feed units an inch (the pins 1/72"
 * apart), ESC P with an argument (IBM) */
typedef struct {
    const char *gfx;
    int vunit;
    int ibm;
    int lq;		/* 24 pins: ESC * 0 at 1/60", line feeds of the
			 * spacing (ESC +, NEC FS 3; IBM ESC 3) in 1/360" */
    int absUnit;	/* ESC $ (IBM CR ESC d) units an inch, 0: 60 */
    int proto;		/* EP_ESCP, or one of OtherDecodeFile's */
    int skip;		/* Toshiba: the pins above a band's top line */
    int feed, band;	/* the line feed after a band for its height (asm:
			 * Toshiba 75/720" for 76/720"), 0: the same */
} EpsonDialect;

enum { EP_ESCP, EP_OKI, EP_CITOH, EP_TOSHIBA };

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

/* the dots an inch of a graphics letter of the dialect, 0 if none */
static int EpsonGfxDpi(const EpsonDialect *dl, int c)
{
    const char *p;

    for (p = dl ? dl->gfx : NULL; p && *p; p++)
	if (*p == c)
	    return atoi(p + 1);
    return 0;
}

/* hdpi: the dots across of the canvas; dl: NULL for the Epson ESC/P of
 * the FX and LQ printers */
/* the dot matrix protocols other than ESC/P (EpsonDialect.proto): text,
 * CR, LF of the spacing, FF, tabs, absolute X and the graphics as dots,
 * vunit units down, hdpi across.
 *   EP_OKI:	ESC % 9 n spacing in 1/144", ESC HT ddd tab column,
 *		CR 1C 03 graphics 03 02 (ETX doubled), 72 dpi, 7 pins 1/72"
 *		apart, the top in bit 0
 *   EP_CITOH:	ESC T dd spacing in 1/144", ESC F dddd X in 1/160",
 *		ESC N (80 dpi) or ESC P (160 dpi) then ESC S dddd graphics,
 *		8 pins 1/72" apart, the top in bit 0
 *   EP_TOSHIBA: ESC L dd spacing in 1/48", ESC HT n tab to column n - 1,
 *		ESC ; dddd (180 dpi) or ESC 1D dddd (360 dpi) graphics,
 *		four bytes a column of six pins 1/180" apart, the top in
 *		bit 5 */
static int Digits(const byte *b, size_t n, size_t *i, int k)
{
    int v = 0;

    for (; k > 0 && *i < n; k--, (*i)++)
	v = v * 10 + (b[*i] - '0');
    return v;
}

static void OtherText(EpsonDecode *d, char c)
{
    if (!d->pages && d->textLen < (int) sizeof(d->text) - 1)
	d->text[d->textLen++] = c;
}

static void OtherTab(EpsonDecode *d, int x)
{
    d->tabs++;
    OtherText(d, '|');
    if (x && d->pages < 8 && !d->absX[d->pages])
	d->absX[d->pages] = x;
}

static void OtherDecodeFile(const byte *b, size_t n, EpsonDecode *d,
			    int hdpi, const EpsonDialect *dl)
{
    size_t i = 0, k, cols;
    int x = 0, y = 0, c, spacing = 0, tab = 0, gdpi = 0, pin, g, scale;

    memset(d, 0, sizeof(*d));
    while (i < n) {
	c = b[i++];
	if (c == '\r') {
	    x = 0;
	    continue;
	}
	if (c == '\n') {
	    y += spacing;
	    continue;
	}
	if (c == 0x0c) {
	    d->pages++;
	    x = y = 0;
	    continue;
	}
	if (c == 0x09) {
	    x = tab * hdpi / 10;
	    OtherTab(d, x);
	    continue;
	}
	if (dl->proto == EP_OKI && c == 0x1c && i < n && b[i] == 0x03) {
	    i++;			/* graphics to ETX STX */
	    d->gfx++;
	    scale = hdpi / 72;
	    for (;;) {
		if (i >= n) {
		    d->bad++;
		    return;
		}
		c = b[i++];
		if (c == 0x03) {
		    if (i < n && b[i] == 0x02) {
			i++;
			break;
		    }
		    if (i >= n || b[i] != 0x03) {
			d->bad++;
			return;
		    }
		    i++;
		}
		for (pin = 0; pin < 7; pin++)
		    if (c & (1 << pin))
			EpsonDot(d, x, y + pin * dl->vunit / 72);
		x += scale;
	    }
	    continue;
	}
	if (c < ' ' && c != 0x1b) {	/* pitch, SO, SI, DC1 */
	    /* 0x01: the bin after an empty ASF code (as Star9) */
	    if (c == 0 || c == 0x01 || c == 0x0e || c == 0x0f || c == 0x11 ||
		(c >= 0x1c && c <= 0x1f && dl->proto == EP_OKI))
		continue;
	    d->bad++;
	    return;
	}
	if (c != 0x1b) {
	    OtherText(d, (char) c);
	    continue;
	}
	if (i >= n) {
	    d->bad++;
	    return;
	}
	c = b[i++];
	switch (dl->proto) {
	case EP_OKI:
	    if (c == '%' && i < n) {
		c = b[i++];
		if (c == 'C')
		    (void) Digits(b, n, &i, 3);
		else if (c == '9' && i < n)
		    spacing = b[i++] * dl->vunit / 144;
		else {
		    d->bad++;
		    return;
		}
	    } else if (c == 'F')
		(void) Digits(b, n, &i, 2);
	    else if (c == 0x09)
		tab = Digits(b, n, &i, 3);
	    else if (c == 'C') {
		if (!d->pages)
		    d->underline++;
	    } else if (!strchr("KMIDLJ10HT", c)) {
		printf("Oki: ESC %#x at %lu\n", c, (unsigned long) i);
		d->bad++;
		return;
	    }
	    break;
	case EP_CITOH:
	    if (c == 'N' || c == 'E' || c == 'P' || c == 'Q')
		gdpi = c == 'N' ? 80 : c == 'P' ? 160 : 0;
	    else if (c == 'T')
		spacing = Digits(b, n, &i, 2) * dl->vunit / 144;
	    else if (c == 'F') {
		x = Digits(b, n, &i, 4) * hdpi / 160;
		OtherTab(d, x);
	    } else if (c == 'S') {
		cols = (size_t) Digits(b, n, &i, 4);
		if (!gdpi || hdpi % gdpi || i + cols > n) {
		    d->bad++;
		    return;
		}
		scale = hdpi / gdpi;
		d->gfx++;
		for (k = 0; k < cols; k++, x += scale)
		    for (pin = 0; pin < 8; pin++)
			if (b[i + k] & (1 << pin))
			    EpsonDot(d, x, y + pin * dl->vunit / 72);
		i += cols;
	    } else if (c == 's' || c == 'i' || c == 'K')
		i++;
	    else if (c == 'X') {
		if (!d->pages)
		    d->underline++;
	    } else if (!strchr("c><!\"Y", c)) {
		printf("C.Itoh: ESC %#x at %lu\n", c, (unsigned long) i);
		d->bad++;
		return;
	    }
	    break;
	default:			/* EP_TOSHIBA */
	    if (c == 0x1a || c == '*' || c == 0x1f || c == 'K')
		i++;
	    else if (c == 'L')
		spacing = Digits(b, n, &i, 2) * dl->vunit / 48;
	    else if (c == 0x09 && i < n) {
		x = (b[i++] - 1) * hdpi / 10;
		OtherTab(d, x);
	    } else if (c == ';' || c == 0x1d) {
		gdpi = c == ';' ? 180 : 360;
		cols = (size_t) Digits(b, n, &i, 4);
		if (hdpi % gdpi || i + cols * 4 > n) {
		    d->bad++;
		    return;
		}
		scale = hdpi / gdpi;
		d->gfx++;
		for (k = 0; k < cols; k++, x += scale)
		    for (g = 0; g < 4; g++)
			for (pin = 0; pin < 6; pin++)
			    if (b[i + k * 4 + g] & (0x20 >> pin))
				EpsonDot(d, x, y + (g * 6 + pin - dl->skip) *
					 dl->vunit / 180);
		i += cols * 4;
	    } else if (c == 'I') {
		if (!d->pages)
		    d->underline++;
	    } else if (!strchr("Z.><[]M\x12\x14J!\"QR", c)) {
		printf("Toshiba: ESC %#x at %lu\n", c, (unsigned long) i);
		d->bad++;
		return;
	    }
	    break;
	}
    }
}

static void EpsonDecodeFileDialect(const byte *b, size_t n,
				   EpsonDecode *d, int hdpi,
				   const EpsonDialect *dl)
{
    size_t i = 0, k, cols;
    int x = 0, y = 0, scale, pin, c, dpi, colBytes, pitch, tab = 0;
    int spacing = 0;

    if (dl && dl->proto != EP_ESCP) {
	OtherDecodeFile(b, n, d, hdpi, dl);
	return;
    }
    memset(d, 0, sizeof(*d));
    while (i < n) {
	c = b[i++];
	if (c == 0x09 && dl) {		/* to the tab stop, 10 pitch */
	    x = tab * hdpi / 10;
	    d->tabs++;
	    if (!d->pages && d->textLen < (int) sizeof(d->text) - 1)
		d->text[d->textLen++] = '|';
	    if (x && d->pages < 8 && !d->absX[d->pages])
		d->absX[d->pages] = x;
	    continue;
	}
	if (c == '\n' && dl && dl->lq) {	/* a line of the spacing */
	    y += spacing;
	    x = 0;
	    continue;
	}
	if (c == 0x1c && dl && dl->lq && i + 1 < n) {	/* NEC FS */
	    c = b[i++];
	    if (c == '3') {
		spacing = b[i++] * dl->vunit / 360;
		continue;
	    }
	    if (c == 'Z' && i + 2 <= n) {	/* 360 dpi, 24 pins */
		cols = (size_t) (b[i] | b[i + 1] << 8);
		i += 2;
		if (i + cols * 3 > n || hdpi % 360) {
		    d->bad++;
		    return;
		}
		scale = hdpi / 360;
		d->gfx++;
		for (k = 0; k < cols; k++, x += scale)
		    for (pin = 0; pin < 24; pin++)
			if (b[i + k * 3 + pin / 8] & (0x80 >> (pin % 8)))
			    EpsonDot(d, x, y + pin * dl->vunit / 180);
		i += cols * 3;
		continue;
	    }
	    d->bad++;
	    return;
	}
	if (c == 0 || c == 0x0f || c == 0x12 || c == 0x09 ||
	    (c == 0x01 && dl))	/* Star: the bin of an empty ASF code */
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
	case 'P':
	    if (dl && dl->ibm)
		i++;
	    break;
	case '@': case 'O': case '6': case '8': case '4': case '5':
	case 'M': case 'T': case 'E': case 'F': case 'G':
	case 'H': case 'g': case ':':
	    break;
	case '+':
	    if (dl && dl->lq && i < n)
		spacing = b[i] * dl->vunit / 360;
	    i++;
	    break;
	case '3':
	    if (dl && dl->lq && dl->ibm && i < n)
		spacing = b[i] * dl->vunit / 360;
	    i++;
	    break;
	case '[':		/* IBM: ESC [ c, a count, the bytes */
	    if (i + 3 > n) {
		d->bad++;
		return;
	    }
	    c = b[i];
	    cols = (size_t) (b[i + 1] | b[i + 2] << 8);
	    i += 3;
	    if (i + cols > n) {
		d->bad++;
		return;
	    }
	    if (c == 'g' && cols) {	/* graphics: the mode, the columns */
		c = b[i];
		colBytes = c >= 13 ? 6 : c >= 8 ? 3 : 1;
		dpi = c == 0 ? 60 : c == 11 ? 180 : c == 12 || c == 16 ? 360 :
		      0;
		pitch = dl->vunit / (c >= 13 ? 360 : c >= 8 ? 180 : 72);
		if (!dpi || hdpi % dpi || (cols - 1) % colBytes) {
		    d->bad++;
		    return;
		}
		scale = hdpi / dpi;
		d->modes[c]++;
		for (k = 0; k < (cols - 1) / colBytes; k++, x += scale)
		    for (pin = 0; pin < 8 * colBytes; pin++)
			if (b[i + 1 + k * colBytes + pin / 8] &
			    (0x80 >> (pin % 8)))
			    EpsonDot(d, x, y + pin * pitch);
	    }
	    i += cols;
	    break;
	case 't': case 'U': case 'r': case 'R': case 'x': case 'S':
	case 'W': case 'w': case 'k': case 'p':
	case 0x19: case 'B': case 'I': case '_': case '7':
	    i++;
	    break;
	case 'K': case 'L': case 'Y': case 'Z': case 'z':
	    dpi = EpsonGfxDpi(dl, c);
	    if (!dpi || hdpi % dpi || i + 2 > n) {
		d->bad++;
		return;
	    }
	    cols = (size_t) (b[i] | b[i + 1] << 8);
	    i += 2;
	    if (i + cols > n) {
		d->bad++;
		return;
	    }
	    scale = hdpi / dpi;
	    d->gfx++;
	    for (k = 0; k < cols; k++, x += scale)
		for (pin = 0; pin < 8; pin++)
		    if (b[i + k] & (0x80 >> pin))
			EpsonDot(d, x, y + pin * dl->vunit / 72);
	    i += cols;
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
	case 'd':			/* IBM: after a CR */
	    if (!dl || !dl->ibm) {
		d->bad++;
		return;
	    }
	    /* FALLTHROUGH */
	case '$':
	    if (i + 1 < n)
		x = (b[i] | b[i + 1] << 8) * hdpi /
		    (dl && dl->absUnit ? dl->absUnit : 60);
	    i += 2;
	    if (!d->pages && d->textLen < (int) sizeof(d->text) - 1)
		d->text[d->textLen++] = '|';
	    if (x && d->pages < 8 && !d->absX[d->pages])
		d->absX[d->pages] = x;
	    break;
	case 'D':
	    if (i < n && b[i])
		tab = b[i];
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
	    /* 8 pins 1/72" (9-pin) or 1/60" (24-pin) apart: 3 units;
	     * 24 pins: 1 unit */
	    dpi = c == 0 ? 60 : c == 1 ? 120 : c == 3 ? 240 : c == 39 ? 180 :
		  c == 40 || c == 73 ? 360 : 0;
	    colBytes = c >= 72 ? 6 : c >= 39 ? 3 : 1;
	    pitch = c >= 39 ? 1 : 3;
	    if (dl && dl->vunit)	/* the pins' distance in units */
		pitch = dl->vunit / (c >= 72 ? 360 : c >= 39 ? 180 :
				     dl->lq ? 60 : 72);
	    if (!dpi || hdpi % dpi || i + cols * colBytes > n) {
		d->bad++;
		return;
	    }
	    scale = hdpi / dpi;
	    d->modes[c]++;
	    for (k = 0; k < cols; k++, x += scale)
		for (pin = 0; pin < 8 * colBytes; pin++)
		    if (b[i + k * colBytes + pin / 8] & (0x80 >> (pin % 8)))
			EpsonDot(d, x, y + pin * pitch);
	    i += cols * colBytes;
	    break;
	default:
	    printf("ESC/P: ESC %#x at %lu\n", c, (unsigned long) i);
	    d->bad++;
	    return;
	}
    }
}

static void EpsonDecodeFile(const byte *b, size_t n, EpsonDecode *d,
			    int hdpi)
{
    EpsonDecodeFileDialect(b, n, d, hdpi, NULL);
}

/* the spool file of MakeSpoolFile through an Epson driver to a file in
 * each graphics mode (codes: its ESC * modes); hdpi the dots across,
 * vunit the line feed units an inch: two pages; on the first the
 * rectangle and the line, 72..500 by 72..400 points of the document, on
 * the second the filled rectangle and the ellipse, 100..400 by 100..600
 * points, each less the document's margins (18 pt), from where the head
 * starts: the printable area of the paper */
static void TestEpson(const char *printer, const char *device, int out,
		      int hdpi, int vunit, const int codes[3])
{
    static const word modes[] = { PM_GRAPHICS_LOW_RES, PM_GRAPHICS_MED_RES,
				  PM_GRAPHICS_HI_RES };
    JobParameters jp;
    EpsonDecode d;
    char name[13], rel[40];
    size_t n;
    byte *b;
    int m, w, h;

#define EP_NEAR(v, pt, res) \
    ((v) > (pt) * (res) / 72 - 12 && (v) < (pt) * (res) / 72 + 12)
    for (m = 0; m < 3; m++) {
	snprintf(rel, sizeof(rel), "out%d.prn", out + m);
	MakeSpoolFile(name);
	FillJob(&jp, name, printer, PPT_FILE, rel);
	strcpy(jp.JP_deviceName, device);
	jp.JP_printMode = modes[m];
	((PrintDriverUIData *) jp.JP_printerData)->PUID_countryCode = PCC_USA;
	CHECK(pSpoolAddJob(&jp) != 0xffff);
	CHECK(WaitIdle());
	snprintf(rel, sizeof(rel), "DOCUMENT/out%d.prn", out + m);
	b = (byte *) Get(rel, &n);
	CHECK(b != NULL && n > 100);
	if (!b)
	    continue;
	CHECK(n > 4 && b[0] == 0x1b && b[1] == '@');
	EpsonDecodeFile(b, n, &d, hdpi);
	w = d.right - d.left;
	h = d.bottom - d.top;
	printf("%s mode %d: %lu bytes, %d pages, %ld dots, %dx%d at %d,%d, "
	       "ESC * %d: %d\n", device, modes[m], (unsigned long) n,
	       d.pages, d.dots, w, h, d.left, d.top, codes[m],
	       d.modes[codes[m]]);
	printf("  page 2: %ld dots, %dx%d at %d,%d\n", d.dots2,
	       d.right2 - d.left2, d.bottom2 - d.top2, d.left2, d.top2);
	CHECK(!d.bad);
	CHECK(d.pages == 2);
	CHECK(d.modes[codes[m]] > 0);
	CHECK(EP_NEAR(w, 428, hdpi) && EP_NEAR(h, 328, vunit));
	CHECK(EP_NEAR(d.left, 54, hdpi) && EP_NEAR(d.top, 54, vunit));
	CHECK(EP_NEAR(d.right2 - d.left2, 300, hdpi) &&
	      EP_NEAR(d.bottom2 - d.top2, 500, vunit));
	CHECK(EP_NEAR(d.left2, 82, hdpi) && EP_NEAR(d.top2, 82, vunit));
	free(b);
    }
#undef EP_NEAR
}

/* a text page through the Epson 9-pin driver in its draft mode: the
 * strings in the order of their lines, then left to right, each at its
 * place; underline switched on for the underlined one; two pages */
static void TestEpsonText(const char *printer, const char *device,
			  const char *out, int hdpi, word mode)
{
    JobParameters jp;
    EpsonDecode d;
    GStateHandle gs;
    FileHandle fh;
    char name[13], rel[40];
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

    FillJob(&jp, name, printer, PPT_FILE, out);
    strcpy(jp.JP_deviceName, device);
    jp.JP_printMode = mode;
    ((PrintDriverUIData *) jp.JP_printerData)->PUID_countryCode = PCC_USA;
    CHECK(pSpoolAddJob(&jp) != 0xffff);
    CHECK(WaitIdle());
    snprintf(rel, sizeof(rel), "DOCUMENT/%s", out);
    b = (byte *) Get(rel, &n);
    CHECK(b != NULL && n > 20);
    if (!b)
	return;
    EpsonDecodeFile(b, n, &d, hdpi);
    d.text[d.textLen] = 0;
    printf("%s text %d: %lu bytes, %d pages, \"%s\", underline %d, "
	   "x %d %d\n", device, mode, (unsigned long) n, d.pages, d.text, d.underline, d.absX[0], d.absX[1]);
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
    CHECK(d.absX[0] == 45 * hdpi / 60 && d.absX[1] == 45 * hdpi / 60);
    free(b);
}


/* ---------------------------------------------------------------------
 * The PCL drivers: PCL back into dots and text
 * ------------------------------------------------------------------- */

typedef struct {
    int pages;
    int bad;			/* an unknown code or a truncated one */
    int res;			/* ESC * t #R */
    int rasters, rows, tiff;	/* ESC * r 1A, rows, rows in mode 2 */
    int left, top, right, bottom;	/* the dots of page 1 */
    long dots;
    int left2, top2, right2, bottom2;	/* and of page 2 */
    long dots2;
    char text[256];		/* the text of page 1, '|' at ESC * p */
    int textLen;
    int underline;		/* ESC & d 3 D on page 1 */
    int curX, curY;		/* the first ESC * p (x, y) on page 1 */
    char font[64];		/* the first ESC ( s ... T */
    int pageLength, inputPath;	/* ESC & l # p, ESC & l # H */
    /* PCL 4: the rasters at the cursor, in 1/300" (abs, set by the
     * caller); the soft fonts */
    int abs, posX, posY, rasterX;	/* abs: PclDecodeFile's */
    int copies, duplex, uel, fontCtl;	/* ESC & l # X, # S, ESC % - 12345 X,
					 * ESC * c # F */
    int headers, chars, inkChars, badChars, selects, moves;
    char downloaded[256];		/* the characters downloaded, page 1 */
    /* color: the planes of a row (ESC * b # V, the last by W), their
     * dots by plane (pages 1 and 2), the palette (ESC * r # U), PJL */
    int planes, palette, pjl;
    long planeDots[4], planeDots2[4];
} PclDecode;

static void PclDot(PclDecode *d, int x, int y)
{
    int *l, *t, *r, *b;
    long *n;

    if (d->pages > 1)
	return;
    l = d->pages ? &d->left2 : &d->left;
    t = d->pages ? &d->top2 : &d->top;
    r = d->pages ? &d->right2 : &d->right;
    b = d->pages ? &d->bottom2 : &d->bottom;
    n = d->pages ? &d->dots2 : &d->dots;
    if (!*n || x < *l)
	*l = x;
    if (!*n || x > *r)
	*r = x;
    if (!*n || y < *t)
	*t = y;
    if (!*n || y > *b)
	*b = y;
    (*n)++;
}

/* a row of raster bytes at row y of the page (abs: at the cursor) */
static void PclRow(PclDecode *d, const byte *row, int n, int y)
{
    int k, bit, sc = d->abs && d->res ? 300 / d->res : 1;

    for (k = 0; k < n; k++)
	for (bit = 0; bit < 8; bit++)
	    if (row[k] & (0x80 >> bit)) {
		if (d->abs)
		    PclDot(d, d->rasterX + (k * 8 + bit) * sc, d->posY);
		else
		    PclDot(d, k * 8 + bit, y);
	    }
    if (d->abs)
	d->posY += sc;
}

static void PclDecodeFile(const byte *b, size_t n, PclDecode *d, int abs)
{
    static byte row[4096], acc[4096];
    size_t i = 0;
    int c, y = 0, mode = 0, val, neg, plus, rn, k, cnt, chr = 0, w, h;
    int plane = 0, an = 0;
    char par, grp, term;

    memset(d, 0, sizeof(*d));
    d->abs = abs;
    d->curX = d->curY = -1;
    while (i < n) {
	c = b[i++];
	if (c == 0x0f || c == '\r' || c == '\n')
	    continue;
	if (c == 0x0c) {
	    d->pages++;
	    y = 0;
	    d->posX = d->posY = 0;
	    continue;
	}
	if (c == '@' && i + 3 <= n && !memcmp(b + i, "PJL", 3)) {
	    d->pjl++;			/* a PJL line up to its LF */
	    while (i < n && b[i] != '\n')
		i++;
	    continue;
	}
	if (c != 0x1b) {
	    if (c >= ' ') {
		if (!d->pages && d->textLen < (int) sizeof(d->text) - 1)
		    d->text[d->textLen++] = (char) c;
		continue;
	    }
	    d->bad++;
	    return;
	}
	if (i >= n) {
	    d->bad++;
	    return;
	}
	par = (char) b[i++];
	if (par == 'E')
	    continue;
	/* a parameterized command: ESC par [group] (value letter)+, the
	 * last letter upper case; ESC ( has no group but for s */
	grp = 0;
	if (i < n && (par != '(' || b[i] == 's') && b[i] >= 0x60 &&
	    b[i] <= 0x7e)
	    grp = (char) b[i++];
	do {
	    val = neg = plus = 0;
	    if (i < n && (b[i] == '-' || b[i] == '+')) {
		neg = b[i] == '-';
		plus = b[i++] == '+';
	    }
	    while (i < n && b[i] >= '0' && b[i] <= '9')
		val = val * 10 + (b[i++] - '0');
	    if (neg)
		val = -val;
	    if (i >= n) {
		d->bad++;
		return;
	    }
	    term = (char) b[i++];
	    if (par == '(' && grp == 's' && !d->pages) {
		k = (int) strlen(d->font);
		if (!strchr(d->font, 'T') && k < (int) sizeof(d->font) - 8)
		    snprintf(d->font + k, sizeof(d->font) - k, "%d%c", val,
			     term);
	    }
	    switch (par << 16 | grp << 8 |
		    (term >= 'A' && term <= 'Z' ? term | 0x20 : term)) {
	    case '*' << 16 | 'p' << 8 | 'x':
		if (plus) {			/* a space of a soft font */
		    d->posX += val;
		    d->moves++;
		    if (!d->pages && d->textLen < (int) sizeof(d->text) - 1)
			d->text[d->textLen++] = ' ';
		    break;
		}
		d->posX = val;
		if (!d->pages && d->curX < 0)
		    d->curX = val;
		if (!d->pages && d->textLen < (int) sizeof(d->text) - 1)
		    d->text[d->textLen++] = '|';
		break;
	    case '*' << 16 | 'p' << 8 | 'y':
		d->posY = val;
		if (!d->pages && d->curY < 0)
		    d->curY = val;
		break;
	    case '&' << 16 | 'l' << 8 | 'x':
		d->copies = val;
		break;
	    case '&' << 16 | 'l' << 8 | 's':
		d->duplex = val;
		break;
	    case '%' << 16 | 0 << 8 | 'x':
		if (val == -12345)
		    d->uel++;
		break;
	    case '&' << 16 | 'f' << 8 | 'x':
	    case '*' << 16 | 'c' << 8 | 'd':
		break;
	    case '*' << 16 | 'c' << 8 | 'e':
		chr = val;
		break;
	    case '(' << 16 | 0 << 8 | 'x':
		d->selects++;
		break;
	    case ')' << 16 | 's' << 8 | 'w':	/* a soft font's header */
		if (val != 64 || i + 64 > n || b[i] || b[i + 1] != 64) {
		    d->bad++;
		    return;
		}
		d->headers++;
		i += 64;
		break;
	    case '(' << 16 | 's' << 8 | 'w':	/* a character */
		if (val < 2 || i + (size_t) val > n) {
		    d->bad++;
		    return;
		}
		if (b[i] == 4 && !b[i + 1]) {	/* not a continuation */
		    d->chars++;
		    w = b[i + 10] << 8 | b[i + 11];
		    h = b[i + 12] << 8 | b[i + 13];
		    if (val < 16 || (w & 7) || val - 16 > (w / 8) * h)
			d->badChars++;
		    if (!d->pages && chr > ' ' && chr < 256 &&
			!strchr(d->downloaded, chr) &&
			strlen(d->downloaded) < sizeof(d->downloaded) - 1)
			d->downloaded[strlen(d->downloaded)] = (char) chr;
		    for (k = 16; k < val && !b[i + k]; k++)
			;
		    if (k < val)
			d->inkChars++;
		} else if (b[i] != 4 || b[i + 1] != 1)
		    d->badChars++;
		i += (size_t) val;
		break;
	    case '*' << 16 | 't' << 8 | 'r':
		d->res = val;
		break;
	    case '*' << 16 | 'r' << 8 | 'a':
		d->rasters++;
		d->rasterX = d->posX;
		break;
	    case '*' << 16 | 'c' << 8 | 'f':
		if (val == 2)
		    d->fontCtl++;
		break;
	    case '*' << 16 | 'r' << 8 | 'b':
	    case '&' << 16 | 'a' << 8 | 'r':
	    case '&' << 16 | 'l' << 8 | 'd':
	    case '&' << 16 | 'l' << 8 | 'e':
	    case '&' << 16 | 'l' << 8 | 'f':
	    case '&' << 16 | 'l' << 8 | 'o':
	    case '&' << 16 | 'l' << 8 | 'l':
	    case '&' << 16 | 'd' << 8 | '@':
	    case '(' << 16 | 0 << 8 | 'u':
	    case '(' << 16 | 0 << 8 | 'n':
	    case '(' << 16 | 's' << 8 | 'p':
	    case '(' << 16 | 's' << 8 | 'h':
	    case '(' << 16 | 's' << 8 | 'v':
	    case '(' << 16 | 's' << 8 | 's':
	    case '(' << 16 | 's' << 8 | 'b':
	    case '(' << 16 | 's' << 8 | 't':
		break;
	    case '&' << 16 | 'l' << 8 | 'p':
		d->pageLength = val;
		break;
	    case '&' << 16 | 'l' << 8 | 'h':
		d->inputPath = val;
		break;
	    case '&' << 16 | 'd' << 8 | 'd':
		if (!d->pages)
		    d->underline++;
		break;
	    case '*' << 16 | 'b' << 8 | 'm':
		mode = val;
		break;
	    case '*' << 16 | 'b' << 8 | 'v':
	    case '*' << 16 | 'b' << 8 | 'w':
		if (val < 0 || i + (size_t) val > n) {
		    d->bad++;
		    return;
		}
		rn = 0;
		if (mode == 0) {
		    memcpy(row, b + i, (size_t) val);
		    rn = val;
		} else if (mode == 2) {
		    d->tiff++;
		    for (k = 0; k < val; ) {
			cnt = (signed char) b[i + k++];
			if (cnt >= 0) {
			    if (k + cnt + 1 > val ||
				rn + cnt + 1 > (int) sizeof(row)) {
				d->bad++;
				return;
			    }
			    memcpy(row + rn, b + i + k, (size_t) cnt + 1);
			    rn += cnt + 1;
			    k += cnt + 1;
			} else if (cnt != -128) {
			    if (k >= val || rn + 1 - cnt > (int) sizeof(row)) {
				d->bad++;
				return;
			    }
			    memset(row + rn, b[i + k++], (size_t) (1 - cnt));
			    rn += 1 - cnt;
			}
		    }
		} else {
		    d->bad++;
		    return;
		}
		i += (size_t) val;
		/* a plane: into the row of all planes */
		for (k = 0; k < rn; k++) {
		    acc[k] |= row[k];
		    if (d->pages < 2)
			for (cnt = 0; cnt < 8; cnt++)
			    if (row[k] & (0x80 >> cnt))
				(d->pages ? d->planeDots2 : d->planeDots)
				    [plane < 4 ? plane : 3]++;
		}
		if (rn > an)
		    an = rn;
		plane++;
		if ((term | 0x20) == 'v')
		    break;
		if (plane > d->planes)
		    d->planes = plane;
		PclRow(d, acc, an, y++);
		memset(acc, 0, (size_t) an);
		plane = an = 0;
		d->rows++;
		break;
	    case '*' << 16 | 'r' << 8 | 'u':
		d->palette = val;
		break;
	    case '*' << 16 | 'r' << 8 | 'c':	/* the end of a raster */
	    case '*' << 16 | 'p' << 8 | 'n':	/* unidirectional */
	    case '&' << 16 | 'a' << 8 | 'n':	/* no negative motion */
		break;
	    default:
		printf("PCL: ESC %c%c%d%c\n", par, grp ? grp : ' ', val,
		       term);
		d->bad++;
		return;
	    }
	} while (term >= 0x60 && term <= 0x7e);
    }
}

/* the spool file of MakeSpoolFile through the DeskJet driver to a file
 * in each graphics mode, as TIFF or uncompressed: two pages, the figures
 * of TestEpson at their places (the rows from the page's first, the
 * dots from the left edge of the printable area) */
static const int pclRes[3] = { 75, 150, 300 };

static void TestPclRaster(int out, int tiff, const char *section,
			  const char *device, int planes, int palette,
			  int pjl, const int *res)
{
    static const word modes[] = { PM_GRAPHICS_LOW_RES, PM_GRAPHICS_MED_RES,
				  PM_GRAPHICS_HI_RES };
    JobParameters jp;
    PrintDriverUIData *ui = (PrintDriverUIData *) jp.JP_printerData;
    PclDecode d;
    char name[13], rel[40];
    size_t n;
    byte *b;
    int m, w, h, r;

#define PCL_NEAR(v, pt) \
    ((v) > (pt) * r / 72 - 8 && (v) < (pt) * r / 72 + 8)
    for (m = 0; m < 3; m++) {
	r = res[m];
	snprintf(rel, sizeof(rel), "out%d.pcl", out + m);
	MakeSpoolFile(name);
	FillJob(&jp, name, section, PPT_FILE, rel);
	strcpy(jp.JP_deviceName, device);
	jp.JP_printMode = modes[m];
	ui->PUID_compressionMode = (byte) (tiff ? 2 : 0);
	ui->PUID_symbolSet = PSS_ROMAN8;
	CHECK(pSpoolAddJob(&jp) != 0xffff);
	CHECK(WaitIdle());
	snprintf(rel, sizeof(rel), "DOCUMENT/out%d.pcl", out + m);
	b = (byte *) Get(rel, &n);
	CHECK(b != NULL && n > 100);
	if (!b)
	    continue;
	CHECK(n > 4 && b[0] == 0x1b &&
	      b[1] == (pjl ? '%' : 'E'));
	PclDecodeFile(b, n, &d, 0);
	w = d.right - d.left;
	h = d.bottom - d.top;
	printf("%s mode %d%s: %lu bytes, %d pages, %d rasters, %d rows "
	       "(%d TIFF), %ld dots, %dx%d at %d,%d, page 2: %ld dots, "
	       "%dx%d at %d,%d\n", device, modes[m], tiff ? " TIFF" : "",
	       (unsigned long) n, d.pages, d.rasters, d.rows, d.tiff, d.dots,
	       w, h, d.left, d.top, d.dots2, d.right2 - d.left2,
	       d.bottom2 - d.top2, d.left2, d.top2);
	printf("  %d planes (%ld %ld %ld %ld dots, page 2: %ld %ld %ld %ld), "
	       "palette %d, %d UEL, %d PJL\n", d.planes, d.planeDots[0],
	       d.planeDots[1], d.planeDots[2], d.planeDots[3],
	       d.planeDots2[0], d.planeDots2[1], d.planeDots2[2],
	       d.planeDots2[3], d.palette, d.uel, d.pjl);
	CHECK(!d.bad);
	/* a color row ends with the empty row of Pr1ScanlineFeed */
	CHECK(d.planes == (planes > 1 ? planes + 1 : 1) &&
	      d.palette == palette);
	if (planes == 3) {
	    /* page 1 black: all three (C, M, Y); page 2 the blue fill:
	     * cyan and magenta, yellow for the black ellipse only */
	    CHECK(d.planeDots[0] == d.dots && d.planeDots[1] == d.dots &&
		  d.planeDots[2] == d.dots);
	    CHECK(d.planeDots2[0] > 10 * d.planeDots2[2] &&
		  d.planeDots2[1] > 10 * d.planeDots2[2] &&
		  d.planeDots2[2] > 0);
	} else if (planes == 4) {
	    /* K, C, M, Y: black on black (the undercolor removal) */
	    CHECK(d.planeDots[0] == d.dots && !d.planeDots[1] &&
		  !d.planeDots[2] && !d.planeDots[3]);
	    CHECK(d.planeDots2[1] > 10 * d.planeDots2[0] &&
		  d.planeDots2[2] > 10 * d.planeDots2[0] &&
		  d.planeDots2[0] > 0 && !d.planeDots2[3]);
	}
	CHECK(pjl ? d.uel == 2 && d.pjl == 2 : !d.uel && !d.pjl);
	CHECK(d.pages == 2);
	CHECK(d.res == r);
	/* 792 pt in lines of 9 pt; tray 1 */
	CHECK(d.pageLength == 88 && d.inputPath == 1);
	if (d.pageLength != 88 || d.inputPath != 1)
	    printf("page length %d, input path %d\n", d.pageLength,
		   d.inputPath);
	CHECK(tiff ? d.tiff > 0 : d.tiff == 0);
	CHECK(PCL_NEAR(w, 428) && PCL_NEAR(h, 328));
	CHECK(PCL_NEAR(d.left, 54) && PCL_NEAR(d.top, 54));
	CHECK(PCL_NEAR(d.right2 - d.left2, 300) &&
	      PCL_NEAR(d.bottom2 - d.top2, 500));
	CHECK(PCL_NEAR(d.left2, 82) && PCL_NEAR(d.top2, 82));
	free(b);
    }
#undef PCL_NEAR
}

/* ---------------------------------------------------------------------
 * The Canon BJC raster mode: ESC [ K and ESC ( x, each with a 16-bit
 * length (low byte first), back into dots
 * ------------------------------------------------------------------- */

typedef struct {
    int pages, bad, resets, emulation;	/* ESC [ K, ESC @ */
    int resX, resY, method, quality;	/* ESC ( d, ESC ( c */
    int compression, format;		/* ESC ( b, ESC ( t: 1, 1 (CMYK) */
    long dots[4], dots2[4];		/* Y C M K, pages 1 and 2 */
    int left, top, right, bottom;	/* all dots of page 1 */
    long all;				/* of all the planes */
} BjcDecode;

static void BjcDecodeFile(const byte *b, size_t n, BjcDecode *d)
{
    static const char inks[] = "YCMK";
    size_t i = 0, len;
    const byte *p;
    int c, y = 0, k, rn, cnt, bit, plane;
    byte row[4096];

    memset(d, 0, sizeof(*d));
    while (i < n) {
	c = b[i++];
	if (c == '\r')
	    continue;
	if (c == 0x0c) {
	    d->pages++;
	    y = 0;
	    continue;
	}
	if (c != 0x1b || i >= n) {
	    d->bad++;
	    return;
	}
	c = b[i++];
	if (c == '@') {
	    d->emulation++;
	    continue;
	}
	if ((c != '[' && c != '(') || i + 3 > n) {
	    d->bad++;
	    return;
	}
	k = b[i];
	len = (size_t) (b[i + 1] | b[i + 2] << 8);
	p = b + i + 3;
	i += 3 + len;
	if (i > n) {
	    d->bad++;
	    return;
	}
	if (c == '[') {
	    if (k == 'K')
		d->resets++;
	    else
		d->bad++;
	    continue;
	}
	switch (k) {
	case 'b':
	    d->compression = p[0];
	    break;
	case 't':
	    d->format = p[1] == 0 ? p[0] : -1;
	    break;
	case 'd':
	    d->resY = p[0] << 8 | p[1];
	    d->resX = p[2] << 8 | p[3];
	    break;
	case 'c':
	    d->method = p[0];
	    d->quality = len > 1 ? p[1] : -1;
	    break;
	case 'e':
	    y += p[0] << 8 | p[1];
	    break;
	case 'A':
	    plane = (int) (strchr(inks, p[0]) ? strchr(inks, p[0]) - inks
						: -1);
	    if (plane < 0 || !len) {
		d->bad++;
		return;
	    }
	    for (k = 1, rn = 0; k < (int) len; ) {
		cnt = (signed char) p[k++];
		if (cnt >= 0) {
		    if (k + cnt + 1 > (int) len ||
			rn + cnt + 1 > (int) sizeof(row)) {
			d->bad++;
			return;
		    }
		    memcpy(row + rn, p + k, (size_t) cnt + 1);
		    rn += cnt + 1;
		    k += cnt + 1;
		} else if (cnt != -128) {
		    if (k >= (int) len || rn + 1 - cnt > (int) sizeof(row)) {
			d->bad++;
			return;
		    }
		    memset(row + rn, p[k++], (size_t) (1 - cnt));
		    rn += 1 - cnt;
		}
	    }
	    if (d->pages > 1)
		break;
	    for (k = 0; k < rn; k++)
		for (bit = 0; bit < 8; bit++)
		    if (row[k] & (0x80 >> bit)) {
			(d->pages ? d->dots2 : d->dots)[plane]++;
			if (d->pages)
			    continue;
			cnt = k * 8 + bit;
			if (!d->all || cnt < d->left)
			    d->left = cnt;
			if (!d->all || cnt > d->right)
			    d->right = cnt;
			if (!d->all || y < d->top)
			    d->top = y;
			if (!d->all || y > d->bottom)
			    d->bottom = y;
			d->all++;
		    }
	    break;
	default:
	    printf("BJC: ESC ( %c\n", k);
	    d->bad++;
	    return;
	}
    }
}

/* the spool file of MakeSpoolFile through the Canon BJC driver in its two
 * resolutions (the medium one is the high one): the planes of its inks,
 * the figures of page 1 at their places */
static void TestBjc(int out, const char *section, const char *device,
		    int inks)
{
    static const word modes[] = { PM_GRAPHICS_LOW_RES, PM_GRAPHICS_MED_RES,
				  PM_GRAPHICS_HI_RES };
    static const int res[] = { 180, 360, 360 };
    JobParameters jp;
    BjcDecode d;
    char name[13], rel[40];
    size_t n;
    byte *b;
    int m, r;

#define BJC_NEAR(v, pt) \
    ((v) > (pt) * r / 72 - 8 && (v) < (pt) * r / 72 + 8)
    for (m = 0; m < 3; m++) {
	r = res[m];
	snprintf(rel, sizeof(rel), "out%d.prn", out + m);
	MakeSpoolFile(name);
	FillJob(&jp, name, section, PPT_FILE, rel);
	strcpy(jp.JP_deviceName, device);
	jp.JP_printMode = modes[m];
	CHECK(pSpoolAddJob(&jp) != 0xffff);
	CHECK(WaitIdle());
	snprintf(rel, sizeof(rel), "DOCUMENT/out%d.prn", out + m);
	b = (byte *) Get(rel, &n);
	CHECK(b != NULL && n > 100);
	if (!b)
	    continue;
	BjcDecodeFile(b, n, &d);
	printf("%s mode %d: %lu bytes, %d pages, %dx%d dpi, quality %d, "
	       "dots Y C M K %ld %ld %ld %ld, page 2: %ld %ld %ld %ld, "
	       "%dx%d at %d,%d\n", device, modes[m], (unsigned long) n,
	       d.pages, d.resX, d.resY, d.quality, d.dots[0], d.dots[1],
	       d.dots[2], d.dots[3], d.dots2[0], d.dots2[1], d.dots2[2],
	       d.dots2[3], d.right - d.left, d.bottom - d.top, d.left, d.top);
	CHECK(!d.bad && d.pages == 2);
	CHECK(d.resets == 1 && d.emulation == 1);
	CHECK(d.compression == 1 && d.format == 1 && d.method == 0x10);
	CHECK(d.resX == r && d.resY == r);
	CHECK(d.quality == (m ? 0 : 2));
	CHECK(BJC_NEAR(d.right - d.left, 428) &&
	      BJC_NEAR(d.bottom - d.top, 328));
	CHECK(BJC_NEAR(d.left, 54) && BJC_NEAR(d.top, 54));
	if (inks == 1)			/* black only */
	    CHECK(d.dots[3] == d.all && !d.dots[0] && !d.dots[1] &&
		  !d.dots[2] && d.dots2[3] > 0 && !d.dots2[0] &&
		  !d.dots2[1] && !d.dots2[2]);
	else if (inks == 3)		/* black of C, M, Y; blue C and M */
	    CHECK(d.dots[0] * 3 == d.all && d.dots[1] == d.dots[0] &&
		  d.dots[2] == d.dots[0] && !d.dots[3] &&
		  d.dots2[1] > 10 * d.dots2[0] && d.dots2[0] > 0 &&
		  !d.dots2[3]);
	else				/* black of K; blue C and M */
	    CHECK(d.dots[3] == d.all && !d.dots[0] && !d.dots[1] &&
		  !d.dots[2] && d.dots2[1] > 10 * d.dots2[3] &&
		  d.dots2[2] > 10 * d.dots2[3] && d.dots2[3] > 0 &&
		  !d.dots2[0]);
	free(b);
    }
#undef BJC_NEAR
}

static void TestDeskjet(int out, int tiff)
{
    TestPclRaster(out, tiff, "DeskJet File", "HP DeskJet 500", 1, 0, 0,
			  pclRes);
}

/* a text page through the DeskJet driver in its NLQ mode: the strings
 * at their places, the printer's fonts chosen */
static void TestDeskjetText(const char *out)
{
    JobParameters jp;
    PclDecode d;
    GStateHandle gs;
    FileHandle fh;
    char name[13], rel[40];
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
    GrSetTextStyle(gs, TS_UNDERLINE, 0);
    GrDrawText(gs, 72, 150, "Under", 0);
    GrSetTextStyle(gs, 0, TS_UNDERLINE);
    GrNewPage(gs, PEC_FORM_FEED);
    GrDrawText(gs, 72, 72, "Page2", 0);
    GrNewPage(gs, PEC_FORM_FEED);
    CHECK(GrEndGString(gs) == GSET_NO_ERROR);
    GrDestroyGString(gs, 0, GSKT_LEAVE_DATA);
    FileClose(fh, FALSE);

    FillJob(&jp, name, "DeskJet File", PPT_FILE, out);
    strcpy(jp.JP_deviceName, "HP DeskJet 500");
    jp.JP_printMode = PM_TEXT_NLQ;
    ((PrintDriverUIData *) jp.JP_printerData)->PUID_symbolSet = PSS_ROMAN8;
    CHECK(pSpoolAddJob(&jp) != 0xffff);
    CHECK(WaitIdle());
    snprintf(rel, sizeof(rel), "DOCUMENT/%s", out);
    b = (byte *) Get(rel, &n);
    CHECK(b != NULL && n > 20);
    if (!b)
	return;
    PclDecodeFile(b, n, &d, 0);
    d.text[d.textLen] = 0;
    printf("DeskJet text: %lu bytes, %d pages, \"%s\", underline %d, "
	   "cursor %d,%d, font %s\n", (unsigned long) n, d.pages, d.text,
	   d.underline, d.curX, d.curY, d.font);
    CHECK(!d.bad);
    CHECK(d.pages == 2);
    CHECK(!strncmp(d.text, "|Hello World|Right|Under", 24));
    CHECK(d.underline >= 1);
    /* 72 pt less the document's margin (18) in 1/300" */
    CHECK(d.curX == 225);
    CHECK(Count((const char *) b, n, "\x1b(8U") == 1);	/* Roman-8 */
    free(b);
}

/* the spool file of MakeSpoolFile through the PCL 4 driver: the figures
 * of TestEpson at their places (in 1/300", the rasters at the cursor),
 * the swaths as one raster (much memory) or as the runs of their lines;
 * two copies the printer makes */
static void TestPcl4(int out, word mode, word memory, byte init,
		     int compressed)
{
    JobParameters jp;
    PrintDriverUIData *ui = (PrintDriverUIData *) jp.JP_printerData;
    PclDecode d;
    char name[13], rel[40];
    size_t n;
    byte *b;
    int w, h, r = 300;

#define PCL_NEAR(v, pt) \
    ((v) > (pt) * r / 72 - 12 && (v) < (pt) * r / 72 + 12)
    snprintf(rel, sizeof(rel), "out%d.pcl", out);
    MakeSpoolFile(name);
    FillJob(&jp, name, "LaserJet File", PPT_FILE, rel);
    strcpy(jp.JP_deviceName, "HP LaserJet IID");
    jp.JP_printMode = mode;
    jp.JP_numCopies = 2;
    ui->PUID_amountMemory = memory;
    ui->PUID_initMemory = init;
    ui->PUID_paperOutput = (PaperOutputOptions) (1 << POO_DUPLEX_OFFSET);
    ui->PUID_symbolSet = PSS_ROMAN8;
    CHECK(pSpoolAddJob(&jp) != 0xffff);
    CHECK(WaitIdle());
    snprintf(rel, sizeof(rel), "DOCUMENT/out%d.pcl", out);
    b = (byte *) Get(rel, &n);
    CHECK(b != NULL && n > 100);
    if (!b)
	return;
    CHECK(n > 4 && b[0] == 0x1b && b[1] == 'E');
    PclDecodeFile(b, n, &d, 1);
    w = d.right - d.left;
    h = d.bottom - d.top;
    printf("PCL 4 mode %d memory %#x init %d: %lu bytes, %d pages, %d "
	   "rasters, %d rows, %ld dots, %dx%d at %d,%d, page 2: %ld dots, "
	   "%dx%d at %d,%d, copies %d, duplex %d, font control %d\n", mode,
	   memory, (sbyte) init, (unsigned long) n, d.pages, d.rasters,
	   d.rows, d.dots, w, h, d.left, d.top, d.dots2, d.right2 - d.left2,
	   d.bottom2 - d.top2, d.left2, d.top2, d.copies, d.duplex,
	   Count((const char *) b, n, "\x1b*c"));
    CHECK(!d.bad);
    CHECK(d.pages == 2);		/* the copies are the printer's */
    CHECK(d.copies == 2 && d.duplex == 1);
    CHECK(d.res == 75 << (mode / 2));
    /* one raster a swath, or one a run */
    CHECK(compressed ? d.rasters > 2 * d.rows / 3 : d.rasters < d.rows / 8);
    CHECK(init == 0xff ? !Count((const char *) b, n, "\x1b*c") :
	  Count((const char *) b, n, init ? "\x1b*c1F" : "\x1b*c0F") == 1);
    CHECK(PCL_NEAR(w, 428) && PCL_NEAR(h, 328));
    CHECK(PCL_NEAR(d.right2 - d.left2, 300) &&
	  PCL_NEAR(d.bottom2 - d.top2, 500));
    CHECK(d.left2 - d.left > 0 && PCL_NEAR(d.left2 - d.left, 28) &&
	  PCL_NEAR(d.top2 - d.top, 28));
    free(b);
#undef PCL_NEAR
}

/* a text page through the PCL 4 driver: in soft fonts GEOS draws (a
 * printer of soft fonts; the font headers, the characters of the page,
 * spaces as moves, the fonts deleted at the page's end) or in the
 * printer's own fonts; the job's entry and exit codes of the device */
static void TestPcl4Text(const char *out, const char *device, int soft)
{
    JobParameters jp;
    PclDecode d;
    GStateHandle gs;
    FileHandle fh;
    FontID coop;
    char name[13], rel[40];
    word ch;
    size_t n;
    byte *b;

    fh = pSpoolCreateSpoolFile(name);
    CHECK(fh != 0);
    if (!fh)
	return;
    coop = GrCheckFontAvailName(FEF_OUTLINES, 0, "Cooper* Black");
    CHECK(coop != FID_INVALID);
    gs = GrCreateGString(fh, GST_STREAM, &ch);
    GrSetFont(gs, coop, (WWFixedAsDWord) 14 << 16);
    GrDrawText(gs, 300, 100, "Right", 0);
    GrDrawText(gs, 72, 100, "Hello World", 0);
    GrSetTextStyle(gs, TS_UNDERLINE, 0);
    GrDrawText(gs, 72, 150, "Under", 0);
    GrSetTextStyle(gs, TS_BOLD, TS_UNDERLINE);
    GrDrawText(gs, 72, 200, "Bold", 0);
    GrNewPage(gs, PEC_FORM_FEED);
    GrSetTextStyle(gs, 0, TS_BOLD);
    GrDrawText(gs, 72, 72, "Page2", 0);
    GrNewPage(gs, PEC_FORM_FEED);
    CHECK(GrEndGString(gs) == GSET_NO_ERROR);
    GrDestroyGString(gs, 0, GSKT_LEAVE_DATA);
    FileClose(fh, FALSE);

    FillJob(&jp, name, "LaserJet File", PPT_FILE, out);
    strcpy(jp.JP_deviceName, device);
    jp.JP_printMode = PM_TEXT_NLQ;
    ((PrintDriverUIData *) jp.JP_printerData)->PUID_symbolSet = PSS_ROMAN8;
    ((PrintDriverUIData *) jp.JP_printerData)->PUID_initMemory = 0xff;
    CHECK(pSpoolAddJob(&jp) != 0xffff);
    CHECK(WaitIdle());
    snprintf(rel, sizeof(rel), "DOCUMENT/%s", out);
    b = (byte *) Get(rel, &n);
    CHECK(b != NULL && n > 20);
    if (!b)
	return;
    PclDecodeFile(b, n, &d, 1);
    d.text[d.textLen] = 0;
    printf("PCL 4 text (%s): %lu bytes, %d pages, \"%s\", underline %d, "
	   "cursor %d,%d, font %s, %d headers, %d chars (%d with ink, %d "
	   "bad) \"%s\", %d selects, %d moves, %d deleted, %d UEL\n", device,
	   (unsigned long) n, d.pages, d.text, d.underline, d.curX, d.curY,
	   d.font, d.headers, d.chars, d.inkChars, d.badChars, d.downloaded,
	   d.selects, d.moves, d.fontCtl, d.uel);
    CHECK(!d.bad);
    CHECK(d.pages == 2);
    CHECK(d.underline >= 1);
    CHECK(d.curX == 225);
    if (soft) {
	/* Cooper Black plain and bold, again on page 2 */
	CHECK(d.headers == 3);
	CHECK(d.fontCtl == 3);
	CHECK(strstr(d.text, "|Hello World|Right|Under|Bold") != NULL);
	CHECK(d.moves >= 1);
	CHECK(!d.badChars && d.inkChars == d.chars);
	/* each character once a font and page: RightHeloWrdUn, Bold,
	 * Page2 */
	CHECK(strlen(d.downloaded) == 15 && strchr(d.downloaded, 'W'));
	CHECK(d.chars == 14 + 4 + 5);
	CHECK(d.selects >= 4);
	CHECK(!strchr(d.font, 'T'));
    } else {
	CHECK(!d.headers && !d.chars && !d.fontCtl);
	CHECK(strstr(d.text, "|Hello World|Right|Under|Bold") != NULL);
	CHECK(d.font[0] && strchr(d.font, 'T'));
    }
    free(b);
}

/* a job's entry and exit codes: the LaserJet 4 (PJL, a message on its
 * display) */
static void TestPcl4Custom(const char *out)
{
    static const char end[] = "\x1b%-12345X@PJL\r\n@PJL RDYMSG DISPLAY "
	"= \"\"\r\n\x1b%-12345X";
    JobParameters jp;
    char name[13], rel[40];
    size_t n;
    byte *b;

    MakeSpoolFile(name);
    FillJob(&jp, name, "LaserJet File", PPT_FILE, out);
    strcpy(jp.JP_deviceName, "HP LaserJet 4 (PCL Mode)");
    jp.JP_printMode = PM_GRAPHICS_LOW_RES;
    ((PrintDriverUIData *) jp.JP_printerData)->PUID_initMemory = 0xff;
    CHECK(pSpoolAddJob(&jp) != 0xffff);
    CHECK(WaitIdle());
    snprintf(rel, sizeof(rel), "DOCUMENT/%s", out);
    b = (byte *) Get(rel, &n);
    CHECK(b != NULL && n > 100);
    if (!b)
	return;
    CHECK(n > 40 && !memcmp(b, "\x1b%-12345X@PJL\r\n@PJL RDYMSG", 26));
    CHECK(Count((const char *) b, n, "@PJL ENTER LANGUAGE = PCL\r\n\x1b"
		"E") == 1);
    CHECK(n > sizeof(end) &&
	  !memcmp(b + n - (sizeof(end) - 1), end, sizeof(end) - 1));
    free(b);
}

/* the other Epson style drivers: the spool file of MakeSpoolFile in
 * each graphics mode (the figures of TestEpson at their places, on a
 * canvas of 480 dpi across (360 for the 24-pin ones) and the line feed
 * units down) and a text page in draft (the strings in order, underline
 * once, two pages) */
typedef struct {
    const char *printer, *driver, *device;
    EpsonDialect dl;
    int tabs;				/* X by tabs, else by spaces */
    int underline;			/* in the draft font */
    int hdpi;				/* the canvas across, 0: 480 */
} NinePin;

static void TestNinePin(const NinePin *np, int out)
{
    static const word modes[] = { PM_GRAPHICS_LOW_RES, PM_GRAPHICS_MED_RES,
				  PM_GRAPHICS_HI_RES };
    const int hdpi = np->hdpi ? np->hdpi : 480, vunit = np->dl.vunit;
    JobParameters jp;
    EpsonDecode d;
    GStateHandle gs;
    FileHandle fh;
    char name[13], rel[40], text[256];
    word ch;
    size_t n;
    byte *b;
    int m, w, h, h2, k, j;

#define EP_NEAR(v, pt, res) \
    ((v) > (pt) * (res) / 72 - 16 && (v) < (pt) * (res) / 72 + 16)
    for (m = 0; m < 3; m++) {
	snprintf(rel, sizeof(rel), "out%d.prn", out + m);
	MakeSpoolFile(name);
	FillJob(&jp, name, np->printer, PPT_FILE, rel);
	strcpy(jp.JP_deviceName, np->device);
	jp.JP_printMode = modes[m];
	((PrintDriverUIData *) jp.JP_printerData)->PUID_countryCode = PCC_USA;
	CHECK(pSpoolAddJob(&jp) != 0xffff);
	CHECK(WaitIdle());
	snprintf(rel, sizeof(rel), "DOCUMENT/out%d.prn", out + m);
	b = (byte *) Get(rel, &n);
	CHECK(b != NULL && n > 100);
	if (!b)
	    continue;
	EpsonDecodeFileDialect(b, n, &d, hdpi, &np->dl);
	w = d.right - d.left;
	h = d.bottom - d.top;
	h2 = d.bottom2 - d.top2;
	if (np->dl.band) {	/* the heights as the line feeds make them */
	    h = h * np->dl.band / np->dl.feed;
	    h2 = h2 * np->dl.band / np->dl.feed;
	}
	printf("%s mode %d: %lu bytes, %d pages, %d rasters, %ld dots, "
	       "%dx%d at %d,%d, page 2: %ld dots, %dx%d at %d,%d\n",
	       np->device, modes[m], (unsigned long) n, d.pages,
	       d.gfx + d.modes[0] + d.modes[1] + d.modes[3] + d.modes[11] +
	       d.modes[12] + d.modes[16] + d.modes[39] + d.modes[40] +
	       d.modes[73], d.dots, w, h,
	       d.left, d.top, d.dots2, d.right2 - d.left2,
	       d.bottom2 - d.top2, d.left2, d.top2);
	CHECK(!d.bad);
	CHECK(d.pages == 2);
	CHECK(EP_NEAR(w, 428, hdpi) && EP_NEAR(h, 328, vunit));
	CHECK(EP_NEAR(d.left, 54, hdpi) && EP_NEAR(d.top, 54, vunit));
	CHECK(EP_NEAR(d.right2 - d.left2, 300, hdpi) &&
	      EP_NEAR(h2, 500, vunit));
	CHECK(EP_NEAR(d.left2, 82, hdpi) && EP_NEAR(d.top2, 82, vunit));
	free(b);
    }
#undef EP_NEAR

    fh = pSpoolCreateSpoolFile(name);
    CHECK(fh != 0);
    if (!fh)
	return;
    gs = GrCreateGString(fh, GST_STREAM, &ch);
    GrDrawText(gs, 300, 100, "Right", 0);
    GrDrawText(gs, 72, 100, "Hello World", 0);
    GrSetTextStyle(gs, TS_UNDERLINE, 0);
    GrDrawText(gs, 72, 150, "Under", 0);
    GrSetTextStyle(gs, 0, TS_UNDERLINE);
    GrNewPage(gs, PEC_FORM_FEED);
    GrDrawText(gs, 72, 72, "Page2", 0);
    GrNewPage(gs, PEC_FORM_FEED);
    CHECK(GrEndGString(gs) == GSET_NO_ERROR);
    GrDestroyGString(gs, 0, GSKT_LEAVE_DATA);
    FileClose(fh, FALSE);
    snprintf(rel, sizeof(rel), "out%d.prn", out + 3);
    FillJob(&jp, name, np->printer, PPT_FILE, rel);
    strcpy(jp.JP_deviceName, np->device);
    jp.JP_printMode = PM_TEXT_DRAFT;
    ((PrintDriverUIData *) jp.JP_printerData)->PUID_countryCode = PCC_USA;
    CHECK(pSpoolAddJob(&jp) != 0xffff);
    CHECK(WaitIdle());
    snprintf(rel, sizeof(rel), "DOCUMENT/out%d.prn", out + 3);
    b = (byte *) Get(rel, &n);
    CHECK(b != NULL && n > 20);
    if (!b)
	return;
    EpsonDecodeFileDialect(b, n, &d, hdpi, &np->dl);
    d.text[d.textLen] = 0;
    for (k = j = 0; d.text[k]; k++)		/* the words only */
	if (d.text[k] != ' ' && d.text[k] != '|')
	    text[j++] = d.text[k];
    text[j] = 0;
    printf("%s text: %lu bytes, %d pages, \"%s\", underline %d, %d tabs\n",
	   np->device, (unsigned long) n, d.pages, d.text, d.underline,
	   d.tabs);
    CHECK(!d.bad);
    CHECK(d.pages == 2);
    CHECK(!strcmp(text, "HelloWorldRightUnder"));
    CHECK(d.underline == np->underline);
    CHECK(np->tabs ? d.tabs >= 3 : !d.tabs);
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
	"[Epson24 File]\r\ndriver = epson24.geo\r\n"
	"device = Epson LQ-800\r\n"
	"[DeskJet File]\r\ndriver = deskjet.geo\r\n"
	"device = HP DeskJet 500\r\n"
	"[DeskJet CMY File]\r\ndriver = dj500c.geo\r\n"
	"device = HP DeskJet 500C (Color)\r\n"
	"[PaintJet File]\r\ndriver = dj500c.geo\r\n"
	"device = HP PaintJet XL300 (3 Color)\r\n"
	"[DeskJet KCMY File]\r\ndriver = mercury.geo\r\n"
	"device = HP DeskJet 550C (4 Color)\r\n"
	"[BJC File]\r\ndriver = canonbjc.geo\r\n"
	"device = Canon BJC-2000\r\n"
	"[QuietJet File]\r\ndriver = quietjet.geo\r\n"
	"device = HP QuietJet (HP Mode)\r\n"
	"[LaserJet File]\r\ndriver = pcl4.geo\r\n"
	"device = HP LaserJet III\r\n"
	"[Epmx9 File]\r\ndriver = epmx9.geo\r\ndevice = Epson MX-80 Type III\r\n"
	"[Eplx9 File]\r\ndriver = eplx9.geo\r\ndevice = Epson Apex 80\r\n"
	"[Eprx9 File]\r\ndriver = eprx9.geo\r\ndevice = Epson RX-80\r\n"
	"[Bchip9 File]\r\ndriver = bchip9.geo\r\ndevice = Blue Chip M120\r\n"
	"[Diconix9 File]\r\ndriver = diconix9.geo\r\n"
	"device = Diconix 150 (Epson Mode)\r\n"
	"[Star9 File]\r\ndriver = star9.geo\r\ndevice = Star Gemini-10x\r\n"
	"[Grpr9 File]\r\ndriver = grpr9.geo\r\ndevice = IBM Graphics Printer\r\n"
	"[Nec24 File]\r\ndriver = nec24.geo\r\ndevice = NEC P2200\r\n"
	"[Epshi24 File]\r\ndriver = epshi24.geo\r\n"
	"device = Epson LQ-850\r\n"
	"[Epson48 File]\r\ndriver = epson48.geo\r\n"
	"device = Epson Stylus Color (Color)\r\n"
	"[Prop9 File]\r\ndriver = prop9.geo\r\ndevice = IBM Proprinter II\r\n"
	"[Propx24 File]\r\ndriver = propx24.geo\r\n"
	"device = IBM Proprinter X24\r\n"
	"[Ppds24 File]\r\ndriver = ppds24.geo\r\n"
	"device = IBM Personal Printer Series II 2390\r\n"
	"[Canon48 File]\r\ndriver = canon48.geo\r\ndevice = Canon BJ-130e\r\n"
	"[Oki9 File]\r\ndriver = oki9.geo\r\ndevice = Okidata ML 92\r\n"
	"[Citoh9 File]\r\ndriver = citoh9.geo\r\ndevice = C.Itoh 8510\r\n"
	"[Tosh24 File]\r\ndriver = tosh24.geo\r\ndevice = Toshiba P321\r\n"
	"[spool]\r\nhostPrinters = false\r\n";
    static const char patch[] = "% devpatch\r\n";
    GeodeLoadError e;
#ifdef GEOS_TEST_CUPS
#define CUPS_DOC_MAX 65536
    char *cupsDoc, got[16];
    FakeCupsJob *cupsJob = NULL;
    pid_t cups;
    static char hostCups[256];
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
	/* nor the printed files of the last run: a driver that does not
	 * load would leave them for the checks */
	for (i = 0; i < 3 * 300; i++) {
	    static const char *const ext[] = { "prn", "pcl", "ps" };
	    char rel[40];

	    snprintf(rel, sizeof(rel), "DOCUMENT/out%d.%s", i / 3,
		     ext[i % 3]);
	    HostPath(path, sizeof(path), rel);
	    remove(path);
	}
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
    Put("SYSTEM/PRINTER/epson24.geo", epson24_geode, epson24_geode_size);
    Put("SYSTEM/PRINTER/deskjet.geo", deskjet_geode, deskjet_geode_size);
    Put("SYSTEM/PRINTER/pcl4.geo", pcl4_geode, pcl4_geode_size);
    Put("SYSTEM/PRINTER/epmx9.geo", epmx9_geode, epmx9_geode_size);
    Put("SYSTEM/PRINTER/eplx9.geo", eplx9_geode, eplx9_geode_size);
    Put("SYSTEM/PRINTER/eprx9.geo", eprx9_geode, eprx9_geode_size);
    Put("SYSTEM/PRINTER/bchip9.geo", bchip9_geode, bchip9_geode_size);
    Put("SYSTEM/PRINTER/diconix9.geo", diconix9_geode, diconix9_geode_size);
    Put("SYSTEM/PRINTER/star9.geo", star9_geode, star9_geode_size);
    Put("SYSTEM/PRINTER/grpr9.geo", grpr9_geode, grpr9_geode_size);
    Put("SYSTEM/PRINTER/nec24.geo", nec24_geode, nec24_geode_size);
    Put("SYSTEM/PRINTER/epshi24.geo", epshi24_geode, epshi24_geode_size);
    Put("SYSTEM/PRINTER/epson48.geo", epson48_geode, epson48_geode_size);
    Put("SYSTEM/PRINTER/prop9.geo", prop9_geode, prop9_geode_size);
    Put("SYSTEM/PRINTER/propx24.geo", propx24_geode, propx24_geode_size);
    Put("SYSTEM/PRINTER/ppds24.geo", ppds24_geode, ppds24_geode_size);
    Put("SYSTEM/PRINTER/canon48.geo", canon48_geode, canon48_geode_size);
    Put("SYSTEM/PRINTER/oki9.geo", oki9_geode, oki9_geode_size);
    Put("SYSTEM/PRINTER/citoh9.geo", citoh9_geode, citoh9_geode_size);
    Put("SYSTEM/PRINTER/tosh24.geo", tosh24_geode, tosh24_geode_size);
    Put("SYSTEM/PRINTER/dj500c.geo", dj500c_geode, dj500c_geode_size);
    Put("SYSTEM/PRINTER/mercury.geo", mercury_geode, mercury_geode_size);
    Put("SYSTEM/PRINTER/canonbjc.geo", canonbjc_geode, canonbjc_geode_size);
    Put("SYSTEM/PRINTER/quietjet.geo", quietjet_geode, quietjet_geode_size);
    Put("SYSTEM/VIDEO", NULL, 0);	/* the bitmaps of the raster drivers */
    Put("SYSTEM/VIDEO/vidmem.geo", vidmem_geode, vidmem_geode_size);
    Put("USERDATA", NULL, 0);		/* a font for the text modes */
    Put("USERDATA/FONT", NULL, 0);
    Put("USERDATA/FONT/Berkeley.fnt", berkeley_fnt, berkeley_fnt_size);
    Put("USERDATA/FONT/TTF", NULL, 0);
    Put("USERDATA/FONT/TTF/cooper.ttf", cooper_ttf, cooper_ttf_size);
    Put("SYSTEM/FONT", NULL, 0);
    Put("SYSTEM/FONT/truetype.geo", truetype_geode, truetype_geode_size);
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
    /* the host's CUPS socket for GEOS_PRINT_QUEUE, before the stand-in
     * takes $CUPS_SERVER */
    if (getenv("CUPS_SERVER"))
	snprintf(hostCups, sizeof(hostCups), "%s", getenv("CUPS_SERVER"));
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

    /* "test_spool deskjet": the DeskJet driver only (driver.deskjet) */
    if (argc > 1 && !strcmp(argv[1], "deskjet")) {
	TestDeskjet(30, 1);
	TestDeskjet(33, 0);
	TestDeskjetText("out36.pcl");
	GeodeFreeLibrary(spool);
	printf("%s: %d failure(s)\n", fails ? "FAILED" : "OK", fails);
	return fails != 0;
    }
    /* "test_spool dotmatrix [driver]": the other Epson style drivers
     * (driver.dotmatrix) */
    if (argc > 1 && !strcmp(argv[1], "dotmatrix")) {
	static const NinePin np[] = {
	    { "Epmx9 File", "epmx9", "Epson MX-80 Type III",
	      { "K60L120", 216, 0, 0, 0, EP_ESCP, 0, 0, 0 }, 1, 1, 0 },
	    { "Eplx9 File", "eplx9", "Epson Apex 80", { "", 216, 0, 0, 0, EP_ESCP, 0, 0, 0 }, 1, 1, 0 },
	    { "Eprx9 File", "eprx9", "Epson RX-80",
	      { "K60L120Z240", 216, 0, 0, 0, EP_ESCP, 0, 0, 0 }, 0, 1, 0 },
	    { "Bchip9 File", "bchip9", "Blue Chip M120",
	      { "K80L160", 216, 0, 0, 0, EP_ESCP, 0, 0, 0 }, 1, 1, 0 },
	    { "Diconix9 File", "diconix9", "Diconix 150 (Epson Mode)",
	      { "K80L160", 288, 0, 0, 0, EP_ESCP, 0, 0, 0 }, 1, 1, 0 },
	    { "Star9 File", "star9", "Star Gemini-10x",
	      { "K60L120z240", 144, 0, 0, 0, EP_ESCP, 0, 0, 0 }, 1, 1, 0 },
	    /* no underline in the IBM Graphics fonts */
	    { "Grpr9 File", "grpr9", "IBM Graphics Printer",
	      { "K60L120Z240", 216, 1, 0, 0, EP_ESCP, 0, 0, 0 }, 1, 0, 0 },
	    { "Nec24 File", "nec24", "NEC P2200", { "", 360, 0, 1, 0, EP_ESCP, 0, 0, 0 }, 0, 1,
	      360 },
	    { "Epshi24 File", "epshi24", "Epson LQ-850",
	      { "", 360, 0, 1, 0, EP_ESCP, 0, 0, 0 }, 0, 1, 360 },
	    { "Epson48 File", "epson48", "Epson Stylus Color (Color)",
	      { "", 360, 0, 1, 0, EP_ESCP, 0, 0, 0 }, 0, 1, 360 },
	    { "Prop9 File", "prop9", "IBM Proprinter II",
	      { "K60L120Z240", 216, 1, 0, 0, EP_ESCP, 0, 0, 0 }, 1, 1, 0 },
	    { "Propx24 File", "propx24", "IBM Proprinter X24",
	      { "", 180, 1, 0, 0, EP_ESCP, 0, 0, 0 }, 1, 1, 360 },
	    { "Ppds24 File", "ppds24",
	      "IBM Personal Printer Series II 2390",
	      { "", 360, 1, 1, 120, EP_ESCP, 0, 0, 0 }, 0, 1, 360 },
	    { "Canon48 File", "canon48", "Canon BJ-130e",
	      { "", 360, 1, 0, 120, EP_ESCP, 0, 0, 0 }, 0, 1, 360 },
	    { "Oki9 File", "oki9", "Okidata ML 92",
	      { "", 144, 0, 0, 0, EP_OKI, 0, 0, 0 }, 1, 1, 144 },
	    { "Citoh9 File", "citoh9", "C.Itoh 8510",
	      { "", 144, 0, 0, 0, EP_CITOH, 0, 0, 0 }, 1, 1, 160 },
	    { "Tosh24 File", "tosh24", "Toshiba P321",
	      { "", 720, 0, 0, 0, EP_TOSHIBA, 5, 75, 76 }, 1, 1, 360 }
	};
	for (i = 0; i < (int) (sizeof(np) / sizeof(np[0])); i++)
	    if (argc < 3 || !strcmp(argv[2], np[i].driver))
		TestNinePin(&np[i], 100 + 4 * i);
	GeodeFreeLibrary(spool);
	printf("%s: %d failure(s)\n", fails ? "FAILED" : "OK", fails);
	return fails != 0;
    }
    /* "test_spool color dj500c|mercury|canonbjc": the color printers,
     * a raster plane a color (driver.dj500c, driver.mercury,
     * driver.canonbjc) */
    if (argc > 2 && !strcmp(argv[1], "color")) {
	if (!strcmp(argv[2], "dj500c")) {
	    TestPclRaster(200, 1, "DeskJet CMY File",
			  "HP DeskJet 500C (Color)", 3, -3, 0,
			  pclRes);
	    TestPclRaster(203, 0, "PaintJet File",
			  "HP PaintJet XL300 (3 Color)", 3, -3, 1,
			  pclRes);
	} else if (!strcmp(argv[2], "mercury"))
	    TestPclRaster(206, 1, "DeskJet KCMY File",
			  "HP DeskJet 550C (4 Color)", 4, -4, 0,
			  pclRes);
	else if (!strcmp(argv[2], "quietjet")) {
	    static const int qjRes[3] = { 96, 192, 192 };
	    TestPclRaster(220, 0, "QuietJet File", "HP QuietJet (HP Mode)",
			  1, 0, 0, qjRes);
	} else if (!strcmp(argv[2], "canonbjc")) {
	    TestBjc(210, "BJC File", "Canon BJC-1000 (B/W)", 1);
	    TestBjc(213, "BJC File", "Canon BJC-1000 (Color)", 3);
	    TestBjc(216, "BJC File", "Canon BJC-2000", 4);
	} else
	    CHECK(!"a color driver");
	GeodeFreeLibrary(spool);
	printf("%s: %d failure(s)\n", fails ? "FAILED" : "OK", fails);
	return fails != 0;
    }
    /* "test_spool pcl4": the PCL 4 driver only (driver.pcl4) */
    if (argc > 1 && !strcmp(argv[1], "pcl4")) {
	TestPcl4(40, PM_GRAPHICS_HI_RES, 0x100, 0xff, 1);
	TestPcl4(41, PM_GRAPHICS_HI_RES, 0x400, 0, 0);
	TestPcl4(42, PM_GRAPHICS_LOW_RES, 0x200, 1, 0);
	TestPcl4(43, PM_GRAPHICS_MED_RES, 0x000, 0xff, 1);
	TestPcl4Text("out44.pcl", "HP LaserJet III", 1);
	TestPcl4Text("out45.pcl", "HP LaserJet PLUS", 0);
	TestPcl4Custom("out46.pcl");
	GeodeFreeLibrary(spool);
	printf("%s: %d failure(s)\n", fails ? "FAILED" : "OK", fails);
	return fails != 0;
    }

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

    /* by hand only (ctest never sets it): the PostScript job to a queue
     * of the host's CUPS, such as a cups-pdf one; never to a real
     * printer by a test */
    if (getenv("GEOS_PRINT_QUEUE")) {
	if (hostCups[0])
	    setenv("CUPS_SERVER", hostCups, 1);
	else
	    unsetenv("CUPS_SERVER");
	InitFileWriteString("Host Queue", "driver", "pscript.geo");
	InitFileWriteString("Host Queue", "device", "PostScript Compatible");
	InitFileWriteString("Host Queue", "portDriver", "cups.geo");
	InitFileWriteString("Host Queue", "hostQueue",
			    getenv("GEOS_PRINT_QUEUE"));
	MakeSpoolFile(name1);
	FillJob(&jp, name1, "Host Queue", PPT_CUSTOM, NULL);
	CHECK(pSpoolAddJob(&jp) != 0xffff);
	CHECK(WaitIdle());
	/* and a page of text */
	{
	    GStateHandle gs;
	    FileHandle fh = pSpoolCreateSpoolFile(name1);
	    word ch;

	    CHECK(fh != 0);
	    gs = GrCreateGString(fh, GST_STREAM, &ch);
	    GrSetFont(gs, FID_PS_TIMES_ROMAN, (WWFixedAsDWord) 24 << 16);
	    GrSetTextStyle(gs, TS_BOLD, 0);
	    GrDrawText(gs, 72, 72, "PC/GEOS on Port32", 0);
	    GrSetTextStyle(gs, 0, TS_BOLD);
	    GrSetFont(gs, FID_PS_HELVETICA, (WWFixedAsDWord) 12 << 16);
	    GrDrawText(gs, 72, 120, "Printed through spool.geo, pscript.geo "
		       "and cups.geo.", 0);
	    GrSetTextStyle(gs, TS_ITALIC, 0);
	    GrDrawText(gs, 72, 140, "Helvetica Oblique", 0);
	    GrSetTextStyle(gs, TS_UNDERLINE, TS_ITALIC);
	    GrSetFont(gs, FID_PS_COURIER, (WWFixedAsDWord) 12 << 16);
	    GrDrawText(gs, 72, 160, "Courier, underlined: Gr\x9f" "\xa7" "e", 0);
	    GrSetTextStyle(gs, 0, TS_UNDERLINE);
	    GrSetTextColor(gs, CF_RGB, 0xcc, 0, 0);
	    GrSetFont(gs, FID_PS_TIMES_ROMAN, (WWFixedAsDWord) 14 << 16);
	    GrDrawText(gs, 72, 190, "Red Times", 0);
	    /* Cooper Black, which the printer lacks: a Type 3 font */
	    {
		FontID coop = GrCheckFontAvailName(FEF_OUTLINES, 0,
						   "Cooper* Black");
		if (coop != FID_INVALID) {
		    GrSetTextColor(gs, CF_RGB, 0, 0, 0);
		    GrSetFont(gs, coop, (WWFixedAsDWord) 24 << 16);
		    GrDrawText(gs, 200, 210, "Cooper Black", 0);
		}
	    }
	    /* a bitmap: 48 by 24, 24 bits, a color ramp (grey on
		 * this level 1 device) */
	    {
		static struct { Bitmap b; byte d[48 * 24 * 3]; } ramp;
		word bx, by;

		ramp.b.B_width = 48;
		ramp.b.B_height = 24;
		ramp.b.B_compact = BMC_UNCOMPACTED;
		ramp.b.B_type = BMF_24BIT;
		for (by = 0; by < 24; by++)
		    for (bx = 0; bx < 48; bx++) {
			byte *px = ramp.d + 3 * (by * 48 + bx);
			px[0] = (byte) (bx * 5);
			px[1] = (byte) (by * 10);
			px[2] = (byte) (255 - bx * 5);
		    }
		GrDrawBitmap(gs, 72, 210, &ramp.b, NULL);
	    }
	    GrNewPage(gs, PEC_FORM_FEED);
	    CHECK(GrEndGString(gs) == GSET_NO_ERROR);
	    GrDestroyGString(gs, 0, GSKT_LEAVE_DATA);
	    FileClose(fh, FALSE);
	    FillJob(&jp, name1, "Host Queue", PPT_CUSTOM, NULL);
	    jp.JP_numPages = 1;
	    CHECK(pSpoolAddJob(&jp) != 0xffff);
	    CHECK(WaitIdle());
	}
	printf("printed to the host queue %s\n", getenv("GEOS_PRINT_QUEUE"));
    }
#endif

    /* the raster drivers last: their jobs take a while, longer than the
     * CUPS stand-in waits */
    {
	static const int fx[3] = { 0, 1, 3 }, lq[3] = { 0, 39, 40 };

	TestEpson("Epson File", "Epson FX-85 (Epson Mode)", 10, 240, 216,
		  fx);
	TestEpson("Epson24 File", "Epson LQ-800", 20, 360, 180, lq);
    }
    TestEpsonText("Epson File", "Epson FX-85 (Epson Mode)", "out13.prn", 240,
		  PM_TEXT_DRAFT);
    TestEpsonText("Epson File", "Epson FX-85 (Epson Mode)", "out14.prn", 240,
		  PM_TEXT_NLQ);
    TestEpsonText("Epson24 File", "Epson LQ-800", "out23.prn", 360,
		  PM_TEXT_DRAFT);
    TestEpsonText("Epson24 File", "Epson LQ-800", "out24.prn", 360,
		  PM_TEXT_NLQ);

    GeodeFreeLibrary(spool);
    printf("%s: %d failure(s)\n", fails ? "FAILED" : "OK", fails);
    return fails != 0;
}
