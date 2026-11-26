/*
 * customLJ4PScript.c -- after customLJ4PScript.asm: PJL around the
 * job (PrintEnterLJ4PScript, PrintExitLJ4PScript).
 * Included by printcomPScriptJob.c, as the asm is.
 */

static Boolean SendCodes(PState *ps, const byte *const *codes)
{
    for (; *codes; codes++)
	if (SendCodeOut(ps, *codes))
	    return TRUE;
    return FALSE;
}

static Boolean PrintEnterLJ4PScript(PState *ps)
{
    static const byte *const codes[] = {
	pr_codes_PJLUEL, pr_codes_PJLCRLF, pr_codes_PJLMsgPScript,
	pr_codes_PJLEnterPostScript, NULL
    };
    return SendCodes(ps, codes);
}

static Boolean PrintExitLJ4PScript(PState *ps)
{
    static const byte *const codes[] = {
	pr_codes_PJLUEL, pr_codes_PJLCRLF, pr_codes_PJLMsgClear,
	pr_codes_PJLUEL, NULL
    };
    return SendCodes(ps, codes);
}
