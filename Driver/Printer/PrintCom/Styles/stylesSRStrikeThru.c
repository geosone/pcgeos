/*
 * stylesSRStrikeThru.c -- after stylesSRStrikeThru.asm: SetStrikeThru and ResetStrikeThru, a
 * control code each.  Included by the style files of the drivers, as
 * the asm is.
 */

static Boolean SetStrikeThru(PState *ps)
{
    return SendCodeOut(ps, pr_codes_SetStrikeThru);
}

static Boolean ResetStrikeThru(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ResetStrikeThru);
}
