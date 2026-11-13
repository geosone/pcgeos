/*
 * textLoadNoISOSymbolSet.c -- PrintLoadSymbolSet without ISO
 * substitutions in the printer (the C port of
 * textLoadNoISOSymbolSet.asm): the translation table of the driver's
 * tables (those of the symbol set the UI chose), nothing sent.
 */
#include "printcom.h"

Boolean PrintLoadSymbolSet(PState *ps)
{
    SpoolUpdateTranslationTable(ps, &printTransTables);
    return FALSE;
}
