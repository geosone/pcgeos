/*
 * libDriver.c -- routines for the text modes of printer drivers (the C
 * port of libDriver.asm): the printer font closest to a requested one,
 * and the character translation table of the PState.
 *
 * The symbol set tables (the upper 128 characters of the GEOS character
 * set in each PrinterSymbolSet) come from lib*Tab.asm at build time
 * (Port32/tools/symtabgen.py).  The Japanese build's mapping of the
 * Kanji fonts (PZ_PCGEOS) is not ported.
 */
#include "spoolLocal.h"
#include <Internal/spoolDr.h>

extern const byte ibm437Table[128], ibm850Table[128], ibm860Table[128],
    ibm863Table[128], ibm865Table[128], romanTable[128], windowsTable[128],
    venturaTable[128], latinTable[128];

/* by PrinterSymbolSet / 2; NULL: PSS_ASCII7 */
static const byte *const symbolTables[] = {
    NULL, ibm437Table, ibm850Table, ibm860Table, ibm863Table, ibm865Table,
    romanTable, windowsTable, venturaTable, latinTable
};


void SpoolMapToPrinterFont(const PrinterInfo *pi, FontID *fontID,
			   word *pointSize, byte *pitch)
{
    const byte *base = (const byte *) pi;
    const FontGeometry *fg, *g, *prev;
    const byte *p;
    FontID id = *fontID;
    word size = *pointSize;
    byte bl = *pitch, bh;

    fg = (const FontGeometry *) (base + pi->PI_fontGeometries);
    for (g = fg; g->FG_fontID != FID_INVALID && g->FG_fontID != id; g++)
	;
    if (g->FG_fontID == FID_INVALID) {
	g = fg;				/* not there: the first font */
	id = g->FG_fontID;
    }
    /* the size: the exact one, else the largest smaller one, else the
     * smallest */
    prev = g;
    while (size > g->FG_pointSize) {
	prev = g;
	if (g[1].FG_fontID != id)
	    break;
	g++;
    }
    if (size != g->FG_pointSize)
	g = prev;
    *fontID = id;
    *pointSize = g->FG_pointSize;
    if (!bl) {
	/* proportional: a font smaller than asked for becomes fixed, at
	 * the pitch of its size */
	if (size >= g->FG_pointSize) {
	    *pitch = 0;
	    return;
	}
	bl = g->FG_pointSize ? (byte) (1440 / g->FG_pointSize > 255 ? 255 :
				       1440 / g->FG_pointSize) : 255;
    }
    /* the pitch: the smallest the font has that is not below it */
    p = base + g->FG_pitchTab;
    bh = *p;
    while (bl <= *p) {
	bh = *p;
	p++;
    }
    *pitch = bh;
}

void SpoolUpdateTranslationTable(PState *ps, const SpoolTransTables *t)
{
    PrinterSymbolSet set = ps->PS_curFont.FE_symbolSet;
    PrintDriverUIData *ui =
	(PrintDriverUIData *) PS_JOB_PARAMS(ps)->JP_printerData;
    const word *w;
    word i, n;

    /* the lower half (all of it for PC/GEOS) as it is */
    n = set == PSS_PCGEOS ? 256 : 128;
    for (i = 0; i < n; i++)
	ps->PS_asciiTrans[i] = (byte) i;
    if (set != PSS_PCGEOS) {
	const byte *tab = (set >> 1) < sizeof(symbolTables) /
	    sizeof(symbolTables[0]) ? symbolTables[set >> 1] : NULL;

	for (i = 0; i < 128; i++)
	    ps->PS_asciiTrans[128 + i] = tab ? tab[i] : C_SPACE;
	/* the national characters of the printer's country */
	if (ui->PUID_countryCode != PCC_USA && t && t->STT_subISO &&
	    t->STT_subISO[0] != 0xffff)
	    for (w = t->STT_subISO + ui->PUID_countryCode * 16; *w; w++) {
		ps->PS_asciiTrans[*w >> 8] = C_SPACE;
		ps->PS_asciiTrans[*w & 0xff] = (byte) (*w >> 8);
	    }
    }
    /* the driver's own translations */
    if (t && t->STT_trans)
	for (i = 0; i < t->STT_transSize / 4; i++)
	    ps->PS_asciiTrans[t->STT_trans[i * 4]] = t->STT_trans[i * 4 + 1];
}
