/*
 * localInit.c -- LocalInit (after localInit.asm): the localization
 * settings of geos.ini, at the boot.
 *
 * Not yet: the date and time formats (DateTimeInitFormats) and the time
 * zone (InitTimezone).
 */
#include "kernelC.h"
#include "localInt.h"

void LocalInit(void)
{
    LocalNumericInitFormats();
    LocalInitQuotes();
}
