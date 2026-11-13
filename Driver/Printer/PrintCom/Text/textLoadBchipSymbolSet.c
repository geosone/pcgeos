/*
 * textLoadBchipSymbolSet.c -- PrintLoadSymbolSet (the C port of
 * textLoadBchipSymbolSet.asm): the translation table of the symbol set
 * and country (SpoolUpdateTranslationTable), and the printer's country
 * (its code plus 11).
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrintLoadSymbolSet(PState *ps)
{
    SpoolUpdateTranslationTable(ps, &printTransTables);
    return SendCodeOut(ps, pr_codes_SetCountry) ||
	PrintStreamWriteByte(ps,
			     (byte) (PS_UI_DATA(ps)->PUID_countryCode + 11));
}
