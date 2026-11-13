/*
 * customPPDS24.c -- PrintGoToUserDef (the C port of customPPDS24.asm):
 * the custom exit of the IBM PPDS 24-pin devices, back to the printer's
 * user defined settings (pr_codes_ReturnSIC) instead of the reset.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrintGoToUserDef(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ReturnSIC);
}
