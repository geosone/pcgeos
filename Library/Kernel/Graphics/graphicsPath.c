/*
 * graphicsPath.c -- paths (after graphicsPath.asm: GrBeginPath,
 * GrEndPath, GrCloseSubPath, GrFillPath, GrDrawPath, GrTestPath,
 * GrGetPathBounds, GrGetPathBoundsDWord; graphicsPathLow.asm:
 * CombineGStringOrPathWithPath, GrDestroyPath, PathBuildRegion and the
 * FillPath* routines, StrokePath).
 *
 * Defining a path: GrBeginPath saves the GState (GrSaveState), and the
 * GState writes into a graphics string of type GST_PATH
 * (GrAllocPathString) instead of drawing: GR_BEGIN_PATH with the
 * combination, the transformation (GR_SET_TRANSFORM) and the pen
 * (GR_MOVE_TO), then the output, transformation and pen elements the
 * routines store (not the attributes).  GrEndPath stores GR_END_PATH,
 * restores the GState and combines the elements with the current path,
 * a chunk of the GState (GS_currentPath): PCT_REPLACE takes them alone,
 * PCT_UNION and PCT_INTERSECTION append them as another group, PCT_NULL
 * (at GrBeginPath) removes the path.  As a chunk of the GState block,
 * the path is saved and restored with the GState.
 *
 * Using a path: its elements are played to device polygons, a group
 * (GR_BEGIN_PATH to GR_END_PATH) at a time, with the GState's
 * transformation and pen as the elements leave them (both put back
 * afterwards).  An element that is not a "to" one starts a subpath;
 * the "to" ones continue the subpath from the pen; GR_CLOSE_SUB_PATH
 * closes it, a move ends it.  Rectangles, polygons and ellipses are
 * closed subpaths; arcs closed as their ArcCloseType says; curves and
 * splines halved until flat (graphicsSpline.c).  GrFillPath fills the
 * groups' regions (by the fill rule) combined by union or intersection
 * with the area attributes; GrDrawPath draws each subpath with the line
 * attributes, closed ones joined at the start.
 *
 * Not yet: clip paths and clip rectangles (GrSetClipPath,
 * GrSetWinClipPath, GrSetClipRect, GrSetWinClipRect), GrSetStrokePath,
 * GrTestPointInPath, GrGetPathPoints, GrGetPathRegion, text, bitmaps
 * and rounded rectangles in paths (skipped), a path defined in a
 * graphics string.
 */
#include "graphicsInt.h"
#include <gstring.h>
#include <Internal/grWinInt.h>
#include <stdlib.h>

#define AREA	((word) offsetof(GState, GS_areaAttr))

static word U(const byte *p)
{
    return (word) (p[0] | (p[1] << 8));
}

static sword S(const byte *p)
{
    return (sword) U(p);
}

/* ---------------------------------------------------------------------
 * Defining a path
 * ------------------------------------------------------------------- */

static void FreePath(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    ChunkHandle ch = gs->GS_currentPath;

    gs->GS_currentPath = 0;
    if (ch)
	LMemFree(ConstructOptr(gstate, ch));
    MemUnlock(gstate);
}

void GrBeginPath(GStateHandle gstate, PathCombineType params)
{
    GState *gs = MemLock(gstate);
    MemHandle path;
    TransMatrix tm;
    PointWWFixed pen;

    if (gs->GS_pathFlags & (PF_FILL | PF_STROKE | PF_DEFINING_PATH)) {
	MemUnlock(gstate);
	return;
    }
    if (params == PCT_NULL) {
	MemUnlock(gstate);
	FreePath(gstate);
	return;
    }
    path = GrAllocPathString();
    if (!path) {
	MemUnlock(gstate);
	return;
    }
    gs->GS_pathData = gs->GS_gstring;
    gs->GS_pathFlags |= PF_DEFINING_PATH;
    gs->GS_gstring = 0;
    MemUnlock(gstate);
    GrSaveState(gstate);
    gs = MemLock(gstate);
    gs->GS_gstring = path;
    MemUnlock(gstate);
    (void) GrGSRecord(gstate, GR_BEGIN_PATH, "wbb", params, 0, 0);
    GrGetTransform(gstate, &tm);
    GrSetTransform(gstate, &tm);
    GrGetCurPosWWFixed(gstate, &pen);
    GrMoveToWWFixed(gstate, GR_WWF_D(pen.PF_x), GR_WWF_D(pen.PF_y));
}

/* CombineGStringOrPathWithPath: the group (size bytes) into the current
 * path */
static void Combine(GStateHandle gstate, const byte *group, word size)
{
    GState *gs = MemLock(gstate);
    ChunkHandle ch = gs->GS_currentPath;
    word old = 0;
    byte *p;

    if (ch && U(group + 1) != PCT_REPLACE)
	old = LMemGetChunkSize(ConstructOptr(gstate, ch));
    if (!ch) {
	ch = LMemAlloc(gstate, size);
	gs = MemDeref(gstate);
	gs->GS_currentPath = ch;
    } else
	LMemReAlloc(ConstructOptr(gstate, ch), (word) (old + size));
    p = LMemDeref(ConstructOptr(gstate, ch));
    memcpy(p + old, group, size);
    MemUnlock(gstate);
}

void GrEndPath(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    MemHandle path = gs->GS_gstring;
    byte *elems;
    word size;

    if (!(gs->GS_pathFlags & PF_DEFINING_PATH)) {
	MemUnlock(gstate);
	(void) GrGSRecord(gstate, GR_END_PATH, "");
	return;
    }
    MemUnlock(gstate);
    (void) GrGSRecord(gstate, GR_END_PATH, "");
    gs = MemLock(gstate);
    gs->GS_gstring = 0;
    MemUnlock(gstate);
    GrRestoreState(gstate);
    elems = GrFreePathString(path, &size);
    if (elems && size > 5 && elems[0] == GR_BEGIN_PATH)
	Combine(gstate, elems, size);
    free(elems);
    gs = MemLock(gstate);
    gs->GS_gstring = gs->GS_pathData;
    gs->GS_pathData = 0;
    gs->GS_pathFlags &= (PathFlags) ~PF_DEFINING_PATH;
    MemUnlock(gstate);
}

void GrCloseSubPath(GStateHandle gstate)
{
    (void) GrGSRecord(gstate, GR_CLOSE_SUB_PATH, "");
}

Boolean GrTestPath(GStateHandle gstate, GetPathType ptype)
{
    GState *gs = MemLock(gstate);
    Boolean r = FALSE;

    switch (ptype) {
    case GPT_CURRENT: r = (Boolean) (gs->GS_currentPath != 0); break;
    case GPT_CLIP: r = (Boolean) (gs->GS_clipPath != 0); break;
    case GPT_WIN_CLIP: r = (Boolean) (gs->GS_winClipPath != 0); break;
    }
    MemUnlock(gstate);
    return r;
}

/* ---------------------------------------------------------------------
 * Playing a path to device polygons
 * ------------------------------------------------------------------- */

typedef struct {
    sword	*SB_pts;	/* x, y */
    word	SB_count;
    word	SB_size;
    Boolean	SB_closed;
} SubPath;

typedef struct {
    GrDraw	*PW_d;
    SubPath	*PW_subs;
    word	PW_count;
    word	PW_size;
    Boolean	PW_open;	/* the last subpath goes on */
} PathWalk;

static SubPath *NewSub(PathWalk *w)
{
    SubPath *s;

    if (w->PW_count == w->PW_size) {
	word n = (word) (w->PW_size ? w->PW_size * 2 : 8);
	SubPath *ns = realloc(w->PW_subs, n * sizeof(SubPath));
	if (!ns)
	    return NULL;
	w->PW_subs = ns;
	w->PW_size = n;
    }
    s = &w->PW_subs[w->PW_count++];
    memset(s, 0, sizeof(*s));
    w->PW_open = TRUE;
    return s;
}

static void AddDev(SubPath *s, sword x, sword y)
{
    if (!s)
	return;
    if (s->SB_count == s->SB_size) {
	word n = (word) (s->SB_size ? s->SB_size * 2 : 16);
	sword *np = realloc(s->SB_pts, (size_t) n * 2 * sizeof(sword));
	if (!np)
	    return;
	s->SB_pts = np;
	s->SB_size = n;
    }
    s->SB_pts[2 * s->SB_count] = x;
    s->SB_pts[2 * s->SB_count + 1] = y;
    s->SB_count++;
}

static void AddDoc(PathWalk *w, SubPath *s, sword x, sword y)
{
    sword dx, dy;

    if (GrTransDev(w->PW_d, x, y, &dx, &dy))
	AddDev(s, dx, dy);
}

/* a point list in the separator format */
static void AddSep(SubPath *s, const sword *p, Boolean skipFirst)
{
    for (; p[0] != SEPARATOR; p += 2) {
	if (skipFirst) {
	    skipFirst = FALSE;
	    continue;
	}
	AddDev(s, p[0], p[1]);
    }
}

/* the subpath the "to" elements continue: from the pen */
static SubPath *Current(PathWalk *w)
{
    SubPath *s;
    sword x, y;

    if (w->PW_open && w->PW_count)
	return &w->PW_subs[w->PW_count - 1];
    s = NewSub(w);
    if (s && GrGetDevPenPos(w->PW_d, &x, &y))
	AddDev(s, x, y);
    return s;
}

static void Closed(PathWalk *w, SubPath *s)
{
    if (s)
	s->SB_closed = TRUE;
    w->PW_open = FALSE;
}

static void Pen(PathWalk *w, sword x, sword y)
{
    GrSetDocPenPos(w->PW_d->GD_gs, x, y);
}

static Point *Points(const byte *p, word n)
{
    Point *pts = malloc((n ? n : 1) * sizeof(Point));
    word i;

    if (pts)
	for (i = 0; i < n; i++) {
	    pts[i].P_x = S(p + 4 * i);
	    pts[i].P_y = S(p + 4 * i + 2);
	}
    return pts;
}

static void Ellipse(PathWalk *w, sword l, sword t, sword r, sword b,
		    word flags, GrArcParams *ap, word close)
{
    sword *pts, top, bot;
    word count;
    SubPath *s;

    pts = GrCalcEllipse(w->PW_d, l, t, r, b, flags, ap, &count, &top, &bot);
    if (!pts)
	return;
    if (ap && close != ACT_OPEN)
	GrCloseArc(&pts, &count, ap);
    s = NewSub(w);
    AddSep(s, pts, FALSE);
    if (!ap || close != ACT_OPEN)	/* an open arc goes on */
	Closed(w, s);
    free(pts);
}

static void Spline(PathWalk *w, const Point *pts, word n, Boolean to)
{
    word count;
    sword *dev = GrSplineDevPoints(w->PW_d, pts, n, &count);
    SubPath *s;

    if (!dev)
	return;
    if (to && w->PW_open && w->PW_count) {
	s = &w->PW_subs[w->PW_count - 1];
	AddSep(s, dev, TRUE);
    } else {
	s = NewSub(w);
	AddSep(s, dev, FALSE);
    }
    free(dev);
    Pen(w, pts[n - 1].P_x, pts[n - 1].P_y);
}

/* the elements from e to GR_END_PATH (or end) as subpaths; returns the
 * end */
static const byte *Walk(PathWalk *w, GStateHandle gstate, const byte *e,
			const byte *end)
{
    word size;

    for (; e < end && *e != GR_END_PATH && *e != GR_END_GSTRING;
	 e += size) {
	SubPath *s;
	Point *pts;
	XYValueAsDWord pen;
	word n;

	size = GrGSElemSize(e, (dword) (end - e));
	if (!size)
	    break;
	switch (*e) {
	case GR_MOVE_TO:
	case GR_REL_MOVE_TO:
	case GR_MOVE_TO_WWFIXED:
	    w->PW_open = FALSE;
	    GrPlayElement(gstate, e);
	    break;
	case GR_CLOSE_SUB_PATH:
	    if (w->PW_open && w->PW_count)
		Closed(w, &w->PW_subs[w->PW_count - 1]);
	    break;
	case GR_DRAW_LINE:
	case GR_DRAW_HLINE:
	case GR_DRAW_VLINE: {
	    sword x1 = S(e + 1), y1 = S(e + 3), x2, y2;
	    if (*e == GR_DRAW_LINE) {
		x2 = S(e + 5);
		y2 = S(e + 7);
	    } else if (*e == GR_DRAW_HLINE) {
		x2 = S(e + 5);
		y2 = y1;
	    } else {
		x2 = x1;
		y2 = S(e + 5);
	    }
	    s = NewSub(w);
	    AddDoc(w, s, x1, y1);
	    AddDoc(w, s, x2, y2);
	    Pen(w, x2, y2);
	    break;
	}
	case GR_DRAW_LINE_TO:
	case GR_DRAW_HLINE_TO:
	case GR_DRAW_VLINE_TO: {
	    sword x, y;
	    pen = GrGetCurPos(gstate);
	    x = *e == GR_DRAW_VLINE_TO ? DWORD_X(pen) : S(e + 1);
	    y = *e == GR_DRAW_LINE_TO ? S(e + 3) :
		*e == GR_DRAW_HLINE_TO ? DWORD_Y(pen) : S(e + 1);
	    AddDoc(w, Current(w), x, y);
	    Pen(w, x, y);
	    break;
	}
	case GR_DRAW_REL_LINE_TO: {
	    sword x, y;
	    s = Current(w);
	    GrRelMovePen(gstate, (WWFixedAsDWord) U(e + 1) |
			 ((WWFixedAsDWord) U(e + 3) << 16),
			 (WWFixedAsDWord) U(e + 5) |
			 ((WWFixedAsDWord) U(e + 7) << 16));
	    if (GrGetDevPenPos(w->PW_d, &x, &y))
		AddDev(s, x, y);
	    break;
	}
	case GR_DRAW_RECT:
	case GR_FILL_RECT:
	case GR_DRAW_RECT_TO:
	case GR_FILL_RECT_TO: {
	    sword l, t, r, b;
	    if (*e == GR_DRAW_RECT || *e == GR_FILL_RECT) {
		l = S(e + 1); t = S(e + 3); r = S(e + 5); b = S(e + 7);
		Pen(w, l, t);
	    } else {
		pen = GrGetCurPos(gstate);
		l = DWORD_X(pen); t = DWORD_Y(pen); r = S(e + 1); b = S(e + 3);
	    }
	    s = NewSub(w);
	    AddDoc(w, s, l, t);
	    AddDoc(w, s, r, t);
	    AddDoc(w, s, r, b);
	    AddDoc(w, s, l, b);
	    Closed(w, s);
	    break;
	}
	case GR_DRAW_POLYLINE:
	case GR_BRUSH_POLYLINE:
	case GR_DRAW_POLYGON:
	case GR_FILL_POLYGON: {
	    word off = (word) (*e == GR_BRUSH_POLYLINE ? 5 :
			       *e == GR_FILL_POLYGON ? 4 : 3), i;
	    n = U(e + 1);
	    if (!n || !(pts = Points(e + off, n)))
		break;
	    s = NewSub(w);
	    for (i = 0; i < n; i++)
		AddDoc(w, s, pts[i].P_x, pts[i].P_y);
	    if (*e == GR_DRAW_POLYGON || *e == GR_FILL_POLYGON) {
		Closed(w, s);
		Pen(w, pts[0].P_x, pts[0].P_y);
	    } else
		Pen(w, pts[n - 1].P_x, pts[n - 1].P_y);
	    free(pts);
	    break;
	}
	case GR_DRAW_ELLIPSE:
	case GR_FILL_ELLIPSE:
	    Ellipse(w, S(e + 1), S(e + 3), S(e + 5), S(e + 7), GR_CET_ELLIPSE,
		    NULL, ACT_CHORD);
	    Pen(w, S(e + 1), S(e + 3));
	    break;
	case GR_DRAW_ARC:
	case GR_FILL_ARC: {
	    GrArcParams ap;
	    sword l = S(e + 3), t = S(e + 5), r = S(e + 7), b = S(e + 9), x, y;
	    word close = U(e + 1);
	    memset(&ap, 0, sizeof(ap));
	    ap.GA_close = close;
	    GrSetupArcLow(w->PW_d, l, t, r, b, S(e + 11), S(e + 13), &ap);
	    Ellipse(w, l, t, r, b, GR_CET_BOUNDED_ARC, &ap, close);
	    GrArcAnglePos(l, t, r, b, close == ACT_OPEN ? S(e + 13) :
			  S(e + 11), &x, &y);
	    Pen(w, x, y);
	    break;
	}
	case GR_DRAW_CURVE:
	    if ((pts = Points(e + 1, 4)) != NULL) {
		Spline(w, pts, 4, FALSE);
		free(pts);
	    }
	    break;
	case GR_DRAW_CURVE_TO:
	case GR_DRAW_SPLINE_TO:
	    n = (word) (*e == GR_DRAW_CURVE_TO ? 3 : U(e + 1));
	    if ((pts = malloc(((size_t) n + 1) * sizeof(Point))) != NULL) {
		Point *more = Points(e + (*e == GR_DRAW_CURVE_TO ? 1 : 3), n);
		if (more) {
		    pen = GrGetCurPos(gstate);
		    pts[0].P_x = DWORD_X(pen);
		    pts[0].P_y = DWORD_Y(pen);
		    memcpy(pts + 1, more, n * sizeof(Point));
		    if (!(w->PW_open && w->PW_count))
			(void) Current(w);
		    Spline(w, pts, (word) (n + 1), TRUE);
		    free(more);
		}
		free(pts);
	    }
	    break;
	case GR_DRAW_SPLINE:
	    n = U(e + 1);
	    if ((pts = Points(e + 3, n)) != NULL) {
		Spline(w, pts, n, FALSE);
		free(pts);
	    }
	    break;
	default:
	    if (*e >= GR_APPLY_ROTATION && *e <= GR_RESTORE_TRANSFORM) {
		GrPlayElement(gstate, e);
		w->PW_d->GD_gs = MemDeref(gstate);
		GrComposeMatrix(gstate, w->PW_d->GD_gs, w->PW_d->GD_w);
	    }
	    break;			/* not yet: see the top */
	}
	w->PW_d->GD_gs = MemDeref(gstate);
    }
    return e;
}

static void FreeSubs(PathWalk *w)
{
    word i;

    for (i = 0; i < w->PW_count; i++)
	free(w->PW_subs[i].SB_pts);
    w->PW_count = 0;
    w->PW_open = FALSE;
}

/* the subpaths in the separator format, each closed or not as asked */
static sword *SepFormat(const PathWalk *w, word first, word last,
			Boolean close, word *count)
{
    size_t n = 2;
    word i, k = 0;
    sword *sep, *o;

    for (i = first; i < last; i++)
	n += (size_t) w->PW_subs[i].SB_count * 2 + 3;
    if (!(sep = malloc(n * sizeof(sword))))
	return NULL;
    o = sep;
    for (i = first; i < last; i++) {
	const SubPath *s = &w->PW_subs[i];
	if (s->SB_count < 2)
	    continue;
	memcpy(o, s->SB_pts, (size_t) s->SB_count * 2 * sizeof(sword));
	o += 2 * s->SB_count;
	k = (word) (k + s->SB_count);
	if (close && (s->SB_pts[0] != o[-2] || s->SB_pts[1] != o[-1])) {
	    o[0] = s->SB_pts[0];
	    o[1] = s->SB_pts[1];
	    o += 2;
	    k++;
	}
	*o++ = SEPARATOR;
    }
    if (o == sep) {
	free(sep);
	return NULL;
    }
    *o++ = SEPARATOR;
    *count = k;
    return sep;
}

/* the region (without bounds, C heap) of a op b; a and b freed */
static word *RegOp(RegionOpFlags op, word *a, word *b)
{
    word size = 512, *out = NULL;

    for (;;) {
	word *n = realloc(out, size);
	if (!n) {
	    free(out);
	    out = NULL;
	    break;
	}
	out = n;
	if (!GrPtrRegOp(op, a, b, out, &size))
	    break;
    }
    free(a);
    free(b);
    return out;
}

/* PathBuildRegion: the path's region in device coordinates (without
 * bounds, C heap; NULL: empty) */
static word *PathRegion(GStateHandle gstate, GrDraw *d, const byte *path,
			word size, RegionFillRule rule, PathWalk *w)
{
    const byte *e = path, *end = path + size;
    word *acc = NULL;

    while (e < end && *e == GR_BEGIN_PATH) {
	PathCombineType comb = (PathCombineType) U(e + 1);
	word first = w->PW_count, count;
	sword *sep;
	word *reg = NULL;

	e = Walk(w, gstate, e + 5, end);
	if (e < end && *e == GR_END_PATH)
	    e++;
	sep = SepFormat(w, first, w->PW_count, FALSE, &count);
	if (sep) {
	    reg = GrPolygonRegion(d->GD_w, sep, count, rule, FALSE);
	    free(sep);
	}
	if (reg) {			/* the region alone */
	    Rectangle r;
	    word n = GrGetPtrRegBounds(reg + 4, &r);
	    word *only = malloc(n);
	    if (only)
		memcpy(only, reg + 4, n);
	    free(reg);
	    reg = only;
	}
	if (!acc || comb == PCT_REPLACE) {
	    free(acc);
	    acc = reg;
	} else if (comb == PCT_UNION) {
	    if (reg)
		acc = RegOp(ROF_OR_OP, acc, reg);
	} else {			/* PCT_INTERSECTION */
	    if (reg)
		acc = RegOp(ROF_AND_OP, acc, reg);
	    else {
		free(acc);
		acc = NULL;
	    }
	}
    }
    return acc;
}

/* a copy of the current path (C heap); NULL if none */
static byte *CopyPath(GStateHandle gstate, word *size)
{
    GState *gs = MemLock(gstate);
    byte *p = NULL;

    if (gs->GS_currentPath) {
	optr o = ConstructOptr(gstate, gs->GS_currentPath);
	*size = LMemGetChunkSize(o);
	if ((p = malloc(*size)) != NULL)
	    memcpy(p, LMemDeref(o), *size);
    }
    MemUnlock(gstate);
    return p;
}

typedef struct {
    TMatrix	PS_matrix;
    PointDWFixed PS_pen;
} PathSaved;

static void Save(GrDraw *d, PathSaved *s, PathFlags flag)
{
    s->PS_matrix = d->GD_gs->GS_TMatrix;
    s->PS_pen = d->GD_gs->GS_penPos;
    d->GD_gs->GS_pathFlags |= flag;
}

static void Restore(GrDraw *d, const PathSaved *s, PathFlags flag)
{
    d->GD_gs = MemDeref(d->GD_gstate);
    d->GD_gs->GS_TMatrix = s->PS_matrix;
    d->GD_gs->GS_penPos = s->PS_pen;
    d->GD_gs->GS_pathFlags &= (PathFlags) ~flag;
    if (d->GD_w)
	GrComposeMatrix(d->GD_gstate, d->GD_gs, d->GD_w);
}

void GrFillPath(GStateHandle gstate, RegionFillRule rule)
{
    GrDraw d;
    PathWalk w;
    PathSaved saved;
    byte *path;
    word size, *reg;

    if (GrGSRecord(gstate, GR_FILL_PATH, "b", rule))
	return;
    if (!(path = CopyPath(gstate, &size)))
	return;
    GrEnterGraphics(gstate, &d);
    if (!GrTrivialReject(&d) &&
	!(d.GD_gs->GS_pathFlags & (PF_FILL | PF_STROKE))) {
	memset(&w, 0, sizeof(w));
	w.PW_d = &d;
	Save(&d, &saved, PF_FILL);
	reg = PathRegion(gstate, &d, path, size, rule, &w);
	Restore(&d, &saved, PF_FILL);
	if (reg) {
	    Rectangle r;
	    word n = GrGetPtrRegBounds(reg, &r);
	    word *full = malloc(n + sizeof(Rectangle));
	    if (full && *reg != EOREGREC) {
		DriverRegs regs;
		full[0] = (word) r.R_left;
		full[1] = (word) r.R_top;
		full[2] = (word) r.R_right;
		full[3] = (word) r.R_bottom;
		memcpy(full + 4, reg, n);
		memset(&regs, 0, sizeof(regs));
		regs.DR_farPtr = full;
		GrCallDriver(&d, DR_VID_REGION, &regs, AREA);
	    }
	    free(full);
	    free(reg);
	}
	FreeSubs(&w);
	free(w.PW_subs);
    }
    GrExitGraphics(&d);
    free(path);
}

void GrDrawPath(GStateHandle gstate)
{
    GrDraw d;
    PathWalk w;
    PathSaved saved;
    byte *path;
    const byte *e, *end;
    word size, i, count;

    if (GrGSRecord(gstate, GR_DRAW_PATH, ""))
	return;
    if (!(path = CopyPath(gstate, &size)))
	return;
    GrEnterGraphics(gstate, &d);
    if (!GrTrivialReject(&d) &&
	!(d.GD_gs->GS_pathFlags & (PF_FILL | PF_STROKE))) {
	memset(&w, 0, sizeof(w));
	w.PW_d = &d;
	Save(&d, &saved, PF_STROKE);
	for (e = path, end = path + size; e < end && *e == GR_BEGIN_PATH;) {
	    e = Walk(&w, gstate, e + 5, end);
	    if (e < end && *e == GR_END_PATH)
		e++;
	}
	Restore(&d, &saved, PF_STROKE);
	for (i = 0; i < w.PW_count; i++) {
	    SubPath *s = &w.PW_subs[i];
	    PathWalk one;
	    sword *sep;
	    if (s->SB_count < 2)
		continue;
	    one = w;
	    sep = SepFormat(&one, i, (word) (i + 1), s->SB_closed, &count);
	    if (sep) {
		GrDrawPolylineLow(&d, sep, count, s->SB_closed);
		free(sep);
	    }
	}
	FreeSubs(&w);
	free(w.PW_subs);
    }
    GrExitGraphics(&d);
    free(path);
}

/* GrGetPathBoundsDWord: the bounds of the filled path in document
 * coordinates (of the device bounds, through the inverse of the
 * matrix); TRUE if there is no path */
Boolean GrGetPathBoundsDWord(GStateHandle gstate, GetPathType ptype,
			     RectDWord *bounds)
{
    GrDraw d;
    PathWalk w;
    PathSaved saved;
    byte *path;
    word size, *reg = NULL;
    Boolean none = TRUE;

    memset(bounds, 0, sizeof(*bounds));
    if (ptype != GPT_CURRENT || !(path = CopyPath(gstate, &size)))
	return TRUE;
    GrEnterGraphics(gstate, &d);
    if (d.GD_w) {
	memset(&w, 0, sizeof(w));
	w.PW_d = &d;
	Save(&d, &saved, PF_FILL);
	reg = PathRegion(gstate, &d, path, size, ODD_EVEN, &w);
	Restore(&d, &saved, PF_FILL);
	FreeSubs(&w);
	free(w.PW_subs);
    }
    if (reg && *reg != EOREGREC) {
	Rectangle r;
	TMatrix m = d.GD_w->W_curTMatrix;
	int64_t xs[4], ys[4];
	int i;

	GrGetPtrRegBounds(reg, &r);
	xs[0] = xs[3] = (int64_t) r.R_left * 65536;
	xs[1] = xs[2] = (int64_t) (r.R_right + 1) * 65536;
	ys[0] = ys[1] = (int64_t) r.R_top * 65536;
	ys[2] = ys[3] = (int64_t) (r.R_bottom + 1) * 65536;
	for (i = 0; i < 4; i++)
	    GrMatUntransDWF(&m, &xs[i], &ys[i]);
	bounds->RD_left = bounds->RD_right = (sdword) (xs[0] >> 16);
	bounds->RD_top = bounds->RD_bottom = (sdword) (ys[0] >> 16);
	for (i = 1; i < 4; i++) {
	    sdword x = (sdword) (xs[i] >> 16), y = (sdword) (ys[i] >> 16);
	    if (x < bounds->RD_left) bounds->RD_left = x;
	    if (x > bounds->RD_right) bounds->RD_right = x;
	    if (y < bounds->RD_top) bounds->RD_top = y;
	    if (y > bounds->RD_bottom) bounds->RD_bottom = y;
	}
	none = FALSE;
    }
    free(reg);
    GrExitGraphics(&d);
    free(path);
    return none;
}

Boolean GrGetPathBounds(GStateHandle gstate, GetPathType ptype,
			Rectangle *bounds)
{
    RectDWord r;
    Boolean none = GrGetPathBoundsDWord(gstate, ptype, &r);

    bounds->R_left = (sword) r.RD_left;
    bounds->R_top = (sword) r.RD_top;
    bounds->R_right = (sword) r.RD_right;
    bounds->R_bottom = (sword) r.RD_bottom;
    return none;
}
