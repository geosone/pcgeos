/*
 * stylesSRBold.c -- after stylesSRBold.asm: SetBold and ResetBold, a
 * control code each.  Included by the style files of the drivers, as
 * the asm is.
 */

static Boolean SetBold(PState *ps)
{
    return SendCodeOut(ps, pr_codes_SetBold);
}

static Boolean ResetBold(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ResetBold);
}
