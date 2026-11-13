/*
 * printcomEpsonStyles.c -- the style routines of the Epson drivers (the
 * C port of printcomEpsonStyles.asm and the Styles/stylesSR*.asm it
 * includes): a control code each; none for strike-thru, shadow, outline,
 * reverse, quad height, overline and the future bit.
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
CODE(SetDblHeight) CODE(ResetDblHeight)
CODE(SetDblWidth) CODE(ResetDblWidth)
CODE(SetItalic) CODE(ResetItalic)
CODE(SetNLQ) CODE(ResetNLQ)
CODE(SetSubscript) CODE(SetSuperscript)
CODE(SetUnderline) CODE(ResetUnderline)

/* stylesSRSubSuperScript.asm: the script is reset only when neither is
 * left set (the asm tested the TextStyle masks TS_SUBSCRIPT and
 * TS_SUPERSCRIPT against the PrintTextStyle word; so does C) */
static Boolean ResetScript(PState *ps)
{
    if (ps->PS_asciiStyle & (TS_SUBSCRIPT | TS_SUPERSCRIPT))
	return FALSE;
    return SendCodeOut(ps, pr_codes_ResetScript);
}

/* by bit: PTS_FUTURE (0) ... PTS_CONDENSED (15) */
PrintStyleRoutine *const printSetStyle[16] = {
    None, None, None, SetDblHeight, SetDblWidth, None, None, None, None,
    SetUnderline, SetItalic, SetBold, SetNLQ, SetSuperscript, SetSubscript,
    SetCondensed
};
PrintStyleRoutine *const printResetStyle[16] = {
    None, None, None, ResetDblHeight, ResetDblWidth, None, None, None, None,
    ResetUnderline, ResetItalic, ResetBold, ResetNLQ, ResetScript,
    ResetScript, ResetCondensed
};
