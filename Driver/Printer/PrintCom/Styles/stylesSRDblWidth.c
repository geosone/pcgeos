/*
 * stylesSRDblWidth.c -- after stylesSRDblWidth.asm: SetDblWidth and ResetDblWidth, a
 * control code each.  Included by the style files of the drivers, as
 * the asm is.
 */

static Boolean SetDblWidth(PState *ps)
{
    return SendCodeOut(ps, pr_codes_SetDblWidth);
}

static Boolean ResetDblWidth(PState *ps)
{
    return SendCodeOut(ps, pr_codes_ResetDblWidth);
}
