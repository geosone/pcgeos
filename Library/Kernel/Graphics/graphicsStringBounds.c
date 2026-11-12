/*
 * graphicsStringBounds.c -- the bounds of a graphics string (after
 * graphicsString.asm: GrGetGStringBounds, GrGetGStringBoundsDWord,
 * UnTransRotBounds, the figBounds parts of the PE* play routines and the
 * Bound* routines).
 *
 * The elements are played, from the string's current position and
 * until the GSControl flags stop (GSC_PARTIAL ignored), into a GState
 * without a window that starts with the transformation, the pen, the
 * line width and the font of the GState passed (if any).  Before each
 * output element plays, its extent in page coordinates (that GState's
 * transformation) goes into the bounds:
 *   - the points the path code makes of it (GrElementBounds: lines,
 *     rectangles, polygons, ellipses and arcs, rounded rectangles,
 *     curves and splines, text as the outlines of its characters), the
 *     ones drawn with a line widened by half the line width;
 *   - text whose font has no outlines: the font's box (left at the
 *     position, the text's width, the font's height);
 *   - a point, GR_SET_GSTRING_BOUNDS, a bitmap (its size): the
 *     rectangle.
 * The bounds go back through the transformation at the start, the
 * corners of a rotated result too, rounded.  An empty string gives
 * (0, 0, 0, 0).
 *
 * Differences from the asm: text is bounded by the ink of outline fonts
 * (the asm's GrGetTextBounds used the character metrics) and by the
 * font's box otherwise; paths are bounded by their elements as they are
 * defined (GR_DRAW_PATH and GR_FILL_PATH add nothing, the asm asked
 * GrGetPathBounds too).  A text field is bounded run by run, as the
 * text.  The *_PTR and *_OPTR elements come as the elements they stand
 * for (graphicsString.c).
 */
#include "graphicsInt.h"
#include <gstring.h>
#include <stdlib.h>

typedef struct {
    GStateHandle	BS_gstate;	/* without a window */
    Boolean		BS_any;
    int64_t		BS_l, BS_t, BS_r, BS_b;	/* page, 1/65536 */
} BoundState;

static word U(const byte *p)
{
    return (word) (p[0] | (p[1] << 8));
}

static sword S(const byte *p)
{
    return (sword) U(p);
}

static void Add(BoundState *b, int64_t x, int64_t y)
{
    if (!b->BS_any) {
	b->BS_l = b->BS_r = x;
	b->BS_t = b->BS_b = y;
	b->BS_any = TRUE;
	return;
    }
    if (x < b->BS_l) b->BS_l = x;
    if (x > b->BS_r) b->BS_r = x;
    if (y < b->BS_t) b->BS_t = y;
    if (y > b->BS_b) b->BS_b = y;
}

/* BoundFillRect: the four corners of a document rectangle (1/65536) */
static void AddDocRect(BoundState *b, const TMatrix *m, int64_t l, int64_t t,
		       int64_t r, int64_t bt)
{
    int64_t xs[4], ys[4];
    int i;

    xs[0] = xs[3] = l;
    xs[1] = xs[2] = r;
    ys[0] = ys[1] = t;
    ys[2] = ys[3] = bt;
    for (i = 0; i < 4; i++) {
	GrMatTransDWF(m, &xs[i], &ys[i]);
	Add(b, xs[i], ys[i]);
    }
}

/* where an element is: at x, y, or at the pen (the "CP" ones) */
static void At(const GState *gs, const byte *e, word off, Boolean cp,
	       int64_t *x, int64_t *y)
{
    if (cp) {
	*x = GrDWF(&gs->GS_penPos.PDF_x);
	*y = GrDWF(&gs->GS_penPos.PDF_y);
    } else {
	*x = (int64_t) S(e + off) * 65536;
	*y = (int64_t) S(e + off + 2) * 65536;
    }
}

static void BitmapBox(BoundState *b, const GState *gs, const TMatrix *m,
		   const byte *e, word size)
{
    Boolean cp = (Boolean) (*e == GR_DRAW_BITMAP_CP ||
			    *e == GR_FILL_BITMAP_CP);
    word off = (word) (cp ? 3 : 7);
    int64_t x, y;
    void *bm;
    XYValueAsDWord sz;

    if (size <= off || !(bm = malloc(size - off)))
	return;
    memcpy(bm, e + off, size - off);	/* aligned */
    sz = GrGetBitmapSize(bm);
    free(bm);
    At(gs, e, 1, cp, &x, &y);
    AddDocRect(b, m, x, y, x + (int64_t) (word) sz * 65536,
	       y + (int64_t) (word) (sz >> 16) * 65536);
}

/* a text field without outlines: each run's box with its attributes */
static void FieldBox(BoundState *b, const TMatrix *m, const byte *e)
{
    GDF_saved f;
    const byte *r = e + GR_FIELD_HEAD;
    int64_t x, y, box[4];
    word left;

    GrFieldDecode(e + 1, &f);
    x = GrWBF64(&f.GDFS_drawPos.PWBF_x);
    y = GrWBF64(&f.GDFS_drawPos.PWBF_y) + GrWBF64(&f.GDFS_baseline);
    for (left = f.GDFS_nChars; left; ) {
	TextAttr ta;
	word n = U(r);
	GrTextAttrDecode(r + 2, &ta);
	GrSetTextAttrLow(b->BS_gstate, &ta);
	if (n && GrTextBox(b->BS_gstate, x, y, (const char *) r + GR_FIELD_RUN,
			   n, box)) {
	    AddDocRect(b, m, box[0], box[1], box[2], box[3]);
	    x = box[2];
	}
	r += GR_FIELD_RUN + n;
	left = (word) (left - n);
    }
    if ((f.GDFS_flags & HF_AUTO_HYPHEN) &&
	GrTextBox(b->BS_gstate, x, y, "-", 1, box))
	AddDocRect(b, m, box[0], box[1], box[2], box[3]);
}

/* text without outlines: the font's box */
static void TextBox(BoundState *b, const GState *gs, const TMatrix *m,
		    const byte *e)
{
    Boolean cp = (Boolean) (*e == GR_DRAW_TEXT_CP || *e == GR_DRAW_CHAR_CP);
    int64_t x, y, box[4];
    const char *str;
    char c;
    word size;

    switch (*e) {
    case GR_DRAW_TEXT: size = U(e + 5); str = (const char *) e + 7; break;
    case GR_DRAW_TEXT_CP: size = U(e + 1); str = (const char *) e + 3; break;
    case GR_DRAW_TEXT_FIELD: size = 0; str = NULL; break;
    default: c = (char) e[1]; str = &c; size = 1; break;
    }
    if (*e == GR_DRAW_TEXT_FIELD) {
	FieldBox(b, m, e);
	return;
    }
    if (!size)
	return;
    At(gs, e, (word) (*e == GR_DRAW_CHAR ? 2 : 1), cp, &x, &y);
    if (GrTextBox(b->BS_gstate, x, y, str, size, box))
	AddDocRect(b, m, box[0], box[1], box[2], box[3]);
}

/* the bounds of the element before it plays (GSF_FIGURE_BOUNDS) */
static void Bound(void *ctx, const byte *e, word size)
{
    BoundState *b = ctx;
    byte op = *e;
    GrDraw d;
    Window w;
    Rectangle r;
    const TMatrix *m = &w.W_curTMatrix;

    if (op != GR_SET_GSTRING_BOUNDS &&
	(op < GR_DRAW_LINE || op > GR_DRAW_BITMAP_CP))
	return;
    /* a window on page coordinates, for the path code */
    memset(&w, 0, sizeof(w));
    w.W_maskRect.R_left = w.W_maskRect.R_top = -0x7fff;
    w.W_maskRect.R_right = w.W_maskRect.R_bottom = 0x7fff;
    d.GD_gstate = b->BS_gstate;
    d.GD_gs = MemLock(b->BS_gstate);
    d.GD_win = 0;
    d.GD_w = &w;
    GrComposeMatrix(d.GD_gstate, d.GD_gs, &w);
    switch (op) {
    case GR_SET_GSTRING_BOUNDS:
	AddDocRect(b, m, (int64_t) S(e + 1) * 65536, (int64_t) S(e + 3) * 65536,
		   (int64_t) S(e + 5) * 65536, (int64_t) S(e + 7) * 65536);
	break;
    case GR_DRAW_POINT:
    case GR_DRAW_POINT_CP: {
	int64_t x, y;
	At(d.GD_gs, e, 1, (Boolean) (op == GR_DRAW_POINT_CP), &x, &y);
	AddDocRect(b, m, x, y, x, y);
	break;
    }
    case GR_DRAW_BITMAP:
    case GR_DRAW_BITMAP_CP:
    case GR_FILL_BITMAP:
    case GR_FILL_BITMAP_CP:
	BitmapBox(b, d.GD_gs, m, e, size);
	break;
    case GR_DRAW_TEXT:
    case GR_DRAW_TEXT_CP:
    case GR_DRAW_CHAR:
    case GR_DRAW_CHAR_CP:
    case GR_DRAW_TEXT_FIELD:
	if (GrElementBounds(&d, e, size, &r)) {
	    Add(b, (int64_t) r.R_left * 65536, (int64_t) r.R_top * 65536);
	    Add(b, (int64_t) r.R_right * 65536, (int64_t) r.R_bottom * 65536);
	} else
	    TextBox(b, d.GD_gs, m, e);
	break;
    case GR_DRAW_PATH:
    case GR_FILL_PATH:
    case GR_DRAW_TEXT_PTR:
    case GR_DRAW_TEXT_OPTR:
    case GR_FILL_BITMAP_OPTR:
    case GR_FILL_BITMAP_PTR:
    case GR_DRAW_BITMAP_OPTR:
    case GR_DRAW_BITMAP_PTR:
	break;
    default:
	if (GrElementBounds(&d, e, size, &r)) {
	    /* BoundXLine: half the line width (page) around a line */
	    int64_t half = op < GR_DRAW_PATH ?
		(int64_t) (sdword) GR_WWF_D(d.GD_gs->GS_lineWidth) / 2 : 0;
	    Add(b, (int64_t) r.R_left * 65536 - half,
		(int64_t) r.R_top * 65536 - half);
	    Add(b, (int64_t) r.R_right * 65536 + half,
		(int64_t) r.R_bottom * 65536 + half);
	}
	break;
    }
    MemUnlock(b->BS_gstate);
}

static sdword Round(int64_t v)
{
    return (sdword) ((v + 0x8000) >> 16);
}

void GrGetGStringBoundsDWord(Handle gstring, GStateHandle gstate,
			     GSControl flags, RectDWord *bounds)
{
    BoundState b;
    TMatrix start;
    GState *gs;
    int64_t xs[4], ys[4];
    sdword l, t, r, bt, s;
    int i;

    memset(&b, 0, sizeof(b));
    b.BS_gstate = GrCreateState(0);
    if (gstate) {
	TransMatrix tm;
	PointWWFixed pen;
	WWFixedAsDWord pointSize;
	FontID id;

	GrGetTransform(gstate, &tm);
	GrSetTransform(b.BS_gstate, &tm);
	GrGetCurPosWWFixed(gstate, &pen);
	GrMoveToWWFixed(b.BS_gstate, GR_WWF_D(pen.PF_x), GR_WWF_D(pen.PF_y));
	GrSetLineWidth(b.BS_gstate, GrGetLineWidth(gstate));
	id = GrGetFont(gstate, &pointSize);
	GrSetFont(b.BS_gstate, id, pointSize);
    }
    gs = MemLock(b.BS_gstate);
    start = gs->GS_TMatrix;
    MemUnlock(b.BS_gstate);
    (void) GrPlayBounds(b.BS_gstate, gstring, flags, Bound, &b);
    GrDestroyState(b.BS_gstate);
    memset(bounds, 0, sizeof(*bounds));
    if (!b.BS_any)
	return;
    /* UnTransRotBounds: through the transformation at the start */
    xs[0] = xs[3] = b.BS_l;
    xs[1] = xs[2] = b.BS_r;
    ys[0] = ys[1] = b.BS_t;
    ys[2] = ys[3] = b.BS_b;
    for (i = 0; i < 4; i++)
	GrMatUntransDWF(&start, &xs[i], &ys[i]);
    l = r = Round(xs[0]);
    t = bt = Round(ys[0]);
    for (i = 1; i < 4; i++) {
	s = Round(xs[i]);
	if (s < l) l = s;
	if (s > r) r = s;
	s = Round(ys[i]);
	if (s < t) t = s;
	if (s > bt) bt = s;
    }
    bounds->RD_left = l;
    bounds->RD_top = t;
    bounds->RD_right = r;
    bounds->RD_bottom = bt;
}

/* CheckDWordResult: TRUE if a bound does not fit a word */
Boolean GrGetGStringBounds(Handle gstring, GStateHandle gstate,
			   GSControl flags, Rectangle *bounds)
{
    RectDWord rd;

    GrGetGStringBoundsDWord(gstring, gstate, flags, &rd);
    if (rd.RD_left < -0x8000 || rd.RD_left > 0x7fff ||
	rd.RD_top < -0x8000 || rd.RD_top > 0x7fff ||
	rd.RD_right < -0x8000 || rd.RD_right > 0x7fff ||
	rd.RD_bottom < -0x8000 || rd.RD_bottom > 0x7fff)
	return TRUE;
    bounds->R_left = (sword) rd.RD_left;
    bounds->R_top = (sword) rd.RD_top;
    bounds->R_right = (sword) rd.RD_right;
    bounds->R_bottom = (sword) rd.RD_bottom;
    return FALSE;
}
