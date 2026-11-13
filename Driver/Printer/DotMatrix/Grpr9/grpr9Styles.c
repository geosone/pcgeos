/*
 * grpr9Styles.c -- the style routines of the IBM Graphics 9-pin driver
 * (the C port of grpr9Styles.asm and the Styles/stylesSR*.asm it
 * includes): a control code each for condensed, bold, overline, double
 * width, NLQ, sub- and superscript and underline; none for the others.
 */
#include "printcom.h"
#include "printcomDriver.h"

static Boolean None(PState *ps)
{
    (void) ps;
    return FALSE;
}

#define CODE(name) \
    static Boolean name(PState *ps) { return SendCodeOut(ps, pr_codes_##name); }
CODE(SetCondensed) CODE(ResetCondensed)
CODE(SetBold) CODE(ResetBold)
CODE(SetOverline) CODE(ResetOverline)
CODE(SetDblWidth) CODE(ResetDblWidth)
CODE(SetNLQ) CODE(ResetNLQ)
CODE(SetSubscript) CODE(SetSuperscript)
CODE(SetUnderline) CODE(ResetUnderline)

/* stylesSRSubSuperScript.asm, as printcomEpsonStyles.c */
static Boolean ResetScript(PState *ps)
{
    if (ps->PS_asciiStyle & (TS_SUBSCRIPT | TS_SUPERSCRIPT))
	return FALSE;
    return SendCodeOut(ps, pr_codes_ResetScript);
}

/* by bit: PTS_FUTURE (0) ... PTS_CONDENSED (15) */
PrintStyleRoutine *const printSetStyle[16] = {
    None, SetOverline, None, None, SetDblWidth, None, None, None, None,
    SetUnderline, None, SetBold, SetNLQ, SetSuperscript, SetSubscript,
    SetCondensed
};
PrintStyleRoutine *const printResetStyle[16] = {
    None, ResetOverline, None, None, ResetDblWidth, None, None, None, None,
    ResetUnderline, None, ResetBold, ResetNLQ, ResetScript, ResetScript,
    ResetCondensed
};
