/*
 * stylesSRSubscript.c -- after stylesSRSubscript.asm: SetSubscript and ResetSubscript, a
 * control code each.  Included by the style files of the drivers, as
 * the asm is.
 */

static Boolean SetSubscript(PState *ps)
{
    return SendCodeOut(ps, pr_codes_SetSubscript);
}

static Boolean ResetSubscript(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ResetSubscript);
}
