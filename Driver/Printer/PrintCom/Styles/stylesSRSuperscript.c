/*
 * stylesSRSuperscript.c -- after stylesSRSuperscript.asm: SetSuperscript and ResetSuperscript, a
 * control code each.  Included by the style files of the drivers, as
 * the asm is.
 */

static Boolean SetSuperscript(PState *ps)
{
    return SendCodeOut(ps, pr_codes_SetSuperscript);
}

static Boolean ResetSuperscript(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ResetSuperscript);
}
