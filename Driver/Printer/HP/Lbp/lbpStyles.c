/*
 * lbpStyles.c -- the style routines of the Canon CaPSL driver (the C port
 * of printcomCapslStyles.asm's Styles/stylesSR*.asm): a control code each
 * for bold, italic, underline, shadow, outline, reverse and double width
 * (the character width at 200%); none for the others.
 */
#include "printcom.h"
#include "printcomDriver.h"

static Boolean None(PState *ps)
{
    (void) ps;
    return FALSE;
}

#define CODE(name) \
    static Boolean name(PState *ps) \
    { \
	return SendCodeOut(ps, pr_codes_##name); \
    }
CODE(SetBold) CODE(ResetBold)
CODE(SetItalic) CODE(ResetItalic)
CODE(SetUnderline) CODE(ResetUnderline)
CODE(SetShadow) CODE(ResetShadow)
CODE(SetOutline) CODE(ResetOutline)
CODE(SetReverse) CODE(ResetReverse)
CODE(SetDblWidth) CODE(ResetDblWidth)

/* by bit: PTS_FUTURE (0) ... PTS_CONDENSED (15) */
PrintStyleRoutine *const printSetStyle[16] = {
    None, None, None, None, SetDblWidth, SetReverse, SetOutline, SetShadow,
    None, SetUnderline, SetItalic, SetBold, None, None, None, None
};
PrintStyleRoutine *const printResetStyle[16] = {
    None, None, None, None, ResetDblWidth, ResetReverse, ResetOutline,
    ResetShadow, None, ResetUnderline, ResetItalic, ResetBold, None, None,
    None, None
};
