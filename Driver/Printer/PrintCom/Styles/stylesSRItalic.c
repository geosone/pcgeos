/*
 * stylesSRItalic.c -- after stylesSRItalic.asm: SetItalic and ResetItalic, a
 * control code each.  Included by the style files of the drivers, as
 * the asm is.
 */

static Boolean SetItalic(PState *ps)
{
    return SendCodeOut(ps, pr_codes_SetItalic);
}

static Boolean ResetItalic(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ResetItalic);
}
