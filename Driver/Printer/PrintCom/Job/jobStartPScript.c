/*
 * jobStartPScript.c -- after jobStartPScript.asm: PrintStartJob.
 * Included by printcomPScriptJob.c, as the asm is.
 */

static const char epsFileName[] = "eps.geo";

static void CopyName(char *dst, word dsize, const char *src, word ssize)
{
    word i;

    for (i = 0; i + 1 < dsize && i < ssize && src[i]; i++)
	dst[i] = src[i];
    dst[i] = 0;
}

Boolean PrintStartJob(PState *ps, DriverRegs *r)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;
    const PSInfoStruct *psis = PS_INFO_STRUCT(pi);
    JobParameters *jp = PS_JOB_PARAMS(ps);
    TransGetExportOptionsFn *getOpts;
    EPSExportOptStreamFn *header;
    GeodeLoadError e;
    PSExportBlock *o;
    MemHandle options, stream;
    word copies;

    (void) r;
    ps->PS_printerType = pi->PI_type;
    ps->PS_printerSmart = pi->PI_smarts;
    if (jp->JP_portInfo.PPI_type != PPT_FILE && pi->PI_customEntry &&
	CustomRoutine(ps, pi->PI_customEntry))
	return TRUE;

    PrSetPaperPath(ps, PS_UI_DATA(ps)->PUID_paperInput, 0);
    ps->PS_cursorPos.P_x = 0;
    ps->PS_cursorPos.P_y = 0;
    ps->PS_epsLibrary = 0;			/* in case the load fails */
    ps->PS_expansionInfo = 0;

    FilePushDir();
    FileSetStandardPath(SP_IMPORT_EXPORT_DRIVERS);
    ps->PS_epsLibrary = GeodeUseLibrary(epsFileName, 0, 0, &e);
    FilePopDir();
    if (!ps->PS_epsLibrary)
	return TRUE;

    getOpts = (TransGetExportOptionsFn *) CallEPSLibrary(ps,
						TR_GET_EXPORT_OPTIONS);
    options = getOpts ? getOpts(0) : 0;
    if (!options)
	return TRUE;
    ps->PS_expansionInfo = options;
    stream = CreatePostScriptBlock(ps);

    o = MemLock(options);
    o->PSEB_public.PSEO_flags &= ~PSEF_EPS_FILE;
    o->PSEB_public.PSEO_common.GEO_hFile = (FileHandle) stream;
    o->PSEB_public.PSEO_common.GEO_pages = jp->JP_numPages;
    /* several copies: collated, the spooler makes them; else PostScript */
    copies = jp->JP_numCopies;
    if (copies > 1) {
	jp->JP_numCopies = 1;
	if (jp->JP_spoolOpts & SO_COLLATE) {
	    jp->JP_numCopies = (byte) copies;
	    copies = 1;
	}
    }
    o->PSEB_public.PSEO_common.GEO_copies = copies;
    o->PSEB_public.PSEO_common.GEO_docH = jp->JP_docSizeInfo.PSR_height;
    o->PSEB_public.PSEO_common.GEO_docW = jp->JP_docSizeInfo.PSR_width;
    if (jp->JP_spoolOpts & SO_COLLATE)
	o->PSEB_public.PSEO_common.GEO_flags |= GEF_COLLATE;
    CopyName(o->PSEB_public.PSEO_common.GEO_appName,
	     sizeof(o->PSEB_public.PSEO_common.GEO_appName),
	     jp->JP_parent, sizeof(jp->JP_parent));
    CopyName(o->PSEB_public.PSEO_common.GEO_docName,
	     sizeof(o->PSEB_public.PSEO_common.GEO_docName),
	     jp->JP_documentName, sizeof(jp->JP_documentName));
    o->PSEB_public.PSEO_fonts = psis->PSIS_fonts;
    o->PSEB_public.PSEO_level = psis->PSIS_level;
    MemUnlock(options);

    header = (EPSExportOptStreamFn *) CallEPSLibrary(ps, TR_EXPORT_HEADER);
    if (!header || header(options, stream) != TE_NO_ERROR)
	return TRUE;
    return AppendToProlog(ps, options, stream);
}
