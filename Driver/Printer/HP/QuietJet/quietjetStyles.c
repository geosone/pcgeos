/*
 * quietjetStyles.c -- the style routines of the QuietJet driver (the C
 * port of quietjetStyles.asm and the Styles/stylesSR*.asm it includes):
 * a control code each for NLQ, bold and underline; none for the others.
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
CODE(SetNLQ) CODE(ResetNLQ)
CODE(SetBold) CODE(ResetBold)
CODE(SetUnderline) CODE(ResetUnderline)

/* by bit: PTS_FUTURE (0) ... PTS_CONDENSED (15) */
PrintStyleRoutine *const printSetStyle[16] = {
    None, None, None, None, None, None, None, None, None, SetUnderline,
    None, SetBold, SetNLQ, None, None, None
};
PrintStyleRoutine *const printResetStyle[16] = {
    None, None, None, None, None, None, None, None, None, ResetUnderline,
    None, ResetBold, ResetNLQ, None, None, None
};
