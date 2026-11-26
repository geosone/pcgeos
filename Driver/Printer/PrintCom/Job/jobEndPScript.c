/*
 * jobEndPScript.c -- after jobEndPScript.asm: PrintEndJob.
 * Included by printcomPScriptJob.c, as the asm is.
 */

Boolean PrintEndJob(PState *ps, DriverRegs *r)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;

    (void) r;
    if (ps->PS_epsLibrary) {
	EPSExportOptStreamFn *trailer = (EPSExportOptStreamFn *)
	    CallEPSLibrary(ps, TR_EXPORT_TRAILER);
	if (ps->PS_expansionInfo) {
	    MemHandle stream = PSStreamBlock(ps);
	    if (trailer && trailer(ps->PS_expansionInfo, stream) !=
		TE_NO_ERROR)
		ps->PS_error = 1;
	    if (stream)
		MemFree(stream);
	    MemFree(ps->PS_expansionInfo);
	    ps->PS_expansionInfo = 0;
	}
	GeodeFreeLibrary(ps->PS_epsLibrary);
	ps->PS_epsLibrary = 0;
    }
    if (PS_JOB_PARAMS(ps)->JP_portInfo.PPI_type != PPT_FILE &&
	pi->PI_customExit)
	return CustomRoutine(ps, pi->PI_customExit);
    return PrintClearStyles(ps);
}
