/*
 * test_eps.c -- the PostScript translation library (eps.geo, the C port
 * of Library/Trans/Graphics/Vector/EPS/Export) in a booted kernel.
 *
 * As the PostScript printer driver drives it: the options block, the
 * header, a page with a graphics string, the end of the page and the
 * trailer, written to a stream whose strategy (here) collects them.
 * The output is checked for the parts each element makes; where
 * Ghostscript is on the host (GEOS_TEST_GS, or gs in the PATH on
 * linux), it must also run the file without an error.
 */
#define _GNU_SOURCE			/* popen */
#include "kernelC.h"
#include "ghal.h"
#include <geosdriver.h>
#include <streamC.h>
#include <library.h>
#include <lmem.h>
#include <file.h>
#include <xlatPS.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char eps_geode[];
extern const unsigned long eps_geode_size;

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

#define DR_STREAM_WRITE_FN 28		/* DR_STREAM_WRITE */

static char out[65536];
static size_t outLen;
static int writes, failWrites;

static Boolean Strategy(word fn, DriverRegs *r)
{
    if (fn != DR_STREAM_WRITE_FN || r->DR_bx != 0x1234 ||
	r->DR_ax != STREAM_BLOCK)
	return TRUE;
    writes++;
    if (failWrites) {
	r->DR_ax = STREAM_CLOSING;
	return TRUE;
    }
    if (outLen + r->DR_cx < sizeof(out)) {
	memcpy(out + outLen, r->DR_dsSi, r->DR_cx);
	outLen += r->DR_cx;
    }
    return FALSE;
}

/* where s is in the output, or -1 (no memmem in every C library) */
static size_t At(const char *s)
{
    size_t n = strlen(s), i;

    for (i = 0; i + n <= outLen; i++)
	if (!memcmp(out + i, s, n))
	    return i;
    return (size_t) -1;
}

static int Has(const char *s)
{
    return At(s) != (size_t) -1;
}

#define HAS(s) do { if (!Has(s)) { fails++; \
    printf("FAIL line %d: no \"%s\"\n", __LINE__, s); } } while (0)

/* ---------------------------------------------------------------------
 * The library
 * ------------------------------------------------------------------- */

typedef TransError LowFn(MemHandle, GStateHandle, MemHandle, GSControl,
			 GSRetType *);
typedef TransError OptStreamFn(MemHandle, MemHandle);
typedef TransError RawFn(MemHandle, const void *, word, MemHandle);
typedef TransError OptFn(MemHandle);
typedef MemHandle GetOptsFn(MemHandle);

/* the entry numbers of eps.gp (xlatPS.h) */
enum { E_LOW = TR_EXPORT_LOW, E_RAW = TR_EXPORT_RAW,
       E_HEADER = TR_EXPORT_HEADER, E_TRAILER = TR_EXPORT_TRAILER,
       E_BEGIN = TR_EXPORT_BEGIN_PAGE, E_END = TR_EXPORT_END_PAGE,
       E_NORM = TR_NORMALIZE_FILENAME, E_OPTS = TR_GET_EXPORT_OPTIONS };

static GeodeHandle lib;

static void *Entry(int n)
{
    return ProcGetLibraryEntry(lib, (word) n);
}

#define LOW	((LowFn *) Entry(E_LOW))
#define RAW	((RawFn *) Entry(E_RAW))
#define HEADER	((OptStreamFn *) Entry(E_HEADER))
#define TRAILER	((OptStreamFn *) Entry(E_TRAILER))
#define BEGIN	((OptStreamFn *) Entry(E_BEGIN))
#define END	((OptStreamFn *) Entry(E_END))
#define NORM	((OptFn *) Entry(E_NORM))
#define GETOPTS	((GetOptsFn *) Entry(E_OPTS))

static WWFixedAsDWord WWF(int i, word f)
{
    return ((dword) (word) i << 16) | f;
}

/* the drawing of the page */
static void Draw(GStateHandle gs)
{
    static const Point poly[] = { {10, 300}, {60, 340}, {110, 300},
				  {160, 340}, {210, 300}, {260, 340} };
    static const Point curve[] = { {300, 300}, {330, 380}, {370, 220},
				   {400, 300} };
    ThreePointArcParams a3;

    GrDrawLine(gs, 10, 10, 200, 10);
    GrSetLineColor(gs, CF_RGB, 0, 0, 0xaa);		/* EGA blue */
    GrSetLineWidth(gs, WWF(2, 0x8000));
    GrDrawRect(gs, 10, 20, 200, 80);
    GrSetLineWidth(gs, WWF(1, 0));
    GrSetLineColor(gs, CF_RGB, 0, 0, 0);
    GrSetAreaColor(gs, CF_RGB, 0xff, 0, 0);
    GrFillRect(gs, 220, 20, 300, 80);
    GrSetLineStyle(gs, LS_DASHED, 0, NULL, 0);
    GrDrawPolyline(gs, poly, 6);
    GrSetLineStyle(gs, LS_SOLID, 0, NULL, 0);
    GrDrawEllipse(gs, 10, 100, 110, 160);
    GrFillEllipse(gs, 120, 100, 220, 160);
    GrDrawRoundRect(gs, 230, 100, 330, 160, 12);
    GrDrawArc(gs, 10, 180, 110, 260, 0, 90, ACT_OPEN);
    GrFillArc(gs, 120, 180, 220, 260, 45, 270, ACT_PIE);
    a3.TPAP_close = ACT_CHORD;
    a3.TPAP_point1.PF_x.WWF_int = 250; a3.TPAP_point1.PF_x.WWF_frac = 0;
    a3.TPAP_point1.PF_y.WWF_int = 220; a3.TPAP_point1.PF_y.WWF_frac = 0;
    a3.TPAP_point2.PF_x.WWF_int = 290; a3.TPAP_point2.PF_x.WWF_frac = 0;
    a3.TPAP_point2.PF_y.WWF_int = 180; a3.TPAP_point2.PF_y.WWF_frac = 0;
    a3.TPAP_point3.PF_x.WWF_int = 330; a3.TPAP_point3.PF_x.WWF_frac = 0;
    a3.TPAP_point3.PF_y.WWF_int = 220; a3.TPAP_point3.PF_y.WWF_frac = 0;
    GrDrawArc3Point(gs, &a3);
    GrDrawCurve(gs, curve);
    GrBeginPath(gs, PCT_REPLACE);
    GrDrawRect(gs, 420, 20, 500, 100);
    GrDrawEllipse(gs, 440, 40, 520, 120);
    GrEndPath(gs);
    GrSetAreaColor(gs, CF_RGB, 0, 0xaa, 0);
    GrFillPath(gs, ODD_EVEN);
    GrDrawPath(gs);
    GrSetAreaMaskSys(gs, SDM_50);			/* not solid */
    GrSetAreaColor(gs, CF_RGB, 0, 0, 0);
    GrFillRect(gs, 420, 140, 500, 200);
    GrSetAreaMaskSys(gs, SDM_0);			/* nothing */
    GrFillRect(gs, 420, 220, 500, 260);
    GrSetAreaMaskSys(gs, SDM_100);
    GrApplyScale(gs, WWF(1, 0x8000), WWF(1, 0x8000));
    GrDrawLine(gs, 10, 400, 100, 400);
    GrNewPage(gs, PEC_FORM_FEED);
    GrDrawLine(gs, 0, 0, 50, 50);			/* page 2 */
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
    GeodeLoadError e;
    MemHandle opts, stream, blk;
    PSExportBlock *o;
    EPSExportLowStreamStruct *s;
    GStateHandle gs;
    GSRetType ret;
    word ch = 0;
    char *slash, file[300];
    FILE *f;
    int r;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    strncpy(top, argv[0], sizeof(top) - 8);
    top[sizeof(top) - 8] = 0;
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash) {
	sep = *slash;
	strcpy(slash + 1, "GEPSTOP");
    } else
	strcpy(top, "GEPSTOP");
    ghal_fs_mkdir(top);
    Put("SYSTEM", NULL, 0);
    Put("SYSTEM/eps.geo", eps_geode, eps_geode_size);

    CHECK(KernelBoot(top, "", &e) == 0 && e == GLE_OK);
    FileSetStandardPath(SP_SYSTEM);
    lib = GeodeUseLibrary("eps.geo", 0, 0, &e);
    CHECK(lib != 0);
    if (!lib) {
	printf("eps.geo: GeodeLoadError %d\n", (int) e);
	return 1;
    }
    /* the numbers of eps.gp: what is not ported is an empty entry */
    CHECK(Entry(0) == NULL && Entry(TR_EXPORT_BITMAP) == NULL);
    CHECK(Entry(TR_EXPORT_LOW) != NULL && Entry(E_OPTS) != NULL);

    /* the defaults */
    opts = GETOPTS(0);
    CHECK(opts != 0 && GETOPTS(opts) == 0);
    o = MemLock(opts);
    CHECK(o->PSEB_public.PSEO_common.GEO_docH == 792 &&
	  o->PSEB_public.PSEO_common.GEO_docW == 612 &&
	  o->PSEB_public.PSEO_flags == PSEF_EPS_FILE &&
	  o->PSEB_curPage == 1 &&
	  !strcmp(o->PSEB_public.PSEO_common.GEO_appName, "PC/GEOS"));

    /* file names */
    strcpy(o->PSEB_public.PSEO_common.GEO_fileName, "picture");
    MemUnlock(opts);
    CHECK(NORM(opts) == TE_NO_ERROR);
    o = MemLock(opts);
    CHECK(!strcmp(o->PSEB_public.PSEO_common.GEO_fileName, "picture.EPS"));
    strcpy(o->PSEB_public.PSEO_common.GEO_fileName, "x.eps");
    o->PSEB_public.PSEO_flags = 0;
    MemUnlock(opts);
    CHECK(NORM(opts) == TE_NO_ERROR);
    o = MemLock(opts);
    CHECK(!strcmp(o->PSEB_public.PSEO_common.GEO_fileName, "x.PS"));
    o->PSEB_public.PSEO_flags = PSEF_EPS_FILE | PSEF_INCLUDE_PREVIEW;
    MemUnlock(opts);
    CHECK(NORM(opts) == TE_NO_ERROR);
    o = MemLock(opts);
    CHECK(!strcmp(o->PSEB_public.PSEO_common.GEO_fileName, "x.EPI"));

    /* a print file of two pages, two copies, level 1 */
    o->PSEB_public.PSEO_flags = 0;
    o->PSEB_public.PSEO_level = 1;
    o->PSEB_public.PSEO_common.GEO_pages = 2;
    o->PSEB_public.PSEO_common.GEO_copies = 2;
    strcpy(o->PSEB_public.PSEO_common.GEO_appName, "Port32 Test");
    strcpy(o->PSEB_public.PSEO_common.GEO_docName, "Shapes");
    MemUnlock(opts);

    stream = MemAlloc(sizeof(EPSExportLowStreamStruct), HF_DYNAMIC,
		      HAF_ZERO_INIT);
    s = MemLock(stream);
    s->ESPELSS_token = 0x1234;
    s->ESPELSS_strategy = Strategy;
    MemUnlock(stream);

    /* the header */
    CHECK(HEADER(opts, 0) == TE_EXPORT_ERROR);
    CHECK(HEADER(opts, stream) == TE_NO_ERROR);
    CHECK(outLen > 1000 && !memcmp(out, "%!PS-Adobe-3.0\r\n", 16));
    HAS("%%BoundingBox: 0 0 612 792\r\n");
    HAS("%%Creator: Port32 Test\r\n");
    HAS("%%CreationDate: ");
    HAS("%%Pages: 2\r\n");
    HAS("%%PageOrder: Ascend");
    HAS("%%Requirements: numcopies(2)\r\n");
    HAS("%%Title: Shapes\r\n");
    HAS("%%EndComments\r\n");
    HAS("%%BeginProlog");
    CHECK(!Has("%%LanguageLevel"));
    o = MemLock(opts);
    CHECK(o->PSEB_status & PSES_OPEN_PROLOG);
    MemUnlock(opts);

    /* the page */
    CHECK(BEGIN(opts, stream) == TE_NO_ERROR);
    HAS("%%EndProlog");
    HAS("/#copies 2 def");
    HAS("%%Page: 1 1\r\n");
    HAS("%%BeginPageSetup");
    o = MemLock(opts);
    CHECK(!(o->PSEB_status & PSES_OPEN_PROLOG) &&
	  (o->PSEB_status & PSES_OPEN_PAGE_SETUP));
    MemUnlock(opts);
    CHECK(RAW(opts, "% raw\r\n", 7, stream) == TE_NO_ERROR);
    HAS("% raw\r\n");

    blk = MemAllocLMem(LMEM_TYPE_GENERAL, 0);
    gs = GrCreateGString(blk, GST_CHUNK, &ch);
    Draw(gs);
    CHECK(GrEndGString(gs) == GSET_NO_ERROR);
    GrDestroyGString(gs, 0, GSKT_LEAVE_DATA);
    gs = GrLoadGString(blk, GST_CHUNK, ch);

    r = (int) outLen;
    CHECK(LOW(opts, gs, stream, 0, &ret) == TE_NO_ERROR &&
	  ret == GSRT_NEW_PAGE);
    HAS("%%EndPageSetup");
    CHECK(At("%%EndPageSetup") > (size_t) r);
    HAS("SO\r\n");
    HAS("[1 0 0 1 0 0] SOT\r\n");
    HAS("200 10 10 10  DL");		/* x2 y2 x1 y1 */
    HAS("2.5 LW 9.9994 LM ");		/* 1 / 0x199a: GEOS's 10 */
    HAS("0 154 255 SGC");		/* EGA blue as printed */
    HAS("200 80 10 20  DR");
    HAS("255 0 0 SGC");
    HAS("300 80 220 20  FR");
    HAS("[ 4 ] 0 LD");			/* dashed */
    HAS("5 DPL");
    HAS("110 10 160 100  DE");		/* x2 x1 y2 y1 */
    HAS("220 120 160 100  FE");
    HAS("38 18 12 280 130  DRR");	/* dx dy r cx cy */
    HAS("0 90 110 10 260 180  DAO");
    HAS("45 270 220 120 260 180  FAP");
    HAS("180 0 330 250 260 180  DAC");	/* the three point arc */
    HAS("400 300 330 380 370 220 300 300  DC");
    HAS("SPM newpath 500 100 420 20  CRP closepath\r\n"
	"520 440 120 40  CEP closepath\r\n FPaO");
    HAS(" RM stroke");
    HAS("[(");				/* the 50% mask */
    HAS("clip");
    HAS("[1.5 0 0 1.5 0 0] SOT");
    HAS("EO\r\n");
    CHECK(!Has("500 260 420 220"));	/* SDM_0: nothing */

    CHECK(END(opts, stream) == TE_NO_ERROR);
    HAS("EP\r\n");
    HAS("%%PageTrailer");
    o = MemLock(opts);
    CHECK(o->PSEB_curPage == 2);
    MemUnlock(opts);

    /* page 2: the rest of the string */
    CHECK(BEGIN(opts, stream) == TE_NO_ERROR);
    HAS("%%Page: 2 2\r\n");
    CHECK(LOW(opts, gs, stream, 0, &ret) == TE_NO_ERROR &&
	  ret == GSRT_COMPLETE);
    HAS("50 50 0 0  DL");
    CHECK(END(opts, stream) == TE_NO_ERROR);
    CHECK(TRAILER(opts, stream) == TE_NO_ERROR);
    CHECK(outLen > 10 && !memcmp(out + outLen - 12, "%%Trailer\r\n\x04", 12));

    /* write errors come back */
    failWrites = 1;
    CHECK(RAW(opts, "x", 1, stream) == TE_EXPORT_ERROR);
    CHECK(END(opts, stream) == TE_EXPORT_ERROR);
    failWrites = 0;

    GrDestroyGString(gs, 0, GSKT_LEAVE_DATA);
    MemFree(blk);

    /* the file, without the ^D of the print trailer, for Ghostscript */
    snprintf(file, sizeof(file), "%s%cout.ps", top, sep);
    f = fopen(file, "wb");
    CHECK(f != NULL);
    if (f) {
	fwrite(out, 1, outLen - 1, f);
	fclose(f);
	r = Ghostscript(file);
	if (r < 0)
	    printf("no Ghostscript: not run\n");
	else
	    CHECK(r == 1);
    }

    MemFree(stream);
    MemFree(opts);
    GeodeFreeLibrary(lib);
    printf("%s (%d failures), %lu bytes of PostScript\n",
	   fails ? "FAILED" : "passed", fails, (unsigned long) outLen);
    return fails ? 1 : 0;
}
