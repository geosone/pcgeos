/*
 * printcomPCLStyles.c -- the styles of the PCL drivers (after
 * printcomPCLStyles.asm, with the Styles/stylesSR*.c parts it includes,
 * as the asm includes Styles/stylesSR*.asm): bold, italic and underline
 * by their codes, the others nothing.
 */
#include "printcom.h"
#include "printcomDriver.h"
#include "Styles/stylesSRBold.c"
#include "Styles/stylesSRItalic.c"
#include "Styles/stylesSRUnderline.c"

static Boolean None(PState *ps)
{
    (void) ps;
    return FALSE;
}

/* by bit: PTS_FUTURE (0) ... PTS_CONDENSED (15) */
PrintStyleRoutine *const printSetStyle[16] = {
    None, None, None, None, None, None, None, None, None,
    SetUnderline, SetItalic, SetBold, None, None, None, None
};
PrintStyleRoutine *const printResetStyle[16] = {
    None, None, None, None, None, None, None, None, None,
    ResetUnderline, ResetItalic, ResetBold, None, None, None, None
};
