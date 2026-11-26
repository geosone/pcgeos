/*
 * customHostIntegration.c -- after customHostIntegration.asm: nothing in Port32.
 * The asm printed through a temp file and the ps2pdf library of DOSBox;
 * here the host prints through the CUPS port driver (Driver/Stream/Cups),
 * so the device prints to its port as the others do.
 * Included by printcomPScriptJob.c, as the asm is.
 */

static Boolean PrintEnterHostIntegration(PState *ps)
{
    (void) ps;
    return FALSE;
}

static Boolean PrintExitHostIntegration(PState *ps)
{
    (void) ps;
    return FALSE;
}
