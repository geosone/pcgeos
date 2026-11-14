/*
 * diabloStyles.c -- the style routines of the Diablo daisy wheel driver
 * (the C port of diabloStyles.asm and the Styles/stylesSR*.asm it
 * includes): a control code each for bold, italic and underline; none
 * for the others.
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

/* by bit: PTS_FUTURE (0) ... PTS_CONDENSED (15) */
PrintStyleRoutine *const printSetStyle[16] = {
    None, None, None, None, None, None, None, None,
    None, SetUnderline, SetItalic, SetBold, None, None, None, None
};
PrintStyleRoutine *const printResetStyle[16] = {
    None, None, None, None, None, None, None, None,
    None, ResetUnderline, ResetItalic, ResetBold, None, None, None, None
};
