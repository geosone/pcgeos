/*
 * processText.c -- printing in the text modes (the C port of
 * processText.asm).
 *
 * The application draws its spool file as for a graphics page.  Here
 * the text of a page is taken out of its graphics string, each string
 * with its place (in points, on the paper) and its attributes, into the
 * TextStrings block (CInclude/Internal/spoolInt.h): a chunk array of
 * StyleRunInfo sorted by y, then x, and an element array of the
 * TextAttrInfo they use.  Then the strings go to the printer driver
 * (DR_PRINT_STYLE_RUN) one by one, for each sheet the page is tiled
 * over; a string across the right edge of a sheet is cut in two.
 *
 * Port32: the element buffer is the chunk TS_gsBuffer, as in the asm; a
 * GR_DRAW_TEXT_FIELD is read whole (GrGetGStringElement) and skipped
 * instead of copied into a graphics string of its own.  The elements of
 * the style run array have an even size (their words aligned).  The
 * DBCS build is not ported.  DR_PRINT_STYLE_RUN: DR_dsSi = the
 * StyleRunInfo, DR_esDi = the TextStrings block, ax and cx = the tile's
 * place on the paper (Port32/ABI.md section 7.6).
 */
#include "spoolLocal.h"
#include <Internal/spoolInt.h>
#include <string.h>

/* the DOS16 layout of a GR_DRAW_TEXT_FIELD: the opcode and GDF_saved,
 * then the runs: a count, the TextAttr (23 bytes), the text */
#define FIELD_HEAD	15
#define FIELD_RUN	(2 + 23)

#define SRI_SIZE(n)	((word) ((offsetof(StyleRunInfo, SRI_text) + (n) + 1) \
				 & ~1))

static word W(const byte *p)
{
    return (word) (p[0] | (p[1] << 8));
}

static TextStrings *TS(SpoolJobInfo *j)
{
    return MemDeref(j->SJI_tsHan);
}

static optr RunArray(SpoolJobInfo *j)
{
    return ConstructOptr(j->SJI_tsHan, TS(j)->TS_styleRunInfo);
}

static StyleRunInfo *Run(SpoolJobInfo *j, word n)
{
    word size;

    return ChunkArrayElementToPtr(RunArray(j), n, &size);
}

/* DoTextInit: the gstate the page is played on and the TextStrings
 * block; 0 if all is well */
word DoTextInit(SpoolJobInfo *j)
{
    TextStrings *ts;
    MemHandle mh;
    ChunkHandle ch;

    j->SJI_gstate = GrCreateState(0);
    GrApplyTransform(j->SJI_gstate, &j->SJI_defMatrix);
    mh = MemAllocLMem(LMEM_TYPE_GENERAL, sizeof(TextStrings));
    if (!mh) {
	GrDestroyState(j->SJI_gstate);
	j->SJI_gstate = 0;
	return 1;
    }
    j->SJI_tsHan = mh;
    MemLock(mh);
    ch = LMemAlloc(mh, 0);
    TS(j)->TS_gsBuffer = ch;
    ch = ChunkArrayCreate(mh, 0, 0, OCF_IGNORE_DIRTY);
    TS(j)->TS_styleRunInfo = ch;
    ch = ElementArrayCreate(mh, sizeof(TextAttrInfo), 0, 0);
    ts = TS(j);
    ts->TS_textAttributeInfo = ch;
    MemUnlock(mh);
    return 0;
}

/* InitTextStringsBlock: no strings for the next page (the attributes
 * stay, as in the asm) */
static void InitTextStringsBlock(SpoolJobInfo *j)
{
    word n;

    MemLock(j->SJI_tsHan);
    n = ChunkArrayGetCount(RunArray(j));
    if (n)
	ChunkArrayDeleteRange(RunArray(j), 0, n);
    MemUnlock(j->SJI_tsHan);
}

/* DoTextCleanup: the block and the gstate go; after an error, what the
 * port has not written yet is dropped and the paper fed out */
void DoTextCleanup(SpoolJobInfo *j)
{
    DriverRegs r;
    PState *ps;

    if (j->SJI_tsHan)
	MemFree(j->SJI_tsHan);
    j->SJI_tsHan = 0;
    if (j->SJI_gstate)
	GrDestroyState(j->SJI_gstate);
    j->SJI_gstate = 0;
    if (!CheckForErrors(j, NULL))
	return;
    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) STREAM_ROLES_WRITER;		/* STREAM_WRITE */
    ps = MemLock(j->SJI_pstate);
    r.DR_bx = ps->PS_streamToken;
    MemUnlock(j->SJI_pstate);
    CallPort(j, DR_STREAM_FLUSH, &r);
    memset(&r, 0, sizeof(r));
    r.DR_cx = C_FF;
    CallPrinter(j, DR_PRINT_END_PAGE, &r);
}

/* ---------------------------------------------------------------------
 * The text of a page into the TextStrings block
 * ------------------------------------------------------------------- */

/* ReadNextElement: the element the graphics string stopped on, in the
 * chunk TS_gsBuffer (the block locked); its address */
static byte *ReadNextElement(SpoolJobInfo *j, word *size)
{
    word n = 0;

    GrGetGStringElement(j->SJI_gstate, j->SJI_gstring, 0, NULL, &n);
    LMemReAllocHandles(j->SJI_tsHan, TS(j)->TS_gsBuffer, (word) (n + 2));
    GrGetGStringElement(j->SJI_gstate, j->SJI_gstring, n,
			LMemDerefHandles(j->SJI_tsHan, TS(j)->TS_gsBuffer),
			&n);
    if (size)
	*size = n;
    return LMemDerefHandles(j->SJI_tsHan, TS(j)->TS_gsBuffer);
}

/* TransformStringPosition: the place of a string to the baseline (the
 * text mode says where it was drawn from), on the paper */
static void TransformStringPosition(SpoolJobInfo *j, sword *x, sword *y)
{
    GStateHandle gs = j->SJI_gstate;
    word m = (word) (GrGetTextMode(gs) &
		     (TM_DRAW_BASE | TM_DRAW_BOTTOM | TM_DRAW_ACCENT));
    XYValueAsDWord xy;

    if (!m)
	*y = (sword) (*y + (sword) GrFontMetrics(gs, GFMI_BASELINE_ROUNDED));
    else if (m & ~TM_DRAW_BASE) {
	if (!(m & ~(TM_DRAW_BASE | TM_DRAW_BOTTOM)))
	    *y = (sword) (*y + (sword) GrFontMetrics(gs,
						     GFMI_DESCENT_ROUNDED));
	else
	    *y = (sword) (*y + (sword) GrFontMetrics(gs,
						     GFMI_ASCENT_ROUNDED));
    }
    xy = GrTransform(gs, *x, *y);
    *x = DWORD_X(xy);
    *y = DWORD_Y(xy);
}

/* TranslateSpecialChars: a non breaking hyphen is a hyphen; an optional
 * one only at the end, if the text mode draws them.  Port32: the asm
 * compared the text mode with TM_DRAW_OPTIONAL_HYPHENS and dropped the
 * hyphen when they were equal, the opposite of what it meant. */
static word TranslateSpecialChars(GStateHandle gs, char *s, word count)
{
    word i, out = 0;
    char c;

    for (i = 0; i < count; i++) {
	c = s[i];
	if ((byte) c == C_NONBRKHYPHEN)
	    c = C_HYPHEN;
	else if ((byte) c == C_OPTHYPHEN) {
	    if (i != count - 1 ||
		!(GrGetTextMode(gs) & TM_DRAW_OPTIONAL_HYPHENS))
		continue;
	    c = C_HYPHEN;
	}
	s[out++] = c;
    }
    return out;
}

/* MapToPrinterStyle: the TextStyle as a PrintTextStyle */
static word MapToPrinterStyle(word ts)
{
    static const struct { byte from; word to; } map[] = {
	{ TS_OUTLINE, PTS_OUTLINE }, { TS_BOLD, PTS_BOLD },
	{ TS_ITALIC, PTS_ITALIC }, { TS_SUPERSCRIPT, PTS_SUPERSCRIPT },
	{ TS_SUBSCRIPT, PTS_SUBSCRIPT }, { TS_STRIKE_THRU, PTS_STRIKETHRU },
	{ TS_UNDERLINE, PTS_UNDERLINE }
    };
    word p = 0, i;

    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
	if (ts & map[i].from)
	    p |= map[i].to;
    return p;
}

/* GetTextAttr: the attributes of the gstate that the printer uses */
static void GetTextAttr(GStateHandle gs, TextAttrInfo *a)
{
    RGBColorAsDWord c = GrGetTextColor(gs);
    WWFixedAsDWord w;

    memset(a, 0, sizeof(*a));
    a->TAI_color.RGB_red = (byte) RGB_RED(c);
    a->TAI_color.RGB_green = (byte) RGB_GREEN(c);
    a->TAI_color.RGB_blue = (byte) RGB_BLUE(c);
    a->TAI_mask = (SystemDrawMask) GrGetTextMask(gs, NULL);
    a->TAI_style = MapToPrinterStyle(GrGetTextStyle(gs));
    a->TAI_mode = (TextMode) GrGetTextMode(gs);
    w = GrGetTextSpacePad(gs);
    a->TAI_spacePad.WBF_int = (sword) (w >> 16);
    a->TAI_spacePad.WBF_frac = (byte) (w >> 8);
    a->TAI_font = GrGetFont(gs, &w);
    a->TAI_size.WBF_int = (sword) (w >> 16);
    a->TAI_size.WBF_frac = (byte) (w >> 8);
    a->TAI_trackKern = (sword) GrGetTrackKern(gs);
    a->TAI_fontWeight = (FontWeight) GrGetFontWeight(gs);
    a->TAI_fontWidth = (FontWidth) GrGetFontWidth(gs);
}

/* AllocStringChunk: count characters at offset off of TS_gsBuffer, at
 * x, y on the paper, into the style run array, in order of y, then x */
static void AllocStringChunk(SpoolJobInfo *j, word off, word count,
			     sword x, sword y)
{
    TextAttrInfo a;
    StyleRunInfo *e;
    char *text;
    word n, i;
    WWFixedAsDWord w;

    if (!count)
	return;
    if (!(j->SJI_printState & SPS_FORM_FEED))
	y = (sword) (y + PAPER_INFO(j).PSR_margins.PCMP_top);
    text = (char *) LMemDerefHandles(j->SJI_tsHan, TS(j)->TS_gsBuffer) +
	off;
    if (count == 1 && (byte) text[0] < C_SPACE)
	return;				/* a control code */
    count = TranslateSpecialChars(j->SJI_gstate, text, count);
    j->SJI_textXPosition = (word) x;
    j->SJI_textYPosition = (word) y;

    /* in front of the first string below it, or right of it */
    n = ChunkArrayGetCount(RunArray(j));
    for (i = 0; i < n; i++) {
	e = Run(j, i);
	if ((word) y < e->SRI_yPosition ||
	    ((word) y == e->SRI_yPosition && (word) x <= e->SRI_xPosition))
	    break;
    }
    if (i < n)
	e = ChunkArrayInsertAt(RunArray(j), Run(j, i), SRI_SIZE(count));
    else
	e = ChunkArrayAppend(RunArray(j), SRI_SIZE(count));
    if (!e)
	return;
    e->SRI_yPosition = (word) y;
    e->SRI_xPosition = (word) x;
    e->SRI_numChars = count;
    text = (char *) LMemDerefHandles(j->SJI_tsHan, TS(j)->TS_gsBuffer) +
	off;
    memcpy(&e->SRI_text, text, count);
    w = GrTextWidthWWFixed(j->SJI_gstate, (const char *) &e->SRI_text,
			   count);
    e->SRI_stringWidth.WBF_int = (sword) (w >> 16);
    e->SRI_stringWidth.WBF_frac = (byte) (w >> 8);

    GetTextAttr(j->SJI_gstate, &a);
    n = ElementArrayAddElement(ConstructOptr(j->SJI_tsHan,
					     TS(j)->TS_textAttributeInfo),
			       &a, 0, NULL);
    TS(j)->TS_testAttribute = a;
    Run(j, i)->SRI_attributes = n;
}

/* GetTextString and friends: a string or a character, drawn at a place
 * or at the pen */
static void GetText(SpoolJobInfo *j, byte op)
{
    byte *e = ReadNextElement(j, NULL);
    word count, off;
    sword x, y;
    XYValueAsDWord cp;

    switch (op) {
    case GR_DRAW_TEXT:			/* op x y count text */
	x = (sword) W(e + 1);
	y = (sword) W(e + 3);
	count = W(e + 5);
	off = 7;
	break;
    case GR_DRAW_TEXT_CP:		/* op count text */
	count = W(e + 1);
	off = 3;
	goto atCP;
    case GR_DRAW_CHAR:			/* op char x y */
	count = 1;
	off = 1;
	x = (sword) W(e + 2);
	y = (sword) W(e + 4);
	break;
    default:				/* GR_DRAW_CHAR_CP: op char */
	count = 1;
	off = 1;
    atCP:
	cp = GrGetCurPos(j->SJI_gstate);
	x = DWORD_X(cp);
	y = DWORD_Y(cp);
	break;
    }
    TransformStringPosition(j, &x, &y);
    AllocStringChunk(j, off, count, x, y);
}

/* GetTextFieldString: each style run of a text field with its
 * attributes, the auto hyphen at the end */
static void GetTextFieldString(SpoolJobInfo *j)
{
    GStateHandle gs = j->SJI_gstate;
    word size, off = FIELD_HEAD, n, i;
    sword left, x, y, fx, fy;
    byte *e = ReadNextElement(j, &size);
    byte flags = e[14];
    TextAttr ta;
    XYValueAsDWord xy;
    const byte *a;

    left = (sword) W(e + 1);
    x = (sword) W(e + 4);			/* GDFS_drawPos, its int */
    y = (sword) ((sword) W(e + 7) + (sword) W(e + 10)); /* + baseline */
    GrSetGStringPos(j->SJI_gstring, GSSPT_SKIP_1, 0);
    while (left > 0 && (word) (off + FIELD_RUN) <= size) {
	e = LMemDerefHandles(j->SJI_tsHan, TS(j)->TS_gsBuffer);
	n = W(e + off);
	if (!n || (word) (off + FIELD_RUN + n) > size)
	    break;
	if ((flags & HF_AUTO_HYPHEN) && n == (word) left) {
	    /* FixUpAutoHyphen: the last run gets the hyphen */
	    LMemReAllocHandles(j->SJI_tsHan, TS(j)->TS_gsBuffer,
			       (word) (off + FIELD_RUN + n + 1));
	    e = LMemDerefHandles(j->SJI_tsHan, TS(j)->TS_gsBuffer);
	    e[off + FIELD_RUN + n] = C_HYPHEN;
	    n++;
	    left++;
	    size = (word) (off + FIELD_RUN + n);
	}
	/* HandleStyleRun: its attributes (no space padding) */
	a = e + off + 2;
	memset(&ta, 0, sizeof(ta));
	ta.TA_color.CQ_redOrIndex = a[0];
	ta.TA_color.CQ_info = a[1];
	ta.TA_color.CQ_green = a[2];
	ta.TA_color.CQ_blue = a[3];
	ta.TA_mask = a[4];
	ta.TA_pattern.HP_type = a[5];
	ta.TA_pattern.HP_data = a[6];
	ta.TA_styleSet = a[7];
	ta.TA_styleClear = a[8];
	ta.TA_modeSet = a[9];
	ta.TA_modeClear = a[10];
	ta.TA_font = (FontID) W(a + 14);
	ta.TA_size.WBF_frac = a[16];
	ta.TA_size.WBF_int = (sword) W(a + 17);
	ta.TA_trackKern = (sword) W(a + 19);
	ta.TA_fontWeight = a[21];
	ta.TA_fontWidth = a[22];
	GrSetTextAttr(gs, &ta);
	i = GrTextWidth(gs, (const char *) e + off + FIELD_RUN, n);
	xy = GrTransform(gs, x, y);
	fx = DWORD_X(xy);
	fy = DWORD_Y(xy);
	GrSaveState(gs);
	AllocStringChunk(j, (word) (off + FIELD_RUN), n, fx, fy);
	GrRestoreState(gs);
	x = (sword) (x + i);
	off = (word) (off + FIELD_RUN + n);
	left = (sword) (left - n);
    }
}

/* GetTextStrings: the text of one page of the graphics string; its
 * GSRetType */
static GSRetType GetTextStrings(SpoolJobInfo *j)
{
    GSRetType ret;
    word op;

    MemLock(j->SJI_tsHan);
    for (;;) {
	op = 0;
	ret = GrDrawGString(j->SJI_gstate, j->SJI_gstring, 0, 0,
			    GSC_NEW_PAGE | GSC_OUTPUT, &op);
	if (ret == GSRT_FAULT)
	    ret = GSRT_COMPLETE;
	if (ret == GSRT_NEW_PAGE || ret == GSRT_COMPLETE)
	    break;
	switch ((byte) op) {
	case GR_DRAW_TEXT_FIELD:
	    GetTextFieldString(j);
	    break;
	case GR_DRAW_TEXT:
	case GR_DRAW_TEXT_CP:
	case GR_DRAW_CHAR:
	case GR_DRAW_CHAR_CP:
	    GetText(j, (byte) op);
	    break;
	default:			/* played by the next round */
	    break;
	}
    }
    MemUnlock(j->SJI_tsHan);
    return ret;
}

/* ---------------------------------------------------------------------
 * The strings to the printer
 * ------------------------------------------------------------------- */

/* SnipTextString: string n reaches over points past the right edge of
 * the sheet: the part on the sheet becomes a string of its own (n), the
 * rest the next (n + 1).  TRUE if no character is on the sheet: then
 * the string only moves right, to the next sheet. */
static Boolean SnipTextString(SpoolJobInfo *j, word n, sword over)
{
    StyleRunInfo *e = Run(j, n), *l;
    sword keep = (sword) (e->SRI_stringWidth.WBF_int - over);
    WWFixedAsDWord f;
    word chars;

    f = GrUDivWWFixed((dword) (word) keep << 16,
		      (dword) (word) e->SRI_stringWidth.WBF_int << 16);
    f = GrMulWWFixed(f, (dword) e->SRI_numChars << 16);
    chars = (word) (f >> 16);
    if (!chars) {
	e->SRI_xPosition = (word) (e->SRI_xPosition + keep);
	e->SRI_stringWidth.WBF_int = (sword) (e->SRI_stringWidth.WBF_int -
					      keep);
	return TRUE;
    }
    l = ChunkArrayInsertAt(RunArray(j), e, SRI_SIZE(chars));
    e = Run(j, (word) (n + 1));
    memcpy(l, e, offsetof(StyleRunInfo, SRI_text) + chars);
    l->SRI_numChars = chars;
    l->SRI_stringWidth.WBF_int = keep;
    l->SRI_stringWidth.WBF_frac = 0;
    e->SRI_numChars = (word) (e->SRI_numChars - chars);
    e->SRI_xPosition = (word) (e->SRI_xPosition + keep);
    e->SRI_stringWidth.WBF_int = (sword) (e->SRI_stringWidth.WBF_int - keep);
    memmove(&e->SRI_text, &e->SRI_text + chars, e->SRI_numChars);
    ChunkArrayElementResize(RunArray(j), (word) (n + 1),
			    SRI_SIZE(e->SRI_numChars));
    return FALSE;
}

/* SendTextStrings: the strings on the current sheet to the driver; TRUE
 * on an error */
static Boolean SendTextStrings(SpoolJobInfo *j)
{
    PaperInputOptions pio;
    StyleRunInfo *e;
    DriverRegs r;
    PState *ps;
    Boolean err = FALSE;
    sword v;
    word n;

    ps = MemLock(j->SJI_pstate);
    pio = ps->PS_paperInput;
    MemUnlock(j->SJI_pstate);
    MemLock(j->SJI_tsHan);
    /* the count again each time: a cut adds a string */
    for (n = 0; n < ChunkArrayGetCount(RunArray(j)); n++) {
	e = Run(j, n);
	v = (sword) (e->SRI_yPosition + PAPER_INFO(j).PSR_margins.PCMP_top -
		     (word) j->SJI_textTileY);
	if (v < 0)
	    continue;
	/* below the sheet: the rest is too (on tractor paper, a tiled
	 * page goes on) */
	if ((!(j->SJI_printState & SPS_TILED) || !(pio & PIO_TRACTOR)) &&
	    (sword) (v - j->SJI_printHeight) >= 0)
	    break;
	v = (sword) (e->SRI_xPosition + PAPER_INFO(j).PSR_margins.PCMP_left -
		     j->SJI_textTileX);
	if (v < 0)
	    continue;
	v = (sword) (v - j->SJI_printWidth);
	if (v >= 0)
	    continue;
	v = (sword) (v + e->SRI_stringWidth.WBF_int);
	if (v >= 0 && SnipTextString(j, n, v))
	    continue;
	memset(&r, 0, sizeof(r));
	r.DR_dsSi = Run(j, n);
	r.DR_esDi = TS(j);
	r.DR_ax = j->SJI_textTileX;
	r.DR_cx = (word) j->SJI_textTileY;
	if (CallPrinter(j, DR_PRINT_STYLE_RUN, &r)) {
	    err = TRUE;
	    break;
	}
    }
    MemUnlock(j->SJI_tsHan);
    return err;
}

/* AskForNextTextPage: on manual feed the user puts in the next sheet */
static word AskForNextTextPage(SpoolJobInfo *j)
{
    PState *ps = MemLock(j->SJI_pstate);
    PaperInputOptions pio = ps->PS_paperInput;

    MemUnlock(j->SJI_pstate);
    if (!(pio & PIO_MANUAL))
	return SPOOL_IC_OK;
    return SpoolErrorBox(SERROR_MANUAL_PAPER_FEED, 0);
}

/* PrintTextPage: one page of the document, on as many sheets as it is
 * tiled over; the GSRetType of the page */
GSRetType PrintTextPage(SpoolJobInfo *j)
{
    DriverRegs r;
    GSRetType ret;

    j->SJI_gstring = GrLoadGString(j->SJI_fHan, GST_STREAM, 0);
    ret = GetTextStrings(j);
    GrDestroyGString(j->SJI_gstring, 0, GSKT_LEAVE_DATA);
    j->SJI_gstring = 0;

    j->SJI_curyPage = j->SJI_yPages;
    j->SJI_textTileY = PAPER_INFO(j).PSR_margins.PCMP_top;
    for (;;) {					/* down */
	j->SJI_curxPage = j->SJI_xPages;
	j->SJI_textTileX = PAPER_INFO(j).PSR_margins.PCMP_left;
	j->SJI_textTileXright = (word) (j->SJI_textTileX +
					j->SJI_printWidth);
	for (;;) {				/* across */
	    BumpPhysPageNumber(j);
	    if (ProcessStartPage(j) || SendTextStrings(j) ||
		ProcessEndPage(j, &ret, PEC_FORM_FEED)) {
		ret = GSRT_FAULT;
		goto done;
	    }
	    if (!(j->SJI_printState & SPS_FORM_FEED)) {
		/* the next page goes on below this one */
		memset(&r, 0, sizeof(r));
		r.DR_dx = j->SJI_printHeight;
		CallPrinter(j, DR_PRINT_SET_CURSOR, &r);
	    }
	    if ((sword) --j->SJI_curxPage <= 0)
		break;
	    if (AskForNextTextPage(j) == SPOOL_IC_DISMISS) {
		ret = GSRT_FAULT;
		goto done;
	    }
	    j->SJI_textTileX = (word) (j->SJI_textTileX + j->SJI_printWidth);
	    j->SJI_textTileXright = (word) (j->SJI_textTileXright +
					    j->SJI_printWidth);
	}
	if ((sword) --j->SJI_curyPage <= 0)
	    break;
	if (AskForNextTextPage(j) == SPOOL_IC_DISMISS) {
	    ret = GSRT_FAULT;
	    goto done;
	}
	j->SJI_textTileY += j->SJI_printHeight;
    }
done:
    InitTextStringsBlock(j);
    return ret;
}
