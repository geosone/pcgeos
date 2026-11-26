/*
 * stylesSROutline.c -- after stylesSROutline.asm: SetOutline and ResetOutline, a
 * control code each.  Included by the style files of the drivers, as
 * the asm is.
 */

static Boolean SetOutline(PState *ps)
{
    return SendCodeOut(ps, pr_codes_SetOutline);
}

static Boolean ResetOutline(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ResetOutline);
}
