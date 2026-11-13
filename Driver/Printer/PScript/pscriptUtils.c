/*
 * pscriptUtils.c -- PostScript support routines of the driver (the C
 * port of pscriptUtils.asm): the page transform and the paper size as
 * PostScript, the numbers as ASCII, and the call of the translation
 * library.
 *
 * The number conversion is this driver's own (its fraction tables end
 * in 6553, 655 and 65, which the translation library's do not).
 */
#include "pscript.h"

#include <system.h>

#define FRAC_MINIMUM	3	/* less than this is .0 */
#define FRAC_MAXIMUM	65530	/* this or more rounds to 1.0 */

/* CallEPSLibrary: the routine of eps.geo with the entry number of
 * TransRoutine; NULL if the library is not loaded or has no such entry */
void *CallEPSLibrary(PState *ps, word routine)
{
    if (!ps->PS_epsLibrary)
	return NULL;
    return ProcGetLibraryEntry(ps->PS_epsLibrary, routine);
}

TransError EPSRaw(PState *ps, MemHandle options, const void *data,
		  word size, MemHandle stream)
{
    EPSExportRawFn *f = (EPSExportRawFn *) CallEPSLibrary(ps,
							  TR_EXPORT_RAW);
    return f ? f(options, data, size, stream) : TE_EXPORT_ERROR;
}

/* UWordToAscii: no leading zeroes */
char *UWordToAscii(char *p, word v)
{
    char d[5];
    int n = 0;

    do {
	d[n++] = (char) ('0' + v % 10);
	v /= 10;
    } while (v);
    while (n)
	*p++ = d[--n];
    return p;
}

/* the fraction tables, read on as one array (as the asm's cs:[si]) */
static const word fracTable[41] = {
    6553, 13107, 19660, 26214, 32768, 39321, 45872, 52428, 58982, 65535,
     655,  1310,  1966,  2621,  3276,  3932,  4587,  5242,  5898,  6553,
      65,   131,   196,   262,   327,   393,   458,   524,   589,   655,
       6,    13,    19,    26,    32,    39,    45,    52,    58,    65,
    0xffff
};

/* ConvertFraction: at most four digits, rounded at the last */
static char *ConvertFraction(char *p, word ax)
{
    int row, i;

    if (ax < FRAC_MINIMUM)
	return p;
    *p = '.';
    for (row = 0; row < 40; row += 10) {
	p++;
	if (ax < FRAC_MINIMUM)
	    return p;
	*p = '0';
	if (ax >= fracTable[row]) {
	    i = row;
	    do {
		(*p)++;
		i++;
	    } while (ax >= fracTable[i]);
	    ax -= fracTable[i - 1];
	}
    }
    p++;
    if (p[-1] <= '9') {
	if (ax < FRAC_MINIMUM)
	    return p;
	if (++p[-1] <= '9')
	    return p;
    }
    p[-1] = '0';				/* roundIt */
    if (++p[-2] <= '9')
	return p;
    p[-2] = '0';
    if (++p[-3] <= '9')
	return p;
    p[-3] = '0';
    ++p[-4];
    return p;
}

static char *WWFixedToAscii(char *p, const WWFixed *w)
{
    sdword v = (sdword) (((dword) (word) w->WWF_int << 16) | w->WWF_frac);
    word i, f;

    if (v < 0) {
	*p++ = '-';
	v = -v;
    }
    i = (word) ((dword) v >> 16);
    f = (word) v;
    if (f >= FRAC_MAXIMUM) {
	f = 0;
	i++;
    }
    p = UWordToAscii(p, i);
    return ConvertFraction(p, f);
}

static char *DWFixedToAscii(char *p, const DWFixed *v)
{
    int64_t x = ((int64_t) v->DWF_int << 16) | v->DWF_frac;
    dword i;
    word f;

    if (x < 0) {
	*p++ = '-';
	x = -x;
    }
    i = (dword) (x >> 16);
    f = (word) x;
    if (f >= FRAC_MAXIMUM) {
	f = 0;
	i++;
    }
    p += UtilHex32ToAscii(p, i, 0);
    return ConvertFraction(p, f);
}

/* MatrixToAscii: "[e11 e12 e21 e22 e31 e32]" */
static char *MatrixToAscii(char *p, const TransMatrix *tm)
{
    *p++ = '[';
    p = WWFixedToAscii(p, &tm->TM_e11);
    *p++ = ' ';
    p = WWFixedToAscii(p, &tm->TM_e12);
    *p++ = ' ';
    p = WWFixedToAscii(p, &tm->TM_e21);
    *p++ = ' ';
    p = WWFixedToAscii(p, &tm->TM_e22);
    *p++ = ' ';
    p = DWFixedToAscii(p, &tm->TM_e31);
    *p++ = ' ';
    p = DWFixedToAscii(p, &tm->TM_e32);
    *p++ = ']';
    return p;
}

static char *Append(char *p, const char *s)
{
    while (*s)
	*p++ = *s++;
    return p;
}

/* EmitTransform: "[matrix] SPT" */
Boolean EmitTransform(PState *ps, MemHandle options, MemHandle stream,
		      const TransMatrix *tm)
{
    char buf[100], *p;

    p = MatrixToAscii(buf, tm);
    p = Append(p, " SPT\r\n");
    return EPSRaw(ps, options, buf, (word) (p - buf), stream) !=
	TE_NO_ERROR;
}

/* EmitPaperSize: "width height SPS" */
Boolean EmitPaperSize(PState *ps, MemHandle options, MemHandle stream)
{
    char buf[60], *p;

    p = UWordToAscii(buf, ps->PS_customWidth);
    *p++ = ' ';
    p = UWordToAscii(p, ps->PS_customHeight);
    p = Append(p, " SPS\r\n");
    return EPSRaw(ps, options, buf, (word) (p - buf), stream) !=
	TE_NO_ERROR;
}

/* the stream block of the job: GEO_hFile of the options block */
MemHandle PSStreamBlock(PState *ps)
{
    PSExportBlock *o;
    MemHandle h;

    if (!ps->PS_expansionInfo)
	return 0;
    o = MemLock(ps->PS_expansionInfo);
    h = (MemHandle) o->PSEB_public.PSEO_common.GEO_hFile;
    MemUnlock(ps->PS_expansionInfo);
    return h;
}
