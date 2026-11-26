/*
 * stylesSROverline.c -- after stylesSROverline.asm: SetOverline and ResetOverline, a
 * control code each.  Included by the style files of the drivers, as
 * the asm is.
 */

static Boolean SetOverline(PState *ps)
{
    return SendCodeOut(ps, pr_codes_SetOverline);
}

static Boolean ResetOverline(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ResetOverline);
}
