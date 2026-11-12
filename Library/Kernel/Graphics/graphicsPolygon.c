/*
 * graphicsPolygon.c -- filled polygons (after graphicsPolygon.asm:
 * GrFillPolygon, GrTestPointInPolygon, DrawPolygonLow, IsPointInPolygon,
 * ReturnRegionPolygon, ReturnRegionPolygonAfterTrans,
 * DrawPolygonAfterTrans, PolygonAfterTrans, GetBounds, CheckTrivial,
 * BuildET, AddPolylineToET, ShortenEdge, InitBresError, PutRecordInET,
 * InsertEdgeRec, ScanLineCon, BuildAEL, SetWindingScanLineInfo,
 * SetEvenOddScanLineInfo, GenRegDef, GetOnOff, DeleteAEL, UpdateAEL,
 * YRoutine, XRoutine, ResortAEL).
 *
 * A polygon is turned into a region in device coordinates, which the
 * video driver draws (DR_VID_REGION).  The scan conversion is the
 * asm's (after the X window system's): an edge table sorted by the top
 * of each edge, an active edge list sorted by x, and for every scan
 * line each edge's span as Bresenham would draw it as a line.  So the
 * polygon covers the pixels of its outline, as a polyline would draw
 * it, and what is inside by the even-odd or the winding rule:
 *   - an edge's span is always on; a SPAN_TOGGLE edge also switches
 *     between outside and inside, a SPAN_ONLY edge does not;
 *   - horizontal edges are SPAN_ONLY;
 *   - at a vertex where the outline goes on in the same vertical
 *     direction, the second edge is SPAN_ONLY on that scan line
 *     (ER_shortenTop, ER_shortenBottom), so the vertex counts once;
 *   - by the winding rule only the edges where the winding number
 *     goes to or from 0 toggle.
 *
 * The points come in the separator format (graphicsPolyline.c): x, y
 * pairs, SEPARATOR after each polyline and once more at the end.  The
 * asm kept the tables in a block of a size worked out before (at most
 * 64K); here they are in the C heap and the region grows as it is
 * written.
 *
 * With an area pattern the polygon is filled through a path, by its
 * fill rule (graphicsPattern.c).
 */
#include "graphicsInt.h"
#include <stdlib.h>

#define AREA	((word) offsetof(GState, GS_areaAttr))

/* ER_edgeInfo */
#define ER_SHORTEN_BOTTOM	0x01
#define ER_SHORTEN_TOP		0x02
#define ER_HORIZ		0x04
#define ER_CCW			0x08
#define ER_USE_Y_ROUTINE	0x10
#define ER_DELTA_X_NEG		0x20

/* ER_scanLineInfo */
#define SPAN_ONLY	1
#define SPAN_TOGGLE	2

typedef struct {
    sword	xLeft;		/* the top x, then the span's left */
    sword	yMin;
    sword	xRight;		/* the span's right */
    sword	yMax;
    sword	xBottom;	/* the bottom x */
    sword	bresD, bresIncr1, bresIncr2;
    sword	clockwise;	/* 1 going down, -1 going up */
    byte	edgeInfo;
    byte	scanLineInfo;
    int		link;		/* the next edge, -1 */
} Edge;

typedef struct {
    sword	y;
    int		head;
} Bucket;

typedef struct {
    const sword	*pts;
    Edge	*edges;
    int		nEdges;
    Bucket	*buckets;
    int		nBuckets;
    int		ael;		/* the active edge list */
    sword	polygonYTop;
    sword	boundXMin, boundXMax, boundYMin, boundYMax;
    byte	prevEdgeInfo;
    RegionFillRule rule;
    word	*reg;		/* the region */
    dword	regSize, regLen;	/* in words */
    dword	prevLine, curLine;
} Poly;

/* ---------------------------------------------------------------------
 * The edge table
 * ------------------------------------------------------------------- */

/* InsertEdgeRec: before the first edge whose x is not less */
static void InsertEdge(Poly *p, int *pp, int e)
{
    sword x = p->edges[e].xLeft;

    while (*pp != -1 && x > p->edges[*pp].xLeft)
	pp = &p->edges[*pp].link;
    p->edges[e].link = *pp;
    *pp = e;
}

/* PutRecordInET: into its bucket, unless above or below the bounds */
static void PutRecord(Poly *p, int e)
{
    Edge *er = &p->edges[e];
    int i;

    if (er->yMin > p->boundYMax || er->yMax < p->boundYMin)
	return;
    for (i = 0; i < p->nBuckets && p->buckets[i].y < er->yMin; i++)
	;
    if (i == p->nBuckets || p->buckets[i].y != er->yMin) {
	memmove(&p->buckets[i + 1], &p->buckets[i],
		(size_t) (p->nBuckets - i) * sizeof(Bucket));
	p->nBuckets++;
	p->buckets[i].y = er->yMin;
	p->buckets[i].head = -1;
    }
    InsertEdge(p, &p->buckets[i].head, e);
    p->nEdges++;
}

/* ShortenEdge: the vertex between two edges going the same way counts
 * once */
static byte Shorten(byte cur, byte prev)
{
    if ((cur & ER_HORIZ) || (prev & ER_HORIZ))
	return cur;
    if (cur & ER_CCW) {
	if (prev & ER_CCW)
	    cur |= ER_SHORTEN_BOTTOM;
    } else if (!(prev & ER_CCW))
	cur |= ER_SHORTEN_TOP;
    return cur;
}

/* InitBresError; FALSE when no scan line of it is inside the bounds */
static Boolean InitBres(Poly *p, Edge *er)
{
    int dx = er->xBottom - er->xLeft, dy, v;
    sword top, bottom;

    if (dx < 0) {
	dx = -dx;
	er->edgeInfo |= ER_DELTA_X_NEG;
    }
    dx = (sword) dx;
    dy = (sword) (er->yMax - er->yMin);
    if (dy >= dx) {
	er->edgeInfo |= ER_USE_Y_ROUTINE;
	er->bresIncr1 = (sword) (2 * dx);
	er->bresD = (sword) (er->bresIncr1 - dy);
	v = er->bresD - dy;
    } else {
	er->bresIncr1 = (sword) (2 * dy);
	er->bresD = (sword) (er->bresIncr1 - dx);
	v = er->bresD - dx;
    }
    er->bresIncr2 = (sword) (v < -32768 || v > 32767 ? (sword) 0x8000 : v);
    bottom = er->yMax < p->boundYMax ? er->yMax : p->boundYMax;
    top = er->yMin > p->boundYMin ? er->yMin : p->boundYMin;
    return (Boolean) (bottom >= top);
}

/* the edge from point a to point b (word offsets into the points) */
static byte MakeEdge(Poly *p, Edge *er, int a, int b)
{
    sword ya = p->pts[a + 1], yb = p->pts[b + 1];
    sword xa = p->pts[a], xb = p->pts[b];

    memset(er, 0, sizeof(*er));
    er->clockwise = 1;
    er->link = -1;
    if (ya == yb) {
	er->edgeInfo = ER_HORIZ;
	er->yMin = er->yMax = ya;
	if (xa > xb) {
	    sword s = xa; xa = xb; xb = s;
	}
	er->xLeft = er->xRight = xa;
	er->xBottom = xb;
	return er->edgeInfo;
    }
    if (ya > yb) {
	sword s;
	er->edgeInfo = ER_CCW;
	er->clockwise = -1;
	s = ya; ya = yb; yb = s;
	s = xa; xa = xb; xb = s;
    }
    er->yMin = ya;
    er->yMax = yb;
    er->xLeft = er->xRight = xa;
    er->xBottom = xb;
    return er->edgeInfo;
}

/* the edge from point a to point b into the table, the vertex at a
 * shortened against the edge before (have: there is one) */
static void AddEdge(Poly *p, int a, int b, Boolean *have)
{
    Edge *er = &p->edges[p->nEdges];
    byte info = MakeEdge(p, er, a, b);

    if (info & ER_HORIZ) {
	PutRecord(p, p->nEdges);
	return;
    }
    if (have) {
	er->edgeInfo = info = Shorten(info, p->prevEdgeInfo);
	p->prevEdgeInfo = info;
    }
    if (InitBres(p, er))
	PutRecord(p, p->nEdges);
}

/* AddPolylineToET: the polyline at word offset s, closed; returns
 * where the next one starts.  The first edge that is not horizontal
 * goes in last, when the edge before it (the closing one, or the last
 * one not horizontal) is known. */
static int AddPolyline(Poly *p, int s)
{
    int n, i, first = -1;
    Boolean have = FALSE;

    for (n = 0; p->pts[s + 2 * n] != SEPARATOR; n++)
	;
    if (n == 1) {				/* a point */
	AddEdge(p, s, s, NULL);
	return s + 3;
    }
    for (i = 0; i < n; i++) {
	int a = s + 2 * i, b = i + 1 < n ? a + 2 : s;
	Edge *er = &p->edges[p->nEdges];
	byte info = MakeEdge(p, er, a, b);

	if (info & ER_HORIZ) {
	    PutRecord(p, p->nEdges);
	    continue;
	}
	if (!have) {			/* kept for the end */
	    have = TRUE;
	    first = i;
	    p->prevEdgeInfo = info;
	    continue;
	}
	AddEdge(p, a, b, &have);
    }
    if (first >= 0) {
	int a = s + 2 * first;
	AddEdge(p, a, first + 1 < n ? a + 2 : s, &have);
    }
    return s + 2 * n + 1;
}

/* ---------------------------------------------------------------------
 * The scan lines
 * ------------------------------------------------------------------- */

/* YRoutine: one pixel per scan line */
static void YRoutine(Edge *er, sword y)
{
    if (y == er->yMin)
	return;
    if (er->bresD > 0) {
	er->xLeft = (sword) (er->xLeft + ((er->edgeInfo & ER_DELTA_X_NEG) ?
					  -1 : 1));
	er->xRight = er->xLeft;
	er->bresD = (sword) (er->bresD + er->bresIncr2);
    } else
	er->bresD = (sword) (er->bresD + er->bresIncr1);
}

/* XRoutine: a run of pixels per scan line */
static void XRoutine(Edge *er, sword y)
{
    sword l = er->xLeft, r = er->xRight, d, n = 0;
    Boolean neg = (Boolean) ((er->edgeInfo & ER_DELTA_X_NEG) != 0);

    if (er->edgeInfo & ER_HORIZ) {
	er->xRight = er->xBottom;
	return;
    }
    if (y != er->yMin) {
	if (y == er->yMax) {		/* the last: to the bottom x */
	    if (!neg) {
		er->xLeft = (sword) (r + 1);
		er->xRight = er->xBottom;
	    } else {
		er->xRight = (sword) (l - 1);
		er->xLeft = er->xBottom;
	    }
	    return;
	}
	if (!neg)
	    l = r = (sword) (r + 1);
	else
	    r = l = (sword) (l - 1);
    }
    d = er->bresD;
    while (d <= 0) {
	n++;
	d = (sword) (d + er->bresIncr1);
    }
    d = (sword) (d + er->bresIncr2);
    if (!neg)
	r = (sword) (r + n);
    else
	l = (sword) (l - n);
    er->xLeft = l;
    er->xRight = r;
    er->bresD = d;
}

/* ResortAEL: by the spans' left x again */
static void Resort(Poly *p)
{
    Boolean swapped;
    int *pp, a, b;

    do {
	swapped = FALSE;
	for (pp = &p->ael; *pp != -1 && p->edges[*pp].link != -1;
	     pp = &p->edges[*pp].link) {
	    a = *pp;
	    b = p->edges[a].link;
	    if (p->edges[a].xLeft > p->edges[b].xLeft) {
		p->edges[a].link = p->edges[b].link;
		p->edges[b].link = a;
		*pp = b;
		swapped = TRUE;
	    }
	}
    } while (swapped);
}

/* SetEvenOddScanLineInfo, SetWindingScanLineInfo */
static void SetScanLineInfo(Poly *p, sword y)
{
    int e, winding = 0, out = 1;
    Edge *er;

    for (e = p->ael; e != -1; e = er->link) {
	er = &p->edges[e];
	er->scanLineInfo = SPAN_TOGGLE;
	if (er->edgeInfo & ER_HORIZ) {
	    er->scanLineInfo = SPAN_ONLY;
	    continue;
	}
	if (er->edgeInfo & ER_SHORTEN_TOP) {	/* only its first line */
	    er->edgeInfo &= (byte) ~ER_SHORTEN_TOP;
	    er->scanLineInfo = SPAN_ONLY;
	    continue;
	}
	if ((er->edgeInfo & ER_SHORTEN_BOTTOM) && y == er->yMax) {
	    er->scanLineInfo = SPAN_ONLY;
	    continue;
	}
	if (p->rule != WINDING)
	    continue;
	winding += er->clockwise;
	if (out > 0) {
	    if (!winding)
		er->scanLineInfo = SPAN_ONLY;
	    else
		out = -out;
	} else if (!winding)
	    out = -out;
	else
	    er->scanLineInfo = SPAN_ONLY;
    }
}

static void RegPut(Poly *p, word v)
{
    if (p->regLen == p->regSize) {
	word *n = realloc(p->reg, 2 * p->regSize * sizeof(word));
	if (!n)
	    return;			/* full: cut off */
	p->reg = n;
	p->regSize *= 2;
    }
    p->reg[p->regLen++] = v;
}

/* GenRegDef, GetOnOff: the line's on/off pairs; a line the same as the
 * one before only moves that one's bottom */
static void GenLine(Poly *p, sword y)
{
    int e = p->ael;
    dword start = p->regLen, i, len;

    RegPut(p, (word) y);
    while (e != -1) {
	Edge *er = &p->edges[e];
	sword on = er->xLeft, off = er->xRight;
	int toggles = er->scanLineInfo != SPAN_ONLY;

	for (e = er->link; e != -1; e = er->link) {
	    er = &p->edges[e];
	    if (!(toggles & 1) && (sword) (er->xLeft - off) > 1)
		break;
	    if (er->scanLineInfo != SPAN_ONLY)
		toggles++;
	    if (er->xRight > off)
		off = er->xRight;
	}
	RegPut(p, (word) on);
	RegPut(p, (word) off);
    }
    RegPut(p, EOREGREC);
    len = p->regLen - start;
    if (p->prevLine != start && start - p->prevLine == len) {
	for (i = 1; i < len; i++)
	    if (p->reg[p->prevLine + i] != p->reg[start + i])
		break;
	if (i == len) {
	    p->reg[p->prevLine]++;
	    p->regLen = start;
	    return;
	}
    }
    p->prevLine = start;
}

/* ScanLineCon */
static void ScanLines(Poly *p)
{
    int bucket = 0, *pp, e, next;
    sword y = p->polygonYTop;

    p->ael = -1;
    for (;;) {
	if (bucket < p->nBuckets && p->buckets[bucket].y == y) {
	    for (e = p->buckets[bucket].head; e != -1; e = next) {
		next = p->edges[e].link;
		InsertEdge(p, &p->ael, e);
	    }
	    bucket++;
	}
	for (e = p->ael; e != -1; e = p->edges[e].link)
	    if (p->edges[e].edgeInfo & ER_USE_Y_ROUTINE)
		YRoutine(&p->edges[e], y);
	    else
		XRoutine(&p->edges[e], y);
	Resort(p);
	SetScanLineInfo(p, y);
	if (y >= p->boundYMin)
	    GenLine(p, y);
	for (pp = &p->ael; *pp != -1;)		/* DeleteAEL */
	    if (p->edges[*pp].yMax == y)
		*pp = p->edges[*pp].link;
	    else
		pp = &p->edges[*pp].link;
	if (y >= p->boundYMax)
	    break;
	y++;
    }
    RegPut(p, EOREGREC);
}

/* GetBounds: of the points, the y range clipped to the window; FALSE
 * when nothing of it is in the window */
static Boolean GetBounds(Poly *p, const Window *w)
{
    const sword *s = p->pts;
    sword xMin = s[0], xMax = s[0], yMin = s[1], yMax = s[1];

    for (s += 2;; s += 2) {
	if (s[0] == SEPARATOR) {
	    if (s[1] == SEPARATOR)
		break;
	    s++;
	}
	if (s[0] < xMin)
	    xMin = s[0];
	else if (s[0] > xMax)
	    xMax = s[0];
	if (s[1] < yMin)
	    yMin = s[1];
	else if (s[1] > yMax)
	    yMax = s[1];
    }
    p->polygonYTop = yMin;
    p->boundXMin = xMin;
    p->boundXMax = xMax;
    p->boundYMin = yMin < w->W_winRect.R_top ? w->W_winRect.R_top : yMin;
    p->boundYMax = yMax > w->W_winRect.R_bottom ? w->W_winRect.R_bottom :
		   yMax;
    return (Boolean) (p->boundYMax >= p->boundYMin);
}

/*
 * ReturnRegionPolygonAfterTrans: the region of polygons in device
 * coordinates (separator format, n points in all), with its bounds
 * first, in the C heap; NULL when nothing of it is in the window.
 * trivial: also NULL when it is outside the mask's bounds
 * (CheckTrivial).
 */
word *GrPolygonRegion(const Window *w, const sword *pts, word n,
		      RegionFillRule rule, Boolean trivial)
{
    Poly p;
    int s;

    memset(&p, 0, sizeof(p));
    p.pts = pts;
    p.rule = rule;
    if (!GetBounds(&p, w))
	return 0;
    if (trivial && (p.boundXMin > w->W_maskRect.R_right ||
		    p.boundXMax < w->W_maskRect.R_left ||
		    p.boundYMin > w->W_maskRect.R_bottom ||
		    p.boundYMax < w->W_maskRect.R_top))
	return 0;
    p.edges = malloc(((size_t) n + 1) * sizeof(Edge));
    p.buckets = malloc(((size_t) n + 1) * sizeof(Bucket));
    p.regSize = 256;
    p.reg = malloc(p.regSize * sizeof(word));
    if (!p.edges || !p.buckets || !p.reg) {
	free(p.edges);
	free(p.buckets);
	free(p.reg);
	return NULL;
    }
    for (s = 0; pts[s] != SEPARATOR;)		/* BuildET */
	s = AddPolyline(&p, s);
    p.regLen = 0;
    RegPut(&p, (word) p.boundXMin);	/* GetRegBuffer */
    RegPut(&p, (word) p.boundYMin);
    RegPut(&p, (word) p.boundXMax);
    RegPut(&p, (word) p.boundYMax);
    RegPut(&p, (word) (p.boundYMin - 1));
    RegPut(&p, EOREGREC);
    p.prevLine = p.regLen;
    ScanLines(&p);
    free(p.edges);
    free(p.buckets);
    return p.reg;
}

/* DrawPolygonAfterTrans: a region of polygons drawn with attr */
void GrDrawPolygonDev(const GrDraw *d, const sword *pts, word n,
		      RegionFillRule rule, word attr)
{
    word *reg = GrPolygonRegion(d->GD_w, pts, n, rule, TRUE);
    DriverRegs r;

    if (!reg)
	return;
    memset(&r, 0, sizeof(r));
    r.DR_farPtr = reg;
    GrCallDriver(d, DR_VID_REGION, &r, attr);
    free(reg);
}

/* DrawPolygonLow: document points */
static void DrawPolygonLow(const GrDraw *d, const Point *points, word n,
			   RegionFillRule rule, word attr)
{
    sword *sep;
    word count;

    if ((d->GD_w->W_grFlags & WGF_MASK_NULL) || !n)
	return;
    sep = GrSeparatorFormat(d, points, n, FALSE, &count);
    if (!sep)
	return;
    GrDrawPolygonDev(d, sep, count, rule, attr);
    free(sep);
}

void GrFillPolygon(GStateHandle gstate, RegionFillRule windingRule,
		   const Point *points, word numPoints)
{
    GrDraw d;

    if (GrPatternStart(gstate, FALSE)) {
	GrFillPolygon(gstate, windingRule, points, numPoints);
	GrPatternFinish(gstate, windingRule, FALSE);
	return;
    }
    (void) GrGSRecord(gstate, GR_FILL_POLYGON, "wbr", numPoints, windingRule,
		      points, 4 * numPoints);
    GrEnterGraphics(gstate, &d);
    if (numPoints)
	GrSetDocPenPos(d.GD_gs, points[0].P_x, points[0].P_y);
    if (!GrTrivialReject(&d))
	DrawPolygonLow(&d, points, numPoints, windingRule, AREA);
    GrExitGraphics(&d);
}

/*
 * IsPointInPolygon.  As in the asm, the point goes through the
 * window's matrix only (WinTransCoord), the polygon through the
 * GState's too.
 */
Boolean GrTestPointInPolygon(GStateHandle gstate, RegionFillRule rule,
			     Point *list, word numPoints, sword xCoord,
			     sword yCoord)
{
    GrDraw d;
    sword *sep;
    word *reg, count;
    Boolean in = FALSE;
    int64_t px = (int64_t) xCoord * 65536, py = (int64_t) yCoord * 65536;
    sword x, y;

    GrEnterGraphics(gstate, &d);
    if (d.GD_w && numPoints) {
	sep = GrSeparatorFormat(&d, list, numPoints, FALSE, &count);
	if (sep) {
	    reg = GrPolygonRegion(d.GD_w, sep, count, rule, FALSE);
	    free(sep);
	    if (reg) {
		GrMatTransDWF(&d.GD_w->W_TMatrix, &px, &py);
		if (GrRoundToWord(px, &x) && GrRoundToWord(py, &y)) {
		    x = (sword) (x + d.GD_w->W_winRect.R_left);
		    y = (sword) (y + d.GD_w->W_winRect.R_top);
		    in = GrTestPointInReg((const Region *) (reg + 4),
					  (word) x, (word) y, NULL);
		}
		free(reg);
	    }
	}
    }
    GrExitGraphics(&d);
    return in;
}
