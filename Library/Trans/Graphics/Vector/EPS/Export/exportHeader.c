/*
 * exportHeader.c -- the comments and sections around the pages (the C
 * port of exportHeader.asm): the header comments, the document setup
 * and the page setup.
 */
#include "exportInt.h"

#include <timedate.h>
#include <string.h>

static Boolean Send(MemHandle stream, const char *s, word len)
{
    return SendToStream(stream, s, len, NULL);
}

#define SEND(st, name)	Send(st, PS(name))

static Boolean SendString(MemHandle stream, const char *s)
{
    return Send(stream, s, EpsStrLen(s));
}

/* EmitBoundingBox: "%%BoundingBox: 0 0 W H" */
static Boolean EmitBoundingBox(PSExportBlock *o, MemHandle stream)
{
    char buf[40], *p = buf;

    if (SEND(stream, boundBox))
	return TRUE;
    memcpy(p, "0 0 ", 4);
    p += 4;
    p = SDWordToAscii(p, (sdword) o->PSEB_public.PSEO_common.GEO_docW);
    *p++ = ' ';
    p = SDWordToAscii(p, (sdword) o->PSEB_public.PSEO_common.GEO_docH);
    *p++ = '\r';
    *p++ = '\n';
    return Send(stream, buf, (word) (p - buf));
}

/* EmitCreationDate: "%%CreationDate: m/d/yy" */
static Boolean EmitCreationDate(MemHandle stream)
{
    TimerDateAndTime t;
    char buf[40], *p = buf;
    word year;

    if (SEND(stream, creationDate))
	return TRUE;
    TimerGetDateAndTime(&t);
    p = UWordToAscii(p, t.TDAT_month);
    *p++ = '/';
    p = UWordToAscii(p, t.TDAT_day);
    *p++ = '/';
    year = (word) (t.TDAT_year - 1900);
    if (year >= 100)
	year -= 100;
    p = UWordToAscii(p, year);
    *p++ = '\r';
    *p++ = '\n';
    return Send(stream, buf, (word) (p - buf));
}

/* EmitPageInfo: "%%Pages: n", the page order, the copies */
static Boolean EmitPageInfo(PSExportBlock *o, MemHandle stream)
{
    const GExportOpts *g = &o->PSEB_public.PSEO_common;
    char buf[40], *p = buf;

    if (SEND(stream, numPages))
	return TRUE;
    p = UWordToAscii(p, g->GEO_pages);
    *p++ = '\r';
    *p++ = '\n';
    if (Send(stream, buf, (word) (p - buf)))
	return TRUE;
    if (g->GEO_pages > 1) {
	if (g->GEO_flags & GEF_PAGES_DESCENDING) {
	    if (SEND(stream, descendOrder))
		return TRUE;
	} else if (SEND(stream, ascendOrder))
	    return TRUE;
    }
    if (g->GEO_copies <= 1)
	return FALSE;
    if (SEND(stream, requirements))
	return TRUE;
    p = UWordToAscii(buf, g->GEO_copies);
    *p++ = ')';
    if (Send(stream, buf, (word) (p - buf)))
	return TRUE;
    if ((g->GEO_flags & GEF_COLLATE) && SEND(stream, reqCollate))
	return TRUE;
    return Send(stream, "\r\n", 2);
}

/* EmitHeaderComments: the DSC header, up to "%%EndComments" */
Boolean EmitHeaderComments(PSExportBlock *o, MemHandle stream)
{
    const GExportOpts *g = &o->PSEB_public.PSEO_common;
    PSLevel level = o->PSEB_public.PSEO_level;

    if (o->PSEB_public.PSEO_flags & PSEF_EPS_FILE) {
	if (SEND(stream, epsFileID))
	    return TRUE;
    } else if (SEND(stream, printFileID))
	return TRUE;
    if (EmitBoundingBox(o, stream) || SEND(stream, creatorComment) ||
	SendString(stream, g->GEO_appName) || SEND(stream, emitCRLF) ||
	EmitCreationDate(stream) || SEND(stream, docData) ||
	EmitPageInfo(o, stream))
	return TRUE;
    if (level != 1) {
	if ((level & PSL_LEVEL) != 1) {
	    if (SEND(stream, level2))
		return TRUE;
	} else {
	    if (SEND(stream, extensionKey))
		return TRUE;
	    if ((level & PSL_CMYK) && SEND(stream, cmykSupport))
		return TRUE;
	    if ((level & PSL_DPS) && SEND(stream, dpsSupport))
		return TRUE;
	    if ((level & PSL_COMPOSITE) && SEND(stream, compositeSupport))
		return TRUE;
	    if ((level & PSL_FILE) && SEND(stream, fileSupport))
		return TRUE;
	    if (SEND(stream, emitCRLF))
		return TRUE;
	}
    }
    return SEND(stream, titleComment) || SendString(stream, g->GEO_docName)
	|| SEND(stream, emitCRLF) || SEND(stream, endComments);
}

/* EmitDocSetup: the end of the prolog, "/#copies n def", A4 */
Boolean EmitDocSetup(PSExportBlock *o, MemHandle stream)
{
    const GExportOpts *g = &o->PSEB_public.PSEO_common;
    char buf[20], *p;

    if (SEND(stream, endProlog) || SEND(stream, beginSetup) ||
	SEND(stream, emitNC))
	return TRUE;
    p = UWordToAscii(buf, g->GEO_copies);
    if (Send(stream, buf, (word) (p - buf)) || SEND(stream, emitDef))
	return TRUE;
    if (!(o->PSEB_public.PSEO_flags & PSEF_EPS_FILE) && g->GEO_docH == 842 &&
	SEND(stream, emitA4))
	return TRUE;
    return SEND(stream, endSetup);
}

/* EmitPageSetup: "%%Page: n n", the save and the dictionary */
Boolean EmitPageSetup(PSExportBlock *o, MemHandle stream)
{
    char buf[20], *p;

    if (SEND(stream, pageNumber))
	return TRUE;
    p = UWordToAscii(buf, o->PSEB_curPage);
    *p++ = ' ';
    p = UWordToAscii(p, o->PSEB_curPage);
    *p++ = '\r';
    *p++ = '\n';
    return Send(stream, buf, (word) (p - buf)) ||
	SEND(stream, beginPageSetup) || SEND(stream, emitSave) ||
	SEND(stream, emitStartDict) || SEND(stream, emitBP);
}

/* EmitEndPageSetup: the default transform, "%%EndPageSetup" */
Boolean EmitEndPageSetup(MemHandle stream)
{
    return SEND(stream, emitSDT) || SEND(stream, endPageSetup);
}
