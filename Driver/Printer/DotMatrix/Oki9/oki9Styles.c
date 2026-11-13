/*
 * oki9Styles.c -- the style routines of the Okidata 9-pin driver (the C
 * port of oki9Styles.asm and the Styles/stylesSR*.asm it includes): a
 * control code each for the scripts, NLQ, bold, italic and underline;
 * none for the others (the asm returned without clearing the carry; C
 * returns no error).
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
CODE(SetSubscript) CODE(ResetSubscript)
CODE(SetSuperscript) CODE(ResetSuperscript)
CODE(SetNLQ) CODE(ResetNLQ)
CODE(SetBold) CODE(ResetBold)
CODE(SetItalic) CODE(ResetItalic)
CODE(SetUnderline) CODE(ResetUnderline)

/* by bit: PTS_FUTURE (0) ... PTS_CONDENSED (15) */
PrintStyleRoutine *const printSetStyle[16] = {
    None, None, None, None, None, None, None, None, None, SetUnderline,
    SetItalic, SetBold, SetNLQ, SetSuperscript, SetSubscript, None
};
PrintStyleRoutine *const printResetStyle[16] = {
    None, None, None, None, None, None, None, None, None, ResetUnderline,
    ResetItalic, ResetBold, ResetNLQ, ResetSuperscript, ResetSubscript,
    None
};
