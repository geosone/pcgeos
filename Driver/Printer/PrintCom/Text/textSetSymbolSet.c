/*
 * textSetSymbolSet.c -- PrintSetSymbolSet (the C port of
 * textSetSymbolSet.asm): the job's symbol set, if the device has a code
 * for it (PI_fontSymbolSets).
 */
#include "printcom.h"

Boolean PrintSetSymbolSet(PState *ps)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;
    const word *sets;
    PrinterSymbolSet s;

    if (!pi->PI_fontSymbolSets)
	return FALSE;
    /* a table of the control codes by PrinterSymbolSet: none of the
     * ported drivers has one (dmdevinfo2c.py refuses it), so its entries
     * would be indices into printFontCommands */
    sets = (const word *) ((const byte *) pi + pi->PI_fontSymbolSets);
    s = PS_UI_DATA(ps)->PUID_symbolSet;
    if (!sets[s / 2])
	return FALSE;
    ps->PS_curFont.FE_symbolSet = s;
    return SendCodeOut(ps, printFontCommands[sets[s / 2]]);
}
