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
 * Clip paths (GS_clipPath, GS_winClipPath) are paths of the GState
 * too, each group with its fill rule; GrSetClipPath and
 * GrSetWinClipPath combine the current path into one, GrSetClipRect and
 * GrSetWinClipRect a rectangle (the window one in page coordinates).
 * The window's mask is the visible region AND their regions (GrClipMask
 * from WinValWinStruct), recalculated when one changes.
 *
 * GrGetPathRegion and GrGetClipRegion return the region in device
 * coordinates with its bounds first (NULL if empty or no window);
 * GrTestPointInPath tests a document point against it.  GrGetPath
 * returns a path as a graphics string.  GrSetStrokePath and
 * GrGetPathPoints do nothing, as in the asm.
 *
 * Text in a path is the outline of its characters, which the font
 * driver gives when the path is used (GrTextOutline, graphicsText.c);
 * each contour is a closed subpath.
 *
 * Not yet: bitmaps in paths (skipped), a path defined in a graphics
 * string.
 */
#include "graphicsInt.h"
#include <gstring.h>
#include <Internal/grWinInt.h>
#include <stdlib.h>

#define AREA	((word) offsetof(GState, GS_areaAttr))

/* OBP_flags of a group of a clip path: its fill rule (the asm's
 * BPF_FILL_RULE_VALID, BPF_FILL_RULE) */
#define PATH_RULE_VALID	0x80
#define PATH_RULE	0x01

/* the GState's chunk of a path */
static ChunkHandle *PathField(GState *gs, GetPathType ptype)
{
    switch (ptype) {
    case GPT_CLIP: return &gs->GS_clipPath;
    case GPT_WIN_CLIP: return &gs->GS_winClipPath;
    default: return &gs->GS_currentPath;
    }
}

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

/* GrDestroyPath */
static void FreePath(GStateHandle gstate, GetPathType ptype)
{
    GState *gs = MemLock(gstate);
    ChunkHandle ch = *PathField(gs, ptype);

    *PathField(gs, ptype) = 0;
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
	FreePath(gstate, GPT_CURRENT);
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

/* CombineGStringOrPathWithPath: the groups (size bytes) into a path of
 * the GState, by the combination of the first */
static void Combine(GStateHandle gstate, GetPathType ptype,
		   const byte *group, word size)
{
    GState *gs = MemLock(gstate);
    ChunkHandle ch = *PathField(gs, ptype);
    word old = 0;
    byte *p;

    if (ch && U(group + 1) != PCT_REPLACE)
	old = LMemGetChunkSize(ConstructOptr(gstate, ch));
    if (!ch) {
	ch = LMemAlloc(gstate, size);
	gs = MemDeref(gstate);
	*PathField(gs, ptype) = ch;
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
	Combine(gstate, GPT_CURRENT, elems, size);
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

static sdword DW(const byte *p)
{
    return (sdword) ((dword) U(p) | ((dword) U(p + 2) << 16));
}

static void SetW(WWFixed *w, sdword v)
{
    w->WWF_frac = (word) v;
    w->WWF_int = (sword) (v >> 16);
}

/* a 3-point arc: the "to" ones from the pen, continuing its subpath if
 * open; the pen to point 3 (point 1 for an open GR_*_ARC_3POINT) */
static void Arc3(PathWalk *w, GStateHandle gstate, const byte *e)
{
    GrArcParams ap;
    sword l, t, r, b;
    sdword x1, y1, px, py;
    word flags, close = U(e + 1);
    const byte *p = e + 3;
    Boolean to = (Boolean) (*e != GR_DRAW_ARC_3POINT &&
			    *e != GR_FILL_ARC_3POINT);

    memset(&ap, 0, sizeof(ap));
    ap.GA_close = close;
    if (to) {
	GrGetCurPosWWFixed(gstate, &ap.GA_point1);
	x1 = (sdword) GR_WWF_D(ap.GA_point1.PF_x);
	y1 = (sdword) GR_WWF_D(ap.GA_point1.PF_y);
    } else {
	x1 = DW(p);
	y1 = DW(p + 4);
	SetW(&ap.GA_point1.PF_x, x1);
	SetW(&ap.GA_point1.PF_y, y1);
	p += 8;
    }
    if (*e == GR_DRAW_REL_ARC_3POINT_TO) {
	SetW(&ap.GA_point2.PF_x, x1 + DW(p));
	SetW(&ap.GA_point2.PF_y, y1 + DW(p + 4));
	SetW(&ap.GA_point3.PF_x, x1 + DW(p + 8));
	SetW(&ap.GA_point3.PF_y, y1 + DW(p + 12));
    } else {
	SetW(&ap.GA_point2.PF_x, DW(p));
	SetW(&ap.GA_point2.PF_y, DW(p + 4));
	SetW(&ap.GA_point3.PF_x, DW(p + 8));
	SetW(&ap.GA_point3.PF_y, DW(p + 12));
    }
    px = (sdword) GR_WWF_D(ap.GA_point3.PF_x);
    py = (sdword) GR_WWF_D(ap.GA_point3.PF_y);
    if (!to && close == ACT_OPEN) {
	px = x1;
	py = y1;
    }
    flags = GrSetupArc3PointLow(w->PW_d, &ap, &l, &t, &r, &b);
    if (flags & GR_CEF_COLINEAR) {	/* a line from l, t to r, b */
	SubPath *s = to ? Current(w) : NewSub(w);
	if (!to)
	    AddDoc(w, s, l, t);
	AddDoc(w, s, r, b);
    } else {
	sword *pts, top, bot;
	word count;
	pts = GrCalcEllipse(w->PW_d, l, t, r, b, flags, &ap, &count, &top,
			    &bot);
	if (pts) {
	    SubPath *s;
	    if (close != ACT_OPEN)
		GrCloseArc(&pts, &count, &ap);
	    s = to && close == ACT_OPEN ? Current(w) : NewSub(w);
	    AddSep(s, pts, FALSE);
	    if (close != ACT_OPEN)
		Closed(w, s);
	    free(pts);
	}
    }
    GrSetDocWWFPenPos(w->PW_d->GD_gs, px, py);
}

static const byte *Walk(PathWalk *w, GStateHandle gstate, const byte *e,
			const byte *end);

/* DrawTextToPath: the outlines of the characters (GrTextOutline), each
 * contour a closed subpath */
static void Text(PathWalk *w, GStateHandle gstate, const byte *e)
{
    int64_t x, y;
    const char *str;
    char c;
    word size, len, first, i;
    byte *outline;
    PointDWFixed pen;

    if (*e == GR_DRAW_TEXT || *e == GR_DRAW_CHAR) {
	word at = (word) (*e == GR_DRAW_TEXT ? 1 : 2);
	x = (int64_t) S(e + at) * 65536;
	y = (int64_t) S(e + at + 2) * 65536;
    } else {
	PointWWFixed cp;
	GrGetCurPosWWFixed(gstate, &cp);
	x = (int64_t) cp.PF_x.WWF_int * 65536 + cp.PF_x.WWF_frac;
	y = (int64_t) cp.PF_y.WWF_int * 65536 + cp.PF_y.WWF_frac;
    }
    switch (*e) {
    case GR_DRAW_TEXT: size = U(e + 5); str = (const char *) e + 7; break;
    case GR_DRAW_TEXT_CP: size = U(e + 1); str = (const char *) e + 3; break;
    case GR_DRAW_CHAR: c = (char) e[1]; str = &c; size = 1; break;
    default: c = (char) e[1]; str = &c; size = 1; break;
    }
    if (!size)
	return;
    outline = GrTextOutline(gstate, x, y, str, size, &len);
    w->PW_d->GD_gs = MemDeref(gstate);
    if (!outline)
	return;
    pen = w->PW_d->GD_gs->GS_penPos;	/* after the text */
    w->PW_open = FALSE;
    first = w->PW_count;
    (void) Walk(w, gstate, outline, outline + len);
    w->PW_d->GD_gs = MemDeref(gstate);
    w->PW_d->GD_gs->GS_penPos = pen;
    for (i = first; i < w->PW_count; i++)
	w->PW_subs[i].SB_closed = TRUE;
    w->PW_open = FALSE;
    free(outline);
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
	case GR_DRAW_ROUND_RECT:
	case GR_FILL_ROUND_RECT:
	case GR_DRAW_ROUND_RECT_TO:
	case GR_FILL_ROUND_RECT_TO: {
	    sword l, t, r, b, *dev;
	    word count, i;
	    if (*e == GR_DRAW_ROUND_RECT || *e == GR_FILL_ROUND_RECT) {
		l = S(e + 3); t = S(e + 5); r = S(e + 7); b = S(e + 9);
		Pen(w, l, t);
	    } else {
		pen = GrGetCurPos(gstate);
		l = DWORD_X(pen); t = DWORD_Y(pen); r = S(e + 3); b = S(e + 5);
	    }
	    dev = GrRoundRectDevPoints(w->PW_d, l, t, r, b, U(e + 1), &count);
	    if (dev) {
		s = NewSub(w);
		for (i = 0; i + 1 < count; i++)	/* not the first again */
		    AddDev(s, dev[2 * i], dev[2 * i + 1]);
		Closed(w, s);
		free(dev);
	    }
	    break;
	}
	case GR_DRAW_ARC_3POINT:
	case GR_FILL_ARC_3POINT:
	case GR_DRAW_ARC_3POINT_TO:
	case GR_FILL_ARC_3POINT_TO:
	case GR_DRAW_REL_ARC_3POINT_TO:
	    Arc3(w, gstate, e);
	    break;
	case GR_DRAW_TEXT:
	case GR_DRAW_TEXT_CP:
	case GR_DRAW_CHAR:
	case GR_DRAW_CHAR_CP:
	    Text(w, gstate, e);
	    break;
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
	RegionFillRule r = (e[3] & PATH_RULE_VALID) ?
	    (RegionFillRule) (e[3] & PATH_RULE) : rule;
	word first = w->PW_count, count;
	sword *sep;
	word *reg = NULL;

	e = Walk(w, gstate, e + 5, end);
	if (e < end && *e == GR_END_PATH)
	    e++;
	sep = SepFormat(w, first, w->PW_count, FALSE, &count);
	if (sep) {
	    reg = GrPolygonRegion(d->GD_w, sep, count, r, FALSE);
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

/* a copy of a path (C heap); NULL if none */
static byte *CopyPath(GStateHandle gstate, GetPathType ptype, word *size)
{
    GState *gs = MemLock(gstate);
    ChunkHandle ch = *PathField(gs, ptype);
    byte *p = NULL;

    if (ch) {
	optr o = ConstructOptr(gstate, ch);
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
    if (!(path = CopyPath(gstate, GPT_CURRENT, &size)))
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
    if (!(path = CopyPath(gstate, GPT_CURRENT, &size)))
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
    if (!(path = CopyPath(gstate, ptype, &size)))
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

/* ---------------------------------------------------------------------
 * Clip paths
 * ------------------------------------------------------------------- */

/* SetClipPathCommon: the current path (its groups with the fill rule)
 * combined into a clip path; the window's mask recalculated */
static void SetClip(GStateHandle gstate, GetPathType ptype,
		    PathCombineType params, RegionFillRule rule)
{
    GState *gs;
    byte *path;
    word size, off;

    if (params == PCT_NULL)
	FreePath(gstate, ptype);
    else if ((path = CopyPath(gstate, GPT_CURRENT, &size)) != NULL) {
	path[1] = (byte) params;
	path[2] = (byte) (params >> 8);
	for (off = 0; off + 5 <= size && path[off] == GR_BEGIN_PATH;) {
	    word n;
	    path[off + 3] = (byte) (PATH_RULE_VALID | (rule & PATH_RULE));
	    for (off += 5; off < size && path[off] != GR_END_PATH;
		 off = (word) (off + n))
		if (!(n = GrGSElemSize(path + off, (dword) (size - off)))) {
		    off = size;
		    break;
		}
	    if (off < size)
		off++;
	}
	Combine(gstate, ptype, path, size);
	free(path);
    } else if (params == PCT_REPLACE)
	FreePath(gstate, ptype);
    gs = MemLock(gstate);
    GrWinStateFlags(gstate, gs, (WinGrFlags) ~WGF_MASK_VALID,
		    (WinGrRegFlags) ~(ptype == GPT_CLIP ? WGRF_PATH_VALID :
				      WGRF_WIN_PATH_VALID));
    MemUnlock(gstate);
}

void GrSetClipPath(GStateHandle gstate, PathCombineType params,
		   RegionFillRule rule)
{
    if (!GrGSRecord(gstate, GR_SET_CLIP_PATH, "bw", rule, params))
	SetClip(gstate, GPT_CLIP, params, rule);
}

void GrSetWinClipPath(GStateHandle gstate, PathCombineType params,
		      RegionFillRule rule)
{
    if (!GrGSRecord(gstate, GR_SET_WIN_CLIP_PATH, "bw", rule, params))
	SetClip(gstate, GPT_WIN_CLIP, params, rule);
}

/* SetClipRectCommon, AddRectangleToPath: the rectangle as a path of its
 * own (the clip rectangle with the transformation, the window one in
 * page coordinates), combined into the clip path; the current path
 * stays */
static void SetClipRect(GStateHandle gstate, GetPathType ptype,
			PathCombineType flags, sword l, sword t, sword r,
			sword b)
{
    GState *gs = MemLock(gstate);
    ChunkHandle cur = gs->GS_currentPath;

    if (gs->GS_pathFlags & (PF_DEFINING_PATH | PF_FILL | PF_STROKE)) {
	MemUnlock(gstate);
	return;
    }
    if (flags == PCT_NULL) {
	MemUnlock(gstate);
	SetClip(gstate, ptype, PCT_NULL, ODD_EVEN);
	return;
    }
    gs->GS_currentPath = 0;
    MemUnlock(gstate);
    GrBeginPath(gstate, PCT_REPLACE);
    if (ptype == GPT_WIN_CLIP)
	GrSetNullTransform(gstate);
    GrFillRect(gstate, l, t, r, b);
    GrEndPath(gstate);
    SetClip(gstate, ptype, flags, ODD_EVEN);
    FreePath(gstate, GPT_CURRENT);
    gs = MemLock(gstate);
    gs->GS_currentPath = cur;
    MemUnlock(gstate);
}

void GrSetClipRect(GStateHandle gstate, PathCombineType flags, sword left,
		   sword top, sword right, sword bottom)
{
    if (!GrGSRecord(gstate, GR_SET_CLIP_RECT, "wwwww", flags, left, top,
		    right, bottom))
	SetClipRect(gstate, GPT_CLIP, flags, left, top, right, bottom);
}

void GrSetWinClipRect(GStateHandle gstate, PathCombineType flags,
		      sword left, sword top, sword right, sword bottom)
{
    if (!GrGSRecord(gstate, GR_SET_WIN_CLIP_RECT, "wwwww", flags, left, top,
		    right, bottom))
	SetClipRect(gstate, GPT_WIN_CLIP, flags, left, top, right, bottom);
}

/* the region rows of chunk ch (C heap) */
static word *ChunkRows(optr o)
{
    word n = LMemGetChunkSize(o), *r = malloc(n ? n : 2);

    if (r)
	memcpy(r, LMemDeref(o), n);
    return r;
}

/* the clip paths of GState gh (locked) taken into the mask of window wh
 * (locked, the matrix composed): the mask AND each clip region
 * (WinValWinStruct) */
void GrClipMask(GStateHandle gh, WindowHandle wh, ChunkHandle mask)
{
    optr m = ConstructOptr(wh, mask);
    GetPathType ptype;

    for (ptype = GPT_CLIP; ptype <= GPT_WIN_CLIP; ptype++) {
	GrDraw d;
	PathWalk w;
	PathSaved saved;
	byte *path;
	word size, *reg;

	if (!(path = CopyPath(gh, ptype, &size)))
	    continue;
	d.GD_gstate = gh;
	d.GD_gs = MemDeref(gh);
	d.GD_win = wh;
	d.GD_w = MemDeref(wh);
	memset(&w, 0, sizeof(w));
	w.PW_d = &d;
	Save(&d, &saved, PF_FILL);
	reg = PathRegion(gh, &d, path, size, ODD_EVEN, &w);
	Restore(&d, &saved, PF_FILL);
	FreeSubs(&w);
	free(w.PW_subs);
	free(path);
	if (reg)
	    reg = RegOp(ROF_AND_OP, ChunkRows(m), reg);
	if (reg && *reg != EOREGREC) {
	    Rectangle r;
	    word n = GrGetPtrRegBounds(reg, &r);
	    LMemReAlloc(m, n);
	    memcpy(LMemDeref(m), reg, n);
	} else {
	    word *p;
	    LMemReAlloc(m, 2 * sizeof(word));
	    p = LMemDeref(m);
	    p[0] = p[1] = EOREGREC;
	}
	free(reg);
    }
}

/* ---------------------------------------------------------------------
 * Asking about paths
 * ------------------------------------------------------------------- */

/* the region (rows, device coordinates, C heap) of a path of the
 * GState through its window; NULL: no path, no window or empty */
static word *DevRegion(GStateHandle gstate, GetPathType ptype,
		       RegionFillRule rule, GrDraw *d)
{
    PathWalk w;
    PathSaved saved;
    byte *path;
    word size, *reg = NULL;

    if (!(path = CopyPath(gstate, ptype, &size)))
	return NULL;
    if (d->GD_w && !(d->GD_gs->GS_pathFlags & (PF_FILL | PF_STROKE))) {
	memset(&w, 0, sizeof(w));
	w.PW_d = d;
	Save(d, &saved, PF_FILL);
	reg = PathRegion(gstate, d, path, size, rule, &w);
	Restore(d, &saved, PF_FILL);
	FreeSubs(&w);
	free(w.PW_subs);
    }
    free(path);
    if (reg && *reg == EOREGREC) {
	free(reg);
	reg = NULL;
    }
    return reg;
}

/* ConvertRegionPathToRegion: a block of the bounds and the rows */
static MemHandle RegionBlock(word *reg)
{
    MemHandle mh;
    Rectangle r;
    word n, *p;

    if (!reg)
	return 0;
    n = GrGetPtrRegBounds(reg, &r);
    mh = MemAlloc((word) (n + sizeof(Rectangle)), HF_SWAPABLE, 0);
    if (mh) {
	p = MemLock(mh);
	p[0] = (word) r.R_left;
	p[1] = (word) r.R_top;
	p[2] = (word) r.R_right;
	p[3] = (word) r.R_bottom;
	memcpy(p + 4, reg, n);
	MemUnlock(mh);
    }
    free(reg);
    return mh;
}

MemHandle GrGetPathRegion(GStateHandle gstate, RegionFillRule rule)
{
    GrDraw d;
    word *reg;

    GrEnterGraphics(gstate, &d);
    reg = DevRegion(gstate, GPT_CURRENT, rule, &d);
    GrExitGraphics(&d);
    return RegionBlock(reg);
}

/* the clip paths ANDed (one alone if the other is missing) */
MemHandle GrGetClipRegion(GStateHandle gstate, RegionFillRule rule)
{
    GrDraw d;
    word *clip, *win;

    GrEnterGraphics(gstate, &d);
    clip = DevRegion(gstate, GPT_CLIP, rule, &d);
    win = DevRegion(gstate, GPT_WIN_CLIP, rule, &d);
    GrExitGraphics(&d);
    if (clip && win)
	clip = RegOp(ROF_AND_OP, clip, win);
    else if (!clip)
	clip = win;
    if (clip && *clip == EOREGREC) {
	free(clip);
	clip = NULL;
    }
    return RegionBlock(clip);
}

/* TRUE if document point x, y is in the filled current path */
Boolean GrTestPointInPath(GStateHandle gstate, word xPos, word yPos,
			  RegionFillRule rule)
{
    GrDraw d;
    word *reg;
    sword x, y;
    Rectangle b;
    Boolean in = FALSE;

    GrEnterGraphics(gstate, &d);
    reg = DevRegion(gstate, GPT_CURRENT, rule, &d);
    if (reg && GrTransDev(&d, (sword) xPos, (sword) yPos, &x, &y))
	in = GrTestPointInReg((const Region *) reg, (word) x, (word) y, &b);
    GrExitGraphics(&d);
    free(reg);
    return in;
}

/* the asm stores the element and leaves the path as it is */
void GrSetStrokePath(GStateHandle gstate)
{
    (void) GrGSRecord(gstate, GR_SET_STROKE_PATH, "");
}

/* the asm returns no points */
MemHandle GrGetPathPoints(GStateHandle gstate, word resolution)
{
    (void) gstate;
    (void) resolution;
    return 0;
}

/* GrGetPath: the path as a graphics string to draw into a GState: each
 * group's transformation applied (GR_APPLY_TRANSFORM) and its pen set
 * before its GR_BEGIN_PATH, so the path lands where it was defined
 * relative to the GState's own transformation; GR_END_GSTRING at the
 * end */
MemHandle GrGetPath(GStateHandle gstate, GetPathType ptype)
{
    byte *path, *out, *o;
    word size, off = 0;
    MemHandle mh;

    if (!(path = CopyPath(gstate, ptype, &size)))
	return 0;
    mh = MemAlloc((word) (size + 1), HF_SWAPABLE, 0);
    if (!mh) {
	free(path);
	return 0;
    }
    out = o = MemLock(mh);
    while (off < size) {
	word n = GrGSElemSize(path + off, (dword) (size - off)), k = 0, m = 0;
	if (!n)
	    break;
	if (path[off] == GR_BEGIN_PATH && off + n < size &&
	    path[off + n] == GR_SET_TRANSFORM)
	    k = GrGSElemSize(path + off + n, (dword) (size - off - n));
	if (k && off + n + k < size)
	    m = GrGSElemSize(path + off + n + k, (dword) (size - off - n - k));
	if (m) {
	    memcpy(o, path + off + n, k);	/* the transformation */
	    *o = GR_APPLY_TRANSFORM;
	    o += k;
	    memcpy(o, path + off + n + k, m);	/* the pen */
	    o += m;
	    memcpy(o, path + off, n);		/* GR_BEGIN_PATH */
	    o += n;
	    off = (word) (off + n + k + m);
	    continue;
	}
	memcpy(o, path + off, n);
	o += n;
	off = (word) (off + n);
    }
    *o++ = GR_END_GSTRING;
    size = (word) (o - out);
    MemUnlock(mh);
    MemReAlloc(mh, size, 0);
    free(path);
    return mh;
}
