/*
 * graphicsTextObject.c -- what the text object needs of the kernel to
 * lay out its lines (after graphicsTextObject.asm: GrTextObjCalc,
 * GrTextPosition).
 *
 * GrTextObjCalc fits a field (the text up to a tab or the end of the
 * line) into an area: it adds the widths of the characters, run by run
 * (the style callback gives the attributes and the characters of each
 * run), with the kerning and the space padding, and finds where the
 * field breaks: at a tab, a null, a carriage return, a column or
 * section break (the line ends there), else at the last word start
 * that fits (word wrap), at a hyphen (hard, optional, or one the
 * hyphenation callback suggests), or at the character that overflowed.
 * The line's height and baseline grow with the tallest run in it (the
 * height callback hears of it).  A tab first in the field asks the tab
 * callback for the area.  An inline graphic (C_GRAPHIC) gets its size
 * from the graphic callback and rides on the baseline.
 *
 * GrTextPosition finds the character nearest a position (pixels from
 * the start of the field): the one whose middle is passed.
 *
 * Port32: the structures are C ones (graphics.h); the callbacks are C
 * functions with the structure; widths are kept in 1/256 internally.
 * Not yet: kinsoku (PZ_PCGEOS), DBCS, character justification.
 */
#include "graphicsInt.h"
#include <Internal/fontDr.h>
#include <char.h>
#include <Objects/Text/tCommon.h>
#include <string.h>

/* char.def: the breaks a line ends at */
#define C_SECTION_BREAK	C_CTRL_K
#define C_COLUMN_BREAK	0x0c		/* C_PAGE_BREAK */

/* WBFixed as 1/256 and back */
static sdword W(const WBFixed *w)
{
    return ((sdword) w->WBF_int << 8) + w->WBF_frac;
}

static void SetW(WBFixed *w, sdword v)
{
    w->WBF_int = (word) (v >> 8);
    w->WBF_frac = (byte) v;
}

/* rndwbf: rounded to a word */
static word Rnd(sdword v)
{
    return (word) ((v + 0x80) >> 8);
}

/* what is kept while the style is current: the gstate locked (its
 * font) by InitForTextMetrics, unlocked by UnlockFontAndGState */
typedef struct {
    TextMetricStyles	*tms;
    GState		*gs;
    FontBuf		*fb;
    byte		kern;		/* the kern char (previous, same run) */
} Metrics;

static void LockFont(Metrics *m)
{
    MemUnlock(m->tms->TMS_gstateHandle);
    m->fb = FontDrLockFont(m->tms->TMS_gstateHandle);
    m->gs = MemLock(m->tms->TMS_gstateHandle);
}

static void UnlockFont(Metrics *m)
{
    if (m->fb) {
	MemUnlock(m->tms->TMS_gstateHandle);
	FontDrUnlockFont(m->tms->TMS_gstateHandle);
	m->gs = MemLock(m->tms->TMS_gstateHandle);
	m->fb = NULL;
    }
}

/* InitForTextMetrics: the structure cleared from TMS_sizeSoFar on, the
 * gstate locked, no font yet, the gstate's space padding */
static void InitForTextMetrics(Metrics *m, TextMetricStyles *tms,
			       GStateHandle gstate, word size)
{
    memset(&tms->TMS_sizeSoFar, 0,
	   size - offsetof(TextMetricStyles, TMS_sizeSoFar));
    memset(m, 0, sizeof(*m));
    m->tms = tms;
    tms->TMS_gstateHandle = gstate;
    m->gs = MemLock(gstate);
    tms->TMS_textAttr.TA_font = FID_INVALID;
    tms->TMS_textAttr.TA_spacePad = m->gs->GS_textSpacePad;
}

static void UnlockFontAndGState(Metrics *m)
{
    UnlockFont(m);
    MemUnlock(m->tms->TMS_gstateHandle);
}

/*
 * CallStyleCallBack: the attributes and characters of the run at
 * offset; the gstate set to them, their font locked; the gstate's text
 * mode (with the kerning the font has).  Returns the count.
 */
static word CallStyleCallBack(Metrics *m, word offset, const char **text)
{
    TextMetricStyles *tms = m->tms;
    word n;
    GState *gs;

    MemUnlock(tms->TMS_gstateHandle);
    n = tms->TMS_styleCallBack(tms, offset, text);
    gs = m->gs = MemLock(tms->TMS_gstateHandle);
    gs->GS_trackKernDegree = tms->TMS_textAttr.TA_trackKern;
    gs->GS_fontAttr.FCA_textStyle = tms->TMS_textAttr.TA_styleSet;
    gs->GS_fontAttr.FCA_fontID = tms->TMS_textAttr.TA_font;
    gs->GS_fontAttr.FCA_pointsize = tms->TMS_textAttr.TA_size;
    gs->GS_fontAttr.FCA_weight = tms->TMS_textAttr.TA_fontWeight;
    gs->GS_fontAttr.FCA_width = tms->TMS_textAttr.TA_fontWidth;
    UnlockFont(m);
    GrInvalidateFont(m->gs);
    LockFont(m);
    gs = m->gs;
    tms->TMS_trackKernValue = gs->GS_trackKernValue;
    tms->TMS_textAttr.TA_modeSet = gs->GS_textMode;
    return n;
}

/*
 * JustAddCharWidth: the character's width (the default character for
 * one the font lacks) added to extra (kerning, padding: 1/256) and to
 * the size so far unless negative; whether it reaches above or below
 * the font's box.  Returns the width and the character's flags.
 */
static sdword JustAddCharWidth(Metrics *m, byte ch, sdword extra,
			       CharTableFlags *flags)
{
    TextMetricStyles *tms = m->tms;
    const CharTableEntry *cte;
    sdword w;

    *flags = 0;
    tms->TMS_flags &= ~(TMSF_EXTENDS_ABOVE | TMSF_EXTENDS_BELOW);
    if (!m->fb)
	return 0;
    cte = GrCharEntry(m->fb, ch);
    w = extra + W(&cte->CTE_width);
    if (w < 0)
	w = 0;		/* isBackwardsMove: no width */
    SetW(&tms->TMS_sizeSoFar, W(&tms->TMS_sizeSoFar) + w);
    *flags = cte->CTE_flags;
    if (cte->CTE_flags & CTF_ABOVE_ASCENT)
	tms->TMS_flags |= TMSF_EXTENDS_ABOVE;
    if (cte->CTE_flags & CTF_BELOW_DESCENT)
	tms->TMS_flags |= TMSF_EXTENDS_BELOW;
    return w;
}

/* DoCallback for a graphic: its height, *width */
static word GraphicCallback(Metrics *m, word offset, word *width)
{
    TextMetricStyles *tms = m->tms;
    word h;

    UnlockFont(m);
    MemUnlock(tms->TMS_gstateHandle);
    *width = 0;
    h = tms->TMS_graphicCallBack ?
	tms->TMS_graphicCallBack(tms, offset, width) : 0;
    m->gs = MemLock(tms->TMS_gstateHandle);
    LockFont(m);
    return h;
}

/*
 * AddWidth: a character added to the size so far (only that with
 * TMSF_UPDATE_SIZE_ONLY), its width the last character's.  A space
 * gets the padding (TM_PAD_SPACES, TMSF_PAD_SPACES); an optional
 * hyphen counts as a hyphen only last (left == 1) or before the null
 * and when drawn (TMSF_OPT_HYPHENS, TM_DRAW_OPTIONAL_HYPHENS), else
 * not at all; a graphic is as wide as the callback says and may make
 * the run taller; kerning with the previous character of the run.
 */
static void AddWidth(Metrics *m, byte ch, word left, const char *next,
		     word offset)
{
    TextMetricStyles *tms = m->tms;
    TextMode mode = tms->TMS_textAttr.TA_modeSet;
    sdword w = 0, k;
    CharTableFlags f;

    tms->TMS_flags &= ~TMSF_NEGATIVE_KERNING;
    if (ch <= C_SPACE) {
	if (ch == C_SPACE && (mode & TM_PAD_SPACES) &&
	    (tms->TMS_flags & TMSF_PAD_SPACES))
	    w += W(&tms->TMS_textAttr.TA_spacePad);
	if (ch == C_OPTHYPHEN && (tms->TMS_flags & TMSF_OPT_HYPHENS)) {
	    if (left != 1 && *next)
		return;			/* ignored */
	    if (mode & TM_DRAW_OPTIONAL_HYPHENS)
		ch = C_HYPHEN;
	} else if (ch == C_GRAPHIC) {
	    word gw, gh = GraphicCallback(m, offset, &gw);

	    /* the graphic rides on the baseline */
	    if (gh > tms->TMS_styleBaseline.WBF_int) {
		tms->TMS_styleHeight.WBF_int += (word) (gh -
				tms->TMS_styleBaseline.WBF_int);
		tms->TMS_styleBaseline.WBF_int = gh;
		tms->TMS_styleBaseline.WBF_frac = 0;
		tms->TMS_flags |= TMSF_STYLE_CHANGED;
	    }
	    w = (sdword) gw << 8;
	    SetW(&tms->TMS_sizeSoFar, W(&tms->TMS_sizeSoFar) + w);
	    SetW(&tms->TMS_lastCharWidth, w);
	    return;
	}
    }
    if (mode & (TM_PAIR_KERN | TM_TRACK_KERN)) {
	k = 0;
	if ((mode & TM_PAIR_KERN) && m->fb)
	    k += GrPairKern(m->fb, m->kern, ch);
	if (mode & TM_TRACK_KERN)
	    k += ((sdword) tms->TMS_trackKernValue.BBF_int * 256) +
		 tms->TMS_trackKernValue.BBF_frac;
	w += k;
	if (w < 0)
	    tms->TMS_flags |= TMSF_NEGATIVE_KERNING;
    }
    w = JustAddCharWidth(m, ch, w, &f);
    if (f & CTF_NEGATIVE_LSB)
	tms->TMS_flags |= TMSF_NEGATIVE_KERNING;
    if (!(tms->TMS_flags & TMSF_UPDATE_SIZE_ONLY))
	SetW(&tms->TMS_lastCharWidth, w);
}

/* ---------------------------------------------------------------------
 * GrTextObjCalc
 * ------------------------------------------------------------------- */

#define INT(v)	((v)->TOCV_int)
#define EXT(v)	((v)->TOCV_ext)

static sdword Descent(TOC_vars *v)
{
    return W(&INT(v).TOCI_currentHgt) - W(&INT(v).TOCI_currentBlo);
}

static void HeightCallback(Metrics *m, TOC_vars *v, sdword hgt)
{
    WBFixed h;

    if (!EXT(v).TOCE_heightCallback)
	return;
    SetW(&h, hgt);
    MemUnlock(m->tms->TMS_gstateHandle);
    EXT(v).TOCE_heightCallback(v, h);
    m->gs = MemLock(m->tms->TMS_gstateHandle);
}

/* SetHeightAndBaseline: the run's height and baseline from its font;
 * the height callback told if the field would grow taller */
static void SetHeightAndBaseline(Metrics *m, TOC_vars *v)
{
    TextMetricStyles *tms = m->tms;
    sdword h = 0, b = 0, desc, asc;

    if (m->fb) {
	h = W(&m->fb->FB_height) + W(&m->fb->FB_heightAdjust);
	b = W(&m->fb->FB_baselinePos) + W(&m->fb->FB_baseAdjust);
    }
    SetW(&tms->TMS_styleHeight, h);
    SetW(&tms->TMS_styleBaseline, b);
    desc = h - b;
    if (Descent(v) > desc)
	desc = Descent(v);
    asc = b;
    if (W(&INT(v).TOCI_currentBlo) > asc)
	asc = W(&INT(v).TOCI_currentBlo);
    if (desc + asc > W(&INT(v).TOCI_currentHgt))
	HeightCallback(m, v, desc + asc);
}

/* UpdateFieldHeightVars: the field as tall as the run (its descent and
 * ascent the larger ones); the height callback told of a change */
static void UpdateFieldHeightVars(Metrics *m, TOC_vars *v)
{
    TextMetricStyles *tms = m->tms;
    sdword h = W(&tms->TMS_styleHeight), b = W(&tms->TMS_styleBaseline);
    sdword desc = h - b, cur;
    Boolean changed = FALSE;

    cur = Descent(v);
    if (desc > cur) {
	SetW(&INT(v).TOCI_currentHgt, W(&INT(v).TOCI_currentHgt) +
	     desc - cur);
	changed = TRUE;
    }
    cur = W(&INT(v).TOCI_currentBlo);
    if (b > cur) {
	SetW(&INT(v).TOCI_currentBlo, b);
	SetW(&INT(v).TOCI_currentHgt, W(&INT(v).TOCI_currentHgt) +
	     b - cur);
	changed = TRUE;
    }
    if (changed)
	HeightCallback(m, v, W(&INT(v).TOCI_currentHgt));
    tms->TMS_flags &= ~TMSF_STYLE_CHANGED;
}

/* CopyFieldHeightVars: the line as tall as the field (descent and
 * ascent); whether a break here comes after a kerned character */
static void CopyFieldHeightVars(TOC_vars *v)
{
    sdword desc = Descent(v), asc = W(&INT(v).TOCI_currentBlo);
    sdword ldesc = W(&EXT(v).TOCE_lineHeight) - W(&EXT(v).TOCE_lineBLO);
    sdword lasc = W(&EXT(v).TOCE_lineBLO);

    if (desc > ldesc)
	SetW(&EXT(v).TOCE_lineHeight, W(&EXT(v).TOCE_lineHeight) +
	     desc - ldesc);
    if (asc > lasc) {
	SetW(&EXT(v).TOCE_lineBLO, asc);
	SetW(&EXT(v).TOCE_lineHeight, W(&EXT(v).TOCE_lineHeight) +
	     asc - lasc);
    }
    EXT(v).TOCE_otherFlags &= ~TOCOF_LAST_BREAK_KERNED;
    if (v->TOCV_int.TOCI_style.TMS_flags & TMSF_NEGATIVE_KERNING)
	EXT(v).TOCE_otherFlags |= TOCOF_LAST_BREAK_KERNED;
}

/*
 * CheckSoftHyphen: would the line end in the optional hyphen at di
 * (expanded) fit?  If so it is the last hyphen; else noted as too wide.
 * The size so far is kept.  TRUE if it fits.
 */
static Boolean CheckSoftHyphen(Metrics *m, TOC_vars *v, word di)
{
    TextMetricStyles *tms = m->tms;
    WBFixed size = tms->TMS_sizeSoFar, last = tms->TMS_lastCharWidth;
    byte kern = m->kern;
    Boolean fits;

    tms->TMS_flags |= TMSF_UPDATE_SIZE_ONLY;
    m->kern = 0;
    AddWidth(m, C_HYPHEN, 2, "", di);
    m->kern = kern;
    fits = !di || (sword) tms->TMS_sizeSoFar.WBF_int <
		  EXT(v).TOCE_areaToFill;
    if (fits) {
	INT(v).TOCI_lastHyphenPos = tms->TMS_sizeSoFar;
	INT(v).TOCI_lastHyphen = (word) (di + 1);
	tms->TMS_flags |= TMSF_IS_BREAK_CHARACTER | TMSF_IS_OPTIONAL_HYPHEN;
	EXT(v).TOCE_flags &= ~TOCF_OPT_HYPHEN_TOO_WIDE;
    } else
	EXT(v).TOCE_flags |= TOCF_OPT_HYPHEN_TOO_WIDE;
    tms->TMS_flags &= ~TMSF_UPDATE_SIZE_ONLY;
    tms->TMS_lastCharWidth = last;
    tms->TMS_sizeSoFar = size;
    return fits;
}

/* where the field breaks */
typedef struct {
    sdword	width;		/* bx.al: the field's width */
    word	just;		/* cx: its width for justification */
    word	flags;		/* dx: LineFlags */
    word	di;		/* the offset of the break */
    word	spaces;		/* si: spaces to add to the paddable */
} Break;

static void CheckBreakKerned(TOC_vars *v, Break *b)
{
    if (EXT(v).TOCE_otherFlags & TOCOF_LAST_BREAK_KERNED)
	b->flags |= LF_LAST_CHAR_KERNED;
}

static void PrevCharKerned(TOC_vars *v, Break *b)
{
    if (EXT(v).TOCE_otherFlags & TOCOF_PREV_CHAR_KERNED)
	b->flags |= LF_LAST_CHAR_KERNED;
}

static sdword PosBeforeLastChar(TextMetricStyles *tms)
{
    return W(&tms->TMS_sizeSoFar) - W(&tms->TMS_lastCharWidth);
}

/* BreakAtNoWordWrap: at the character that overflowed */
static void BreakAtNoWordWrap(TOC_vars *v, Break *b)
{
    TextMetricStyles *tms = &INT(v).TOCI_style;

    b->spaces = (word) EXT(v).TOCE_nExtraSpaces;
    b->width = PosBeforeLastChar(tms);
    b->just = Rnd(b->width);
    b->flags = 0;
    CopyFieldHeightVars(v);
    PrevCharKerned(v, b);
}

static void BreakAtWordWrapOrHyphen(Metrics *m, TOC_vars *v, Break *b)
{
    TextMetricStyles *tms = m->tms;

    if (EXT(v).TOCE_otherFlags & TOCOF_FIRST_CHAR_OVERFLOW) {
	/* BreakAtOneWideChar */
	b->spaces = (word) EXT(v).TOCE_nExtraSpaces;
	b->width = PosBeforeLastChar(tms);
	b->just = Rnd(b->width);
	b->flags = 0;
	UpdateFieldHeightVars(m, v);
	CopyFieldHeightVars(v);
	CheckBreakKerned(v, b);
	return;
    }
    if (EXT(v).TOCE_flags & TOCF_NO_WORD_WRAP) {
	BreakAtNoWordWrap(v, b);
	return;
    }
    if ((EXT(v).TOCE_flags & TOCF_AUTO_HYPHENATE) &&
	EXT(v).TOCE_hyphenCallback) {
	UnlockFont(m);
	MemUnlock(tms->TMS_gstateHandle);
	EXT(v).TOCE_hyphenCallback(v, b->di);
	m->gs = MemLock(tms->TMS_gstateHandle);
	LockFont(m);
    }
    if (INT(v).TOCI_lastHyphen || INT(v).TOCI_suggestedHyphen) {
	/* BreakAtHyphen: the later of the two */
	b->flags = 0;
	if (INT(v).TOCI_lastHyphen && (!INT(v).TOCI_suggestedHyphen ||
	    INT(v).TOCI_suggestedHyphen <= INT(v).TOCI_lastHyphen)) {
	    if (tms->TMS_flags & TMSF_IS_OPTIONAL_HYPHEN)
		b->flags |= LF_ENDS_IN_OPTIONAL_HYPHEN;
	    b->di = INT(v).TOCI_lastHyphen;
	    b->spaces = (word) EXT(v).TOCE_nExtraSpaces;
	    b->width = W(&INT(v).TOCI_lastHyphenPos);
	    b->just = Rnd(b->width);
	    EXT(v).TOCE_nExtraSpaces = 0;
	    CheckBreakKerned(v, b);
	    return;
	}
	b->di = INT(v).TOCI_suggestedHyphen;
	b->spaces = (word) EXT(v).TOCE_nExtraSpaces;
	b->flags |= LF_ENDS_IN_AUTO_HYPHEN;
	b->width = W(&INT(v).TOCI_suggestedHyphenPos);
	b->just = Rnd(b->width + W(&EXT(v).TOCE_hyphenWidth));
	EXT(v).TOCE_nExtraSpaces = 0;
	CopyFieldHeightVars(v);
	CheckBreakKerned(v, b);
	return;
    }
    /* BreakAtWordWrap: at the start of the last word (one word only:
     * as without word wrap) */
    if (!INT(v).TOCI_lastWordStart) {
	BreakAtNoWordWrap(v, b);
	return;
    }
    b->di = INT(v).TOCI_lastWordStart;
    b->spaces = 0;
    b->width = W(&INT(v).TOCI_lastWordPos);
    b->just = INT(v).TOCI_lastWordEndPos;
    b->flags = 0;
    CheckBreakKerned(v, b);
}

/* BreakAtLineEndChar: after the terminator (add characters); an
 * optional hyphen before it is drawn if it fits, else back to a word
 * break */
static void BreakAtLineEndChar(Metrics *m, TOC_vars *v, Break *b,
			       word add, byte prev)
{
    TextMetricStyles *tms = m->tms;

    b->di = (word) (b->di + add);
    if (prev == C_OPTHYPHEN) {
	if (EXT(v).TOCE_flags & TOCF_OPT_HYPHEN_TOO_WIDE) {
	    b->di = (word) (b->di - add);
	    BreakAtWordWrapOrHyphen(m, v, b);
	    return;
	}
	b->flags |= LF_ENDS_IN_OPTIONAL_HYPHEN;
	b->width = W(&INT(v).TOCI_lastHyphenPos);
    } else
	b->width = W(&tms->TMS_sizeSoFar);
    b->just = Rnd(b->width);
    b->spaces = (word) EXT(v).TOCE_nExtraSpaces;
    CopyFieldHeightVars(v);
    PrevCharKerned(v, b);
}

/*
 * FindLineBreak: the break for the character that ended the loop (ch,
 * after prev); the results stored
 */
static void FindLineBreak(Metrics *m, TOC_vars *v, byte ch, byte prev,
			  word di)
{
    TextMetricStyles *tms = m->tms;
    Break b;
    CharTableFlags f;

    memset(&b, 0, sizeof(b));
    b.di = di;
    if (ch == C_TAB) {
	if (EXT(v).TOCE_flags & TOCF_ONE_TAB_TOO_LARGE) {
	    /* only a tab, and too wide */
	} else {
	    b.width = W(&tms->TMS_sizeSoFar);
	    b.just = Rnd(b.width);
	    b.spaces = (word) EXT(v).TOCE_nExtraSpaces;
	    CopyFieldHeightVars(v);
	    CheckBreakKerned(v, &b);
	}
    } else {
	EXT(v).TOCE_flags |= TOCF_LINE_TERMINATED;
	switch (ch) {
	case C_NULL:
	    b.flags = LF_ENDS_PARAGRAPH | LF_ENDS_IN_NULL;
	    BreakAtLineEndChar(m, v, &b, 0, prev);
	    break;
	case C_COLUMN_BREAK:
	    b.flags = LF_ENDS_PARAGRAPH | LF_ENDS_IN_COLUMN_BREAK;
	    BreakAtLineEndChar(m, v, &b, 1, prev);
	    break;
	case C_SECTION_BREAK:
	    b.flags = LF_ENDS_PARAGRAPH | LF_ENDS_IN_SECTION_BREAK;
	    BreakAtLineEndChar(m, v, &b, 1, prev);
	    break;
	case C_CR:
	    b.flags = LF_ENDS_PARAGRAPH | LF_ENDS_IN_CR;
	    BreakAtLineEndChar(m, v, &b, 1, prev);
	    /* the field as wide as with a paragraph mark */
	    (void) JustAddCharWidth(m, C_PARAGRAPH, 0, &f);
	    b.width = W(&tms->TMS_sizeSoFar);
	    break;
	default:
	    BreakAtWordWrapOrHyphen(m, v, &b);
	    break;
	}
    }
    SetW(&EXT(v).TOCE_fieldWidth, b.width);
    EXT(v).TOCE_justWidth = (sword) b.just;
    EXT(v).TOCE_lineFlags |= b.flags;
    EXT(v).TOCE_nChars = (sword) b.di;
    EXT(v).TOCE_nSpaces = (sword) (EXT(v).TOCE_nSpaces + b.spaces);
}

void GrTextObjCalc(GStateHandle gstate, TOC_vars *v)
{
    Metrics m;
    TextMetricStyles *tms = &INT(v).TOCI_style;
    const char *s = NULL;
    word n = 0, di = 0;
    byte ch = 0, prev = 0;
    sword size;

    InitForTextMetrics(&m, tms, gstate, sizeof(TOC_int));
    EXT(v).TOCE_nSpaces = 0;
    EXT(v).TOCE_nExtraSpaces = 0;
    tms->TMS_flags = 0;
    INT(v).TOCI_lastMetricFlags = 0;
    INT(v).TOCI_lastWordStart = 0;
    INT(v).TOCI_tallCharBaselinePos = 0xffff;
    INT(v).TOCI_tallCharHeightPos = 0xffff;
    for (;;) {
	if (!n) {
	    /* the next run: its attributes, the height */
	    n = CallStyleCallBack(&m, di, &s);
	    SetHeightAndBaseline(&m, v);
	    tms->TMS_flags |= TMSF_STYLE_CHANGED;
	    if (!di)
		UpdateFieldHeightVars(&m, v);
	    if (!di && n && (byte) *s == C_TAB) {
		/* a tab first in the field: the area from the tab */
		Boolean none;

		MemUnlock(gstate);
		none = EXT(v).TOCE_tabCallback ?
		       EXT(v).TOCE_tabCallback(v, s) : FALSE;
		m.gs = MemLock(gstate);
		if (none) {
		    EXT(v).TOCE_flags |= TOCF_ONE_TAB_TOO_LARGE;
		    ch = C_TAB;
		    break;
		}
		CopyFieldHeightVars(v);
		n--;
		ch = (byte) *s++;
		goto nextChar;
	    }
	    m.kern = 0;		/* no kerning across runs */
	}
	tms->TMS_flags &= ~TMSF_IS_BREAK_CHARACTER;
	ch = s ? (byte) *s++ : 0;
	if (!n)
	    break;		/* out of text */
	n--;
	if (ch < C_SPACE) {
	    if (ch == C_OPTHYPHEN && CheckSoftHyphen(&m, v, di))
		goto nextChar;
	    if (ch == C_NULL || ch == C_CR || ch == C_TAB ||
		ch == C_SECTION_BREAK || ch == C_COLUMN_BREAK)
		break;
	}
	AddWidth(&m, ch, (word) (n + 1), s, di);
	if (tms->TMS_flags & TMSF_EXTENDS_ABOVE)
	    EXT(v).TOCE_lineFlags |= LF_INTERACTS_ABOVE;
	if (tms->TMS_flags & TMSF_EXTENDS_BELOW)
	    EXT(v).TOCE_lineFlags |= LF_INTERACTS_BELOW;
	if (ch != C_SPACE) {
	    /* a word start: a space or a tab before */
	    if (!(INT(v).TOCI_lastMetricFlags & TMSF_NOWRAP) &&
		(prev == C_SPACE || prev == C_TAB)) {
		INT(v).TOCI_lastWordStart = di;
		SetW(&INT(v).TOCI_lastWordPos, PosBeforeLastChar(tms));
	    }
	} else {
	    EXT(v).TOCE_nExtraSpaces++;
	    tms->TMS_flags |= TMSF_IS_BREAK_CHARACTER;
	    if (prev != C_SPACE && !(tms->TMS_flags & TMSF_NOWRAP)) {
		/* a word end: the whole word fits, its spaces count */
		INT(v).TOCI_lastHyphen = 0;
		EXT(v).TOCE_nSpaces = (sword) (EXT(v).TOCE_nSpaces +
					EXT(v).TOCE_nExtraSpaces - 1);
		EXT(v).TOCE_nExtraSpaces = 1;
		INT(v).TOCI_lastWordEndPos = Rnd(PosBeforeLastChar(tms));
		if (EXT(v).TOCE_otherFlags & TOCOF_WAIT_FOR_WORD) {
		    EXT(v).TOCE_otherFlags &= ~TOCOF_WAIT_FOR_WORD;
		    EXT(v).TOCE_otherFlags |= TOCOF_FOUND_WORD;
		}
	    }
	}
	/* the overflow (trailing spaces of a wrapped line take no room) */
	if ((EXT(v).TOCE_flags & TOCF_NO_WORD_WRAP) || ch != C_SPACE) {
	    size = (sword) tms->TMS_sizeSoFar.WBF_int;
	    if ((word) size >= (word) EXT(v).TOCE_areaToFill) {
		if (EXT(v).TOCE_otherFlags & TOCOF_WRAP_AFTER_OVERFLOW) {
		    if (!(tms->TMS_flags & TMSF_NOWRAP)) {
			if (ch == C_GRAPHIC)
			    goto notWrappingAfterOverflow;
			if (EXT(v).TOCE_wrapAfterOverflowWidth &&
			    (word) size >=
			    EXT(v).TOCE_wrapAfterOverflowWidth)
			    break;
		    }
		    if (W(&INT(v).TOCI_lastWordPos))
			break;
		    if (!(EXT(v).TOCE_otherFlags & TOCOF_WAIT_FOR_WORD)) {
			if (EXT(v).TOCE_otherFlags & TOCOF_FOUND_WORD) {
			    EXT(v).TOCE_otherFlags &= ~TOCOF_FOUND_WORD;
			    break;
			}
			EXT(v).TOCE_otherFlags |= TOCOF_WAIT_FOR_WORD;
		    }
		} else {
notWrappingAfterOverflow:
		    /* a single character too wide for the first field
		     * of the line: kept, with the spaces after it */
		    if ((EXT(v).TOCE_otherFlags &
			 TOCOF_FIRST_CHAR_OVERFLOW) || di ||
			!(EXT(v).TOCE_otherFlags & TOCOF_IS_FIRST_FIELD))
			break;
		    EXT(v).TOCE_otherFlags |= TOCOF_FIRST_CHAR_OVERFLOW;
		}
	    }
	}
	/* the anchor character */
	if (ch == (byte) EXT(v).TOCE_anchorChar &&
	    !(EXT(v).TOCE_flags & TOCF_FOUND_ANCHOR)) {
	    EXT(v).TOCE_flags |= TOCF_FOUND_ANCHOR;
	    EXT(v).TOCE_widthToAnchor = (sword) tms->TMS_sizeSoFar.WBF_int;
	}
	/* a hard hyphen: a break */
	if (ch == C_ENDASH || ch == C_EMDASH || ch == C_HYPHEN) {
	    INT(v).TOCI_lastHyphen = (word) (di + 1);
	    INT(v).TOCI_lastHyphenPos = tms->TMS_sizeSoFar;
	    tms->TMS_flags &= ~TMSF_IS_OPTIONAL_HYPHEN;
	    tms->TMS_flags |= TMSF_IS_BREAK_CHARACTER;
	}
nextChar:
	/* the character fit */
	if (tms->TMS_flags & TMSF_STYLE_CHANGED)
	    UpdateFieldHeightVars(&m, v);
	if (tms->TMS_flags & TMSF_IS_BREAK_CHARACTER)
	    CopyFieldHeightVars(v);
	EXT(v).TOCE_otherFlags &= ~TOCOF_PREV_CHAR_KERNED;
	if (tms->TMS_flags & TMSF_NEGATIVE_KERNING)
	    EXT(v).TOCE_otherFlags |= TOCOF_PREV_CHAR_KERNED;
	prev = ch;
	m.kern = ch;
	INT(v).TOCI_lastMetricFlags = tms->TMS_flags;
	di++;
    }
    FindLineBreak(&m, v, ch, prev, di);
    if (!(EXT(v).TOCE_flags & TOCF_FOUND_ANCHOR))
	EXT(v).TOCE_widthToAnchor = EXT(v).TOCE_justWidth;
    UnlockFontAndGState(&m);
}

/* ---------------------------------------------------------------------
 * GrTextPosition
 * ------------------------------------------------------------------- */

/*
 * GrTextPosition: of up to nChars characters of the field, the one
 * nearest the position (pixels from the field's start; a character is
 * passed when its middle is); *pos the position of its left edge
 * (rounded down).  Stops at a null, a carriage return or a tab (then
 * the count and the width of the characters before).  Returns the
 * character's offset.
 */
word GrTextPosition(GStateHandle gstate, GTP_vars *v, word nChars,
		    word pixel, word *pos)
{
    Metrics m;
    TextMetricStyles *tms = &v->GTPL_style;
    const char *s = NULL;
    word n = 0, left = nChars;
    sdword over, lcw;
    byte ch;

    *pos = 0;
    if (!nChars)
	return 0;
    InitForTextMetrics(&m, tms, gstate, sizeof(TextMetricStyles));
    v->GTPL_charCount = 0;
    v->GTPL_startPosition = pixel;
    tms->TMS_flags = TMSF_OPT_HYPHENS | TMSF_PAD_SPACES;
    for (;;) {
	if (!n) {
	    n = CallStyleCallBack(&m, v->GTPL_charCount, &s);
	    m.kern = 0;
	    if (n > left)
		n = left;
	}
	ch = n ? (byte) *s : 0;
	if (!ch || ch == C_CR || ch == C_TAB)
	    break;
	s++;
	v->GTPL_charCount++;
	AddWidth(&m, ch, n == left ? n : 2, s,
		 (word) (v->GTPL_charCount - 1));
	if ((word) tms->TMS_sizeSoFar.WBF_int >= pixel) {
	    /* past it: the nearer of this character's edges */
	    over = W(&tms->TMS_sizeSoFar) - ((sdword) pixel << 8);
	    lcw = W(&tms->TMS_lastCharWidth);
	    if (lcw - over <= (lcw >> 1)) {
		/* before its middle: this character, its left edge */
		v->GTPL_charCount--;
		over -= lcw;
	    }
	    /* checkRounding: rounded down */
	    *pos = (word) ((sdword) pixel + (over >> 8));
	    UnlockFontAndGState(&m);
	    return v->GTPL_charCount;
	}
	m.kern = ch;
	n--;
	if (!--left)
	    break;
    }
    *pos = (word) tms->TMS_sizeSoFar.WBF_int;
    UnlockFontAndGState(&m);
    return v->GTPL_charCount;
}
