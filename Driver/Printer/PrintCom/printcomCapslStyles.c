/*
 * printcomCapslStyles.c -- the style routines of the Canon CaPSL drivers
 * (after printcomCapslStyles.asm, with the Styles/stylesSR*.c parts it
 * includes, as the asm includes Styles/stylesSR*.asm): a control code
 * each for bold, italic, underline, shadow, outline, reverse and double
 * width (the character width at 200%); none for the others.
 */
#include "printcom.h"
#include "printcomDriver.h"
#include "Styles/stylesSRBold.c"
#include "Styles/stylesSRItalic.c"
#include "Styles/stylesSRUnderline.c"
#include "Styles/stylesSRShadow.c"
#include "Styles/stylesSROutline.c"
#include "Styles/stylesSRReverse.c"
#include "Styles/stylesSRDblWidth.c"

static Boolean None(PState *ps)
{
    (void) ps;
    return FALSE;
}

/* by bit: PTS_FUTURE (0) ... PTS_CONDENSED (15) */
PrintStyleRoutine *const printSetStyle[16] = {
    None, None, None, None, SetDblWidth, SetReverse, SetOutline, SetShadow,
    None, SetUnderline, SetItalic, SetBold, None, None, None, None
};
PrintStyleRoutine *const printResetStyle[16] = {
    None, None, None, None, ResetDblWidth, ResetReverse, ResetOutline,
    ResetShadow, None, ResetUnderline, ResetItalic, ResetBold, None,
    None, None, None
};
