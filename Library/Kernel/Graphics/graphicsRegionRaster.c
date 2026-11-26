/*
 * graphicsRegionRaster.c -- after graphicsRegionRaster.asm: Contains
 * rasterization routines for regions/paths.
 */
#include "graphicsInt.h"
#include <stdlib.h>

#define BEZIER_POINT_TOLERANCE	2	/* grWinInt.def, pixels */
#define BEZIER_MAX_DEPTH	10

static sdword Abs(sdword v)
{
    return v < 0 ? -v : v;
}

/* the distance of c from the line a-b, squared, is within half a pixel */
static Boolean NearLine(const sdword *a, const sdword *b, const sdword *c)
{
    int64_t dx = b[0] - a[0], dy = b[1] - a[1];
    int64_t cross = dx * (c[1] - a[1]) - dy * (c[0] - a[0]);
    int64_t len2 = dx * dx + dy * dy;

    if (!len2)
	return FALSE;
    /* (cross / len)^2 <= (128)^2 */
    return (Boolean) ((double) cross * (double) cross <=
		      16384.0 * (double) len2);
}

/* RasterBezier: p[0..7] the four points (x, y), 1/256 pixel; adds the
 * end of each piece */
Boolean RasterBezier(SplinePoly *poly, const sdword *p, int depth)
{
    sdword l[8], r[8], m[2];
    int i;
    Boolean flat;

    flat = (Boolean) (Abs(p[2] - p[0]) <= BEZIER_POINT_TOLERANCE * 256 &&
		      Abs(p[3] - p[1]) <= BEZIER_POINT_TOLERANCE * 256 &&
		      Abs(p[6] - p[4]) <= BEZIER_POINT_TOLERANCE * 256 &&
		      Abs(p[7] - p[5]) <= BEZIER_POINT_TOLERANCE * 256);
    if (!flat)
	flat = (Boolean) (NearLine(&p[0], &p[6], &p[2]) &&
			  NearLine(&p[0], &p[6], &p[4]));
    if (flat || depth >= BEZIER_MAX_DEPTH)
	return GrSplineAddPoint(poly, p[6], p[7]);
    for (i = 0; i < 2; i++) {			/* DivideCurve */
	sdword p01 = (p[i] + p[2 + i]) / 2, p12 = (p[2 + i] + p[4 + i]) / 2;
	sdword p23 = (p[4 + i] + p[6 + i]) / 2;
	sdword a = (p01 + p12) / 2, b = (p12 + p23) / 2;
	m[i] = (a + b) / 2;
	l[i] = p[i];
	l[2 + i] = p01;
	l[4 + i] = a;
	l[6 + i] = m[i];
	r[i] = m[i];
	r[2 + i] = b;
	r[4 + i] = p23;
	r[6 + i] = p[6 + i];
    }
    return (Boolean) (RasterBezier(poly, l, depth + 1) &&
		      RasterBezier(poly, r, depth + 1));
}
