/*
 * printcomIBMPPDS24Styles.c -- the style routines of the IBM PPDS 24-pin
 * and the Canon 48 drivers (the C port of printcomIBMPPDS24Styles.asm and
 * the Styles/stylesSR*.asm it includes): a control code each for bold,
 * double height and width, italic, shadow, outline, NLQ, the scripts and
 * underline; none for condensed, overline, strike-thru, reverse, quad
 * height and the future bit.
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
CODE(SetDblHeight) CODE(ResetDblHeight)
CODE(SetDblWidth) CODE(ResetDblWidth)
CODE(SetItalic) CODE(ResetItalic)
CODE(SetShadow) CODE(ResetShadow)
CODE(SetOutline) CODE(ResetOutline)
CODE(SetNLQ) CODE(ResetNLQ)
CODE(SetSubscript) CODE(SetSuperscript)
CODE(SetUnderline) CODE(ResetUnderline)

/* stylesSRSubSuperScript.asm: reset only when neither is left set */
static Boolean ResetScript(PState *ps)
{
    if (ps->PS_asciiStyle & (TS_SUBSCRIPT | TS_SUPERSCRIPT))
	return FALSE;
    return SendCodeOut(ps, pr_codes_ResetScript);
}

/* by bit: PTS_FUTURE (0) ... PTS_CONDENSED (15) */
PrintStyleRoutine *const printSetStyle[16] = {
    None, None, None, SetDblHeight, SetDblWidth, None, SetOutline,
    SetShadow, None, SetUnderline, SetItalic, SetBold, SetNLQ,
    SetSuperscript, SetSubscript, None
};
PrintStyleRoutine *const printResetStyle[16] = {
    None, None, None, ResetDblHeight, ResetDblWidth, None, ResetOutline,
    ResetShadow, None, ResetUnderline, ResetItalic, ResetBold, ResetNLQ,
    ResetScript, ResetScript, None
};
