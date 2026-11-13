/*
 * fontTopLevelPCL4.c -- the soft font manager of the PCL 4 printers (the
 * C port of fontTopLevelPCL4.asm): started at the start of a page (the
 * limits of the device's DownloadProperties, a VM file for the
 * characters' bitmaps), the fonts deleted from the printer at its end;
 * a face per font, size, style and optional attributes, its characters
 * downloaded the first time a page uses them, spaces as moves.  When the
 * printer takes no more soft fonts (or none of the size), the printer's
 * resident font that comes closest is chosen instead.
 *
 * Port32: the bitmaps' file is a temporary file of SP_SPOOL (the asm's
 * was cbuf.bm there, one name for all jobs).
 */
#include "fontPCL4.h"
#include "printcomDriver.h"
#include <vm.h>
#include <string.h>

static HPFontInfo *LockFontInfo(PState *ps)
{
    return ps->PS_expansionInfo ? MemLock(ps->PS_expansionInfo) : NULL;
}

static void UnlockFontInfo(PState *ps)
{
    MemUnlock(ps->PS_expansionInfo);
}

Boolean FontInit(PState *ps)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;
    const word *dp = (const word *) ((const byte *) pi +
				     pi->PI_fontGeometries);
    HPFontInfo *fi;

    ps->PS_previousAttribute = 0xffff;
    ps->PS_expansionInfo = MemAlloc(sizeof(HPFontInfo),
				    HF_DYNAMIC | HF_SHARABLE,
				    HAF_LOCK | HAF_ZERO_INIT | HAF_NO_ERR);
    fi = MemDeref(ps->PS_expansionInfo);
    fi->HPFI_allocID = INITIAL_HP_FONT_ID;
    fi->HPFI_maxNumFonts = dp[0];		/* DP_maxNumFonts */
    fi->HPFI_maxPointsize = dp[1];		/* DP_maxPointsize */
    ps->PS_curOptFont.OFE_spacePad = 0;
    ps->PS_curOptFont.OFE_trackKern = 0;
    FilePushDir();
    FileSetStandardPath(SP_SPOOL);
    fi->HPFI_bmFileHandle = VMOpen(fi->HPFI_bmFileName,
				   VMAF_FORCE_READ_WRITE |
				   VMAF_FORCE_DENY_WRITE,
				   VMO_TEMP_FILE, 0);
    FilePopDir();
    UnlockFontInfo(ps);
    return FALSE;
}

/* FontExit: the page's soft fonts deleted (by their IDs, the last
 * first), the bitmaps' file closed and deleted */
Boolean FontExit(PState *ps)
{
    HPFontInfo *fi = LockFontInfo(ps);
    word id;

    if (!fi)
	return FALSE;
    for (id = fi->HPFI_allocID; id != INITIAL_HP_FONT_ID; id--)
	if (WriteNumCommand(ps, pr_codes_SetFontID, id) ||
	    WriteNumCommand(ps, pr_codes_FontControl,
			    HPFC_DELETE_SOFT_FONT)) {
	    UnlockFontInfo(ps);
	    return TRUE;
	}
    if (fi->HPFI_bmFileHandle) {
	VMClose(fi->HPFI_bmFileHandle, TRUE);
	FilePushDir();
	FileSetStandardPath(SP_SPOOL);
	FileDelete(fi->HPFI_bmFileName);
	FilePopDir();
    }
    MemFree(ps->PS_expansionInfo);
    ps->PS_expansionInfo = 0;
    return FALSE;
}

/* FontAddFace: the soft font of PS_curFont, PS_asciiStyle and
 * PS_curOptFont made and its header sent, unless the printer has it,
 * takes no more fonts, none of the size, or the font has no width */
Boolean FontAddFace(PState *ps)
{
    HPFontInfo *fi = LockFontInfo(ps);
    SoftFontEntry *sfe;

    if (!fi)
	return FALSE;
    if (fi->HPFI_maxNumFonts > fi->HPFI_numFonts &&
	fi->HPFI_maxPointsize >= ps->PS_curFont.FE_size &&
	ps->PS_curOptFont.OFE_fontWidth) {
	fi->HPFI_heapCount++;
	sfe = IsFontInPrinter(ps, fi);
	if (!sfe && fi->HPFI_numFonts < MAX_SFE_NUMBER) {
	    sfe = AddFontEntry(ps, fi, ++fi->HPFI_allocID);
	    CreateFontHeader(ps, fi, sfe);
	    SendFontHeader(ps, fi, sfe);
	}
	if (sfe) {
	    sfe->SFE_usage = fi->HPFI_heapCount;
	    sfe->SFE_flags |= SFF_ON_PAGE;
	}
    }
    UnlockFontInfo(ps);
    return FALSE;
}

/* FontAddChars: the characters of the text not yet on the page
 * downloaded to the current soft font, which is selected; without one,
 * the resident font */
Boolean FontAddChars(PState *ps, const byte *text, word count)
{
    HPFontInfo *fi = LockFontInfo(ps);
    SoftFontEntry *sfe = fi ? IsFontInPrinter(ps, fi) : NULL;
    Boolean err;
    word i;
    byte c;

    if (!sfe) {
	if (fi)
	    UnlockFontInfo(ps);
	return PrintSetFontWayInt(ps);	/* FontSetAttrs */
    }
    for (i = 0; i < count; i++) {
	c = text[i];
	if (c <= C_SPACE || c == C_NONBRKSPACE ||
	    (sfe->SFE_chars[c] & SCF_ON_PAGE))
	    continue;
	sfe->SFE_chars[c] |= SCF_ON_PAGE;
	DownloadCharData(ps, fi, sfe, c);
    }
    err = WriteNumCommand(ps, pr_codes_SelectFont, sfe->SFE_fontTag);
    UnlockFontInfo(ps);
    return err;
}

/* FontSendSpace: in a soft font a move right by the width of a space
 * (and the track kerning; none if that is negative), else a space */
Boolean FontSendSpace(PState *ps)
{
    HPFontInfo *fi;
    SoftFontEntry *sfe;
    GStateHandle gs;
    sword w;
    Boolean err = FALSE;

    if (ps->PS_printerSmart != PS_DUMB_RASTER &&
	(fi = LockFontInfo(ps)) != NULL) {
	sfe = IsFontInPrinter(ps, fi);
	if (sfe) {
	    gs = GrCreateState(0);
	    SetTextAttrs(gs, sfe);
	    w = (sword) (GrCharWidth(gs, C_SPACE) >> 16);
	    GrDestroyState(gs);
	    w = (sword) (w + sfe->SFE_test.TFE_optFontEntry.OFE_trackKern);
	    if (w >= 0)
		err = WriteNumCommand(ps, pr_codes_RightRelMove, (word) w);
	    UnlockFontInfo(ps);
	    return err;
	}
	UnlockFontInfo(ps);
    }
    return PrintStreamWriteByte(ps, C_SPACE);
}
