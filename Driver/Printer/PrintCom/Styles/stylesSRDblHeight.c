/*
 * stylesSRDblHeight.c -- after stylesSRDblHeight.asm: SetDblHeight and ResetDblHeight, a
 * control code each.  Included by the style files of the drivers, as
 * the asm is.
 */

static Boolean SetDblHeight(PState *ps)
{
    return SendCodeOut(ps, pr_codes_SetDblHeight);
}

static Boolean ResetDblHeight(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ResetDblHeight);
}
