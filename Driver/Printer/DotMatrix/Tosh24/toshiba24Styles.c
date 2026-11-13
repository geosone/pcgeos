/*
 * toshiba24Styles.c -- the style routines of the Toshiba 24-pin driver
 * (the C port of toshiba24Styles.asm and the Styles/stylesSR*.asm it
 * includes): a control code each for condensed, NLQ, bold, italic,
 * underline, shadow and double width; none for the others.
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
CODE(SetCondensed) CODE(ResetCondensed)
CODE(SetNLQ) CODE(ResetNLQ)
CODE(SetBold) CODE(ResetBold)
CODE(SetItalic) CODE(ResetItalic)
CODE(SetUnderline) CODE(ResetUnderline)
CODE(SetShadow) CODE(ResetShadow)
CODE(SetDblWidth) CODE(ResetDblWidth)

/* by bit: PTS_FUTURE (0) ... PTS_CONDENSED (15) */
PrintStyleRoutine *const printSetStyle[16] = {
    None, None, None, None, SetDblWidth, None, None, SetShadow, None,
    SetUnderline, SetItalic, SetBold, SetNLQ, None, None, SetCondensed
};
PrintStyleRoutine *const printResetStyle[16] = {
    None, None, None, None, ResetDblWidth, None, None, ResetShadow, None,
    ResetUnderline, ResetItalic, ResetBold, ResetNLQ, None, None,
    ResetCondensed
};
