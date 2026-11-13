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
#include <font.h>
#include <fontID.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char berkeley_fnt[];
extern const unsigned long berkeley_fnt_size;
extern const unsigned char truetype_geode[];
extern const unsigned long truetype_geode_size;
extern const unsigned char cooper_ttf[];
extern const unsigned long cooper_ttf_size;
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
static size_t AtN(const char *s, size_t n)
{
    size_t i;

    for (i = 0; i + n <= outLen; i++)
	if (!memcmp(out + i, s, n))
	    return i;
    return (size_t) -1;
}

static size_t AtFrom(const char *s, size_t from)
{
    size_t n = strlen(s), i;

    for (i = from; i + n <= outLen; i++)
	if (!memcmp(out + i, s, n))
	    return i;
    return (size_t) -1;
}

static size_t At(const char *s)
{
    return AtN(s, strlen(s));
}

static int Has(const char *s)
{
    return At(s) != (size_t) -1;
}

#define HAS(s) do { if (!Has(s)) { fails++; \
    printf("FAIL line %d: no \"%s\"\n", __LINE__, s); } } while (0)
/* bytes with a 0 among them: a string literal */
#define HASB(s) do { if (AtN(s, sizeof(s) - 1) == (size_t) -1) { fails++; \
    printf("FAIL line %d: no bytes of \"%s\"\n", __LINE__, s); } } \
    while (0)

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

/* GrDrawTextField: "Hi " in Helvetica, then "Field" red, underlined, in
 * Times bold */
static word FieldStyle(GDF_vars *v, TextAttr *ta, word offset,
		       Boolean first, const char **text)
{
    static const char field[] = "Hi Field";
    Boolean second = (Boolean) (offset >= 3);

    (void) v;
    (void) first;
    memset(ta, 0, sizeof(*ta));
    ta->TA_color.CQ_info = CF_RGB;
    ta->TA_color.CQ_redOrIndex = (byte) (second ? 0xff : 0);
    ta->TA_mask = SDM_100;
    ta->TA_styleSet = (TextStyle) (second ? TS_UNDERLINE | TS_BOLD : 0);
    ta->TA_styleClear = (TextStyle) (second ? 0 : TS_UNDERLINE | TS_BOLD);
    ta->TA_modeClear = TM_DRAW_BASE;
    ta->TA_font = second ? FID_PS_TIMES_ROMAN : FID_PS_HELVETICA;
    ta->TA_size.WBF_int = 12;
    ta->TA_fontWeight = FW_NORMAL;
    ta->TA_fontWidth = FWI_MEDIUM;
    *text = field + offset;
    return (word) (second ? 5 : 3);
}

static FontID ttFont = FID_INVALID;	/* Cooper Black */

/* the text of the page */
static void DrawText(GStateHandle gs)
{
    GDF_vars v;

    GrDrawText(gs, 10, 450, "Hello (World)", 0);	/* Berkeley */
    GrSetFont(gs, FID_PS_HELVETICA, WWF(14, 0));
    GrSetTextStyle(gs, TS_BOLD, 0);
    GrDrawText(gs, 10, 480, "Bold", 0);
    /* Zapf Chancery has only an italic: the bold is done by a matrix */
    GrSetFont(gs, FID_PS_ZAPF_CHANCERY, WWF(14, 0));
    GrDrawText(gs, 100, 480, "Chancery", 0);
    GrSetFont(gs, FID_PS_COURIER, WWF(12, 0));
    GrSetTextStyle(gs, TS_UNDERLINE, TS_BOLD);
    GrDrawText(gs, 10, 510, "K\x8ase", 0);		/* "Kase" with a-umlaut */
    GrSetTextStyle(gs, 0, TS_UNDERLINE);
    /* Cooper Black: no printer has it, GEOS has its outlines (TrueType):
     * a Type 3 font, its characters as they are needed */
    {
	FontID coop = GrCheckFontAvailName(FEF_OUTLINES, 0, "Cooper* Black");
	if (coop != FID_INVALID) {
	    GrSetFont(gs, coop, WWF(20, 0));
	    GrDrawText(gs, 200, 450, "Cooper", 0);
	    GrDrawText(gs, 200, 480, "Cop!", 0);	/* only the ! is new */
	    GrSetTextStyle(gs, TS_ITALIC, 0);	/* the driver's shear */
	    GrDrawText(gs, 300, 450, "Cop", 0);
	    GrSetTextStyle(gs, 0, TS_ITALIC);
	    GrSetFont(gs, FID_PS_COURIER, WWF(12, 0));
	}
	ttFont = coop;
    }
    memset(&v, 0, sizeof(v));
    v.GDFV_saved.GDFS_nChars = 8;
    v.GDFV_saved.GDFS_drawPos.PWBF_x.WBF_int = 10;
    v.GDFV_saved.GDFS_drawPos.PWBF_y.WBF_int = 530;
    v.GDFV_saved.GDFS_limit = 0xffff;
    v.GDFV_saved.GDFS_flags = HF_AUTO_HYPHEN;
    v.GDFV_styleCallback = FieldStyle;
    GrDrawTextField(gs, &v);
}

/* a complex bitmap and its slice */
typedef struct {
    CBitmap c;
    byte    d[6];
} TestSlice;
static Bitmap *nextSlice;

static Bitmap *NextSlice(Bitmap *bm)
{
    Bitmap *n = nextSlice;

    (void) bm;
    nextSlice = NULL;
    return n;
}

/* the bitmaps of a page: gray on page 1 (level 1), color on page 2 */
static void DrawBitmaps(GStateHandle gs)
{
    /* 12 by 3 monochrome, the bits past 12 set; a 0x1b goes as hex */
    static const struct { Bitmap b; byte d[6]; } mono = {
	{ 12, 3, BMC_UNCOMPACTED, BMF_MONO },
	{ 0xaa, 0xff, 0x1b, 0x00, 0x55, 0x0f }
    };
    /* 8 by 2 with a mask, PackBits: mask f0 data ff, then 0f twice */
    static const struct { Bitmap b; byte d[5]; } masked = {
	{ 8, 2, BMC_PACKBITS, BMF_MONO | BMT_MASK },
	{ 0x01, 0xf0, 0xff, 0xff, 0x0f }
    };
    /* 4 by 2, 4 bits, the default palette: black, white, white, black */
    static const struct { Bitmap b; byte d[4]; } four = {
	{ 4, 2, BMC_UNCOMPACTED, BMF_4BIT },
	{ 0x0f, 0xf0, 0x0f, 0xf0 }
    };
    static TestSlice s1, s2;

    GrSetAreaColor(gs, CF_RGB, 0, 0, 0);
    GrDrawBitmap(gs, 300, 420, &mono.b, NULL);
    GrSetAreaColor(gs, CF_RGB, 0, 0, 0xff);
    GrFillBitmap(gs, 340, 420, &masked.b, NULL);
    GrDrawBitmap(gs, 360, 420, &four.b, NULL);
    /* 2 by 2, 24 bits at 144 dpi, in two slices: red green, blue white */
    memset(&s1, 0, sizeof(s1));
    s1.c.CB_simple.B_width = 2;
    s1.c.CB_simple.B_height = 2;
    s1.c.CB_simple.B_compact = BMC_UNCOMPACTED;
    s1.c.CB_simple.B_type = BMF_24BIT | BMT_COMPLEX;
    s1.c.CB_numScans = 1;
    s1.c.CB_data = sizeof(CBitmap);
    s1.c.CB_xres = s1.c.CB_yres = 144;
    s1.d[0] = 0xff; s1.d[4] = 0xff;
    s2 = s1;
    s2.c.CB_startScan = 1;
    memset(s2.d, 0xff, sizeof(s2.d));
    s2.d[0] = s2.d[1] = 0;
    nextSlice = &s2.c.CB_simple;
    GrDrawBitmap(gs, 380, 420, &s1.c.CB_simple, NextSlice);
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
    DrawText(gs);
    DrawBitmaps(gs);
    GrApplyScale(gs, WWF(1, 0x8000), WWF(1, 0x8000));
    GrDrawLine(gs, 10, 400, 100, 400);
    GrNewPage(gs, PEC_FORM_FEED);
    GrDrawLine(gs, 0, 0, 50, 50);			/* page 2 */
    DrawBitmaps(gs);
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
    Put("USERDATA", NULL, 0);
    Put("USERDATA/FONT", NULL, 0);
    Put("USERDATA/FONT/Berkeley.fnt", berkeley_fnt, berkeley_fnt_size);
    Put("USERDATA/FONT/TTF", NULL, 0);
    Put("USERDATA/FONT/TTF/cooper.ttf", cooper_ttf, cooper_ttf_size);
    Put("SYSTEM/FONT", NULL, 0);
    Put("SYSTEM/FONT/truetype.geo", truetype_geode, truetype_geode_size);

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
    o->PSEB_public.PSEO_fonts = PSFL_STANDARD_35N;	/* Zapf Chancery */
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

    /* text: a style group of style runs each, the fonts as the printer
     * has them */
    HAS("[(Hello \\(World\\)) \r\n");
    HAS("/Times-Roman true 10 [1 0 0 1 0 0] ] ] ");	/* Berkeley 10 */
    HAS("/Helvetica-Bold true 14 [1 0 0 1 0 0] ] ] ");
    HAS("/ZapfChancery-MediumItalic true 14 [1.125 0 0 1 0 0] ] ] ");
    HAS("[(K\\212se) \r\n");
    HAS("/Courier true 12 ");
    HAS(" UL}");			/* underlined */
    HAS(" 10 460 DSG\r\n");		/* its top at 450 */
    HAS("[(Hi ) \r\n");		/* the field */
    HAS("[(Field-) \r\n");		/* its hyphen */
    HAS("{255 0 0 SGC ");
    HAS("/Times-Bold true 12 ");
    HAS(" DSG\r\n");

    /* Cooper Black as a Type 3 font: defined before its first text,
     * its characters added as they come, each once */
    CHECK(ttFont != FID_INVALID);
    if (ttFont != FID_INVALID) {
	char name[16];
	size_t def, first, add2;

	snprintf(name, sizeof(name), "/X%04X00", (unsigned) ttFont);
	HAS("12 dict begin\r\n/FontType 3 def\r\n");
	HAS("/FontMatrix [0.001 0 0 0.001 0 0] def\r\n");
	{
	    char s[64];
	    snprintf(s, sizeof(s), "%s exch definefont pop\r\n", name);
	    def = At(s);
	    CHECK(def != (size_t) -1);
	    snprintf(s, sizeof(s), "%s findfont /CharProcs get begin\r\n"
		     "/c45 <", name);			/* the hyphen first */
	    first = At(s);
	    CHECK(first != (size_t) -1 && first > def);
	    snprintf(s, sizeof(s), "%s true 20 ", name);
	    CHECK(!Has(s));
	    snprintf(s, sizeof(s), "%s false 20 [1 0 0 1 0 0] ] ] ", name);
	    HAS(s);
	}
	{
	    char s[64];
	    snprintf(s, sizeof(s), "/X%04X10 exch definefont pop\r\n",
		     (unsigned) ttFont);
	    HAS(s);				/* italic: a font of its own */
	}
	HAS("/FontMatrix [0.001 0 0.000");	/* sheared */
	HAS("/c67 <");				/* C */
	HAS("/c111 <");				/* o */
	/* each character once in the font; the italic font (defined
	 * after) has its own */
	{
	    char s[64];
	    size_t it;
	    snprintf(s, sizeof(s), "/X%04X10 exch definefont pop",
		     (unsigned) ttFont);
	    it = At(s);
	    CHECK(it != (size_t) -1 && it > first);
	    CHECK(AtFrom("/c111 <", At("/c111 <") + 1) > it);
	    add2 = At("/c33 <");		/* ! */
	    CHECK(add2 != (size_t) -1 && add2 > first && add2 < it);
	    CHECK(AtFrom("/c67 <", add2) > it);	/* C not again */
	}
    }

    /* bitmaps on a level 1 printer: monochrome in packets, binary or
     * hex, the bits past the width cleared; the colors as grey */
    HASB("16 3 2 16 3 300 420 DMB\r\n" "B\xaa\xf0\r\n" "H1B00\r\n"
	"BU\0\r\n");
    HAS("8 2 1 8 2 340 420 DMB\r\n" "B\xf0\r\n" "B\x0f\r\n");
    HAS("4 2 2 4 2 360 420 DGB\r\n" "0FF0\r\n" "0FF0\r\n");
    /* 144 dpi: 2 * 72 / 144 divided first, as the asm */
    HAS("2 2 2 0.9998 0.9998 380 420 DXB\r\n" "5F80\r\n" "20FF\r\n");

    CHECK(END(opts, stream) == TE_NO_ERROR);
    HAS("EP\r\n");
    HAS("%%PageTrailer");
    o = MemLock(opts);
    CHECK(o->PSEB_curPage == 2);
    MemUnlock(opts);

    /* page 2: the rest of the string, level 2 */
    o = MemLock(opts);
    o->PSEB_public.PSEO_level = 2;
    MemUnlock(opts);
    CHECK(BEGIN(opts, stream) == TE_NO_ERROR);
    HAS("%%Page: 2 2\r\n");
    CHECK(LOW(opts, gs, stream, 0, &ret) == TE_NO_ERROR &&
	  ret == GSRT_COMPLETE);
    HAS("50 50 0 0  DL");
    /* level 2: colors as PackBits runs of red, green and blue */
    HAS("4 2 4 4 2 360 420 DCB\r\n"
	"0000FFFF00000000FFFF00000000FFFF0000\r\n");
    HAS("2 2 2 0.9998 0.9998 380 420 DCB\r\n" "01FF000100FFFF00\r\n"
	"0100FF0100FFFFFF\r\n");
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
