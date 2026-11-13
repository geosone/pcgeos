/*
 * textLoadEpsonSymbolSet.c -- PrintLoadSymbolSet (the C port of
 * textLoadEpsonSymbolSet.asm): the translation table of the symbol set
 * and country (SpoolUpdateTranslationTable), and the printer's country
 * (ESC R n; the "legal" set is 64).
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrintLoadSymbolSet(PState *ps)
{
    byte c;

    SpoolUpdateTranslationTable(ps, &printTransTables);
    if (SendCodeOut(ps, pr_codes_SetCountry))
	return TRUE;
    c = (byte) (PS_UI_DATA(ps)->PUID_countryCode + 1);
    if (c == PCC_LEGAL + 1)
	c = 64;
    return PrintStreamWriteByte(ps, c);
}
