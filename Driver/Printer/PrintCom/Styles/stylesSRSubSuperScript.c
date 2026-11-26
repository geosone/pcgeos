/*
 * stylesSRSubSuperScript.c -- after stylesSRSubSuperScript.asm:
 * SetSubscript and SetSuperscript a control code each; the script is
 * reset (ResetSubscript, ResetSuperscript) only when neither is left set
 * in the new style (the asm tested the TextStyle masks TS_SUBSCRIPT and
 * TS_SUPERSCRIPT against the PrintTextStyle word; so does C).  Included
 * by the style files of the drivers, as the asm is.
 */

static Boolean SetSubscript(PState *ps)
{
    return SendCodeOut(ps, pr_codes_SetSubscript);
}

static Boolean SetSuperscript(PState *ps)
{
    return SendCodeOut(ps, pr_codes_SetSuperscript);
}

static Boolean ResetSubscript(PState *ps)
{
    if (ps->PS_asciiStyle & (TS_SUBSCRIPT | TS_SUPERSCRIPT))
	return FALSE;
    return SendCodeOut(ps, pr_codes_ResetScript);
}
#define ResetSuperscript	ResetSubscript
