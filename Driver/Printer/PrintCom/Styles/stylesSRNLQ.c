/*
 * stylesSRNLQ.c -- after stylesSRNLQ.asm: SetNLQ and ResetNLQ, a
 * control code each.  Included by the style files of the drivers, as
 * the asm is.
 */

static Boolean SetNLQ(PState *ps)
{
    return SendCodeOut(ps, pr_codes_SetNLQ);
}

static Boolean ResetNLQ(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ResetNLQ);
}
