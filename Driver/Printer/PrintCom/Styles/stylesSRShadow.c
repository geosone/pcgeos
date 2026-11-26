/*
 * stylesSRShadow.c -- after stylesSRShadow.asm: SetShadow and ResetShadow, a
 * control code each.  Included by the style files of the drivers, as
 * the asm is.
 */

static Boolean SetShadow(PState *ps)
{
    return SendCodeOut(ps, pr_codes_SetShadow);
}

static Boolean ResetShadow(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ResetShadow);
}
