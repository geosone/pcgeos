/*
 * geodesPatch.c -- after geodesPatch.asm: IsMultiLanguageModeOn.
 */
#include "kernelC.h"
#include "geosmod.h"
#include <string.h>

/* no multi-language mode (MULTI_LANGUAGE was off in the asm kernel) */
Boolean IsMultiLanguageModeOn(void)
{
    return FALSE;
}
