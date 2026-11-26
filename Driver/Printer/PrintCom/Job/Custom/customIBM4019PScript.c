/*
 * customIBM4019PScript.c -- after customIBM4019PScript.asm: the tray,
 * then PostScript (PrintEnterIBM4019PScript).
 * Included by printcomPScriptJob.c, as the asm is.
 */

static Boolean PrintEnterIBM4019PScript(PState *ps)
{
    byte in = PS_UI_DATA(ps)->PUID_paperInput, tray;

    if (SendCodeOut(ps, pr_codes_IBMSelectTraySIC))
	return TRUE;
    if (in == (ASF_TRAY2 << PIO_ASF_OFFSET))
	tray = 0x90;				/* the bottom tray */
    else if (in == (MF_MANUAL1 << PIO_MANUAL_OFFSET))
	tray = 0x80;				/* manual feed */
    else if (in == (ASF_TRAY3 << PIO_ASF_OFFSET))
	tray = 0x98;				/* the envelope tray */
    else
	tray = 0x88;				/* the upper tray */
    if (PrintStreamWriteByte(ps, tray))
	return TRUE;
    return SendCodeOut(ps, pr_codes_IBMEnterPostScriptSIC);
}
