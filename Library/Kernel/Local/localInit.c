/*
 * localInit.c -- LocalInit (after localInit.asm): the localization
 * settings of geos.ini, at the boot.
 */
#include "kernelC.h"
#include "localInt.h"

void LocalInit(void)
{
    LocalDateTimeInitFormats();
    LocalNumericInitFormats();
    LocalInitQuotes();
    LocalInitTimezone();
}
