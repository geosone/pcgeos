/*
 * oki9Styles.c -- the style routines of the Okidata 9-pin driver (after
 * oki9Styles.asm, with the Styles/stylesSR*.c parts it includes, as the
 * asm includes Styles/stylesSR*.asm): a control code each for the
 * scripts, NLQ, bold, italic and underline; none for the others (the asm
 * returned without clearing the carry; C returns no error).
 */
#include "printcom.h"
#include "printcomDriver.h"
#include "Styles/stylesSRSubscript.c"
#include "Styles/stylesSRSuperscript.c"
#include "Styles/stylesSRNLQ.c"
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
    None, None, None, None, None, None, None, None, None, SetUnderline,
    SetItalic, SetBold, SetNLQ, SetSuperscript, SetSubscript, None
};
PrintStyleRoutine *const printResetStyle[16] = {
    None, None, None, None, None, None, None, None, None,
    ResetUnderline, ResetItalic, ResetBold, ResetNLQ, ResetSuperscript,
    ResetSubscript, None
};
