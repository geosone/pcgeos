/*
 * stylesSRCondensed.c -- after stylesSRCondensed.asm: SetCondensed and ResetCondensed, a
 * control code each.  Included by the style files of the drivers, as
 * the asm is.
 */

static Boolean SetCondensed(PState *ps)
{
    return SendCodeOut(ps, pr_codes_SetCondensed);
}

static Boolean ResetCondensed(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ResetCondensed);
}
