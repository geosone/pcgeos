/*
 * stylesSRUnderline.c -- after stylesSRUnderline.asm: SetUnderline and ResetUnderline, a
 * control code each.  Included by the style files of the drivers, as
 * the asm is.
 */

static Boolean SetUnderline(PState *ps)
{
    return SendCodeOut(ps, pr_codes_SetUnderline);
}

static Boolean ResetUnderline(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ResetUnderline);
}
