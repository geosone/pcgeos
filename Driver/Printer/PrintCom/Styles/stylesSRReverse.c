/*
 * stylesSRReverse.c -- after stylesSRReverse.asm: SetReverse and ResetReverse, a
 * control code each.  Included by the style files of the drivers, as
 * the asm is.
 */

static Boolean SetReverse(PState *ps)
{
    return SendCodeOut(ps, pr_codes_SetReverse);
}

static Boolean ResetReverse(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ResetReverse);
}
