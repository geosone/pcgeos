/*
 * graphicsMath.c -- fixed point math of the graphics system (after
 * graphicsMath.asm and graphicsMathObscure.asm).
 *
 * WWFixed is 16.16, DWFixed 32.16 (48 bits), each in two's complement.
 * The results are the asm's bit for bit: products truncated toward
 * zero, quotients too (GrSDivDWFbyWWF rounds), the sine from the
 * tables with the asm's interpolation, the arc sine with its binary
 * search, the square root with its two Newton steps.
 */
#include "graphicsInt.h"
#include <stdint.h>

static uint32_t Abs32(sdword v)
{
    return v < 0 ? (uint32_t) 0 - (uint32_t) v : (uint32_t) v;
}

/* GrRegMul32ToDDF, the middle 32 bits */
WWFixedAsDWord GrMulWWFixed(WWFixedAsDWord i, WWFixedAsDWord j)
{
    uint64_t p = (uint64_t) Abs32((sdword) i) * Abs32((sdword) j);
    uint32_t r = (uint32_t) (p >> 16);

    return ((sdword) (i ^ j) < 0) ? (WWFixedAsDWord) (0 - r) :
	   (WWFixedAsDWord) r;
}

Boolean GrUDiv32(WWFixedAsDWord dividend, WWFixedAsDWord divisor,
		 WWFixedAsDWord *q)
{
    uint64_t r;

    if (!dividend) {			/* the asm's shortcuts */
	*q = 0;
	return TRUE;
    }
    if (divisor == 0x10000UL) {
	*q = dividend;
	return TRUE;
    }
    if (!divisor)
	return FALSE;
    r = ((uint64_t) dividend << 16) / divisor;
    if (r >> 32)
	return FALSE;
    *q = (WWFixedAsDWord) r;
    return TRUE;
}

Boolean GrSDiv32(WWFixedAsDWord dividend, WWFixedAsDWord divisor,
		 WWFixedAsDWord *q)
{
    Boolean ok = GrUDiv32(Abs32((sdword) dividend), Abs32((sdword) divisor),
			  q);

    if ((sdword) (dividend ^ divisor) < 0)
	*q = (WWFixedAsDWord) (0 - *q);
    return ok;
}

/* overflow: the quotient is undefined; here 0 */
WWFixedAsDWord GrSDivWWFixed(WWFixedAsDWord dividend, WWFixedAsDWord divisor)
{
    WWFixedAsDWord q;

    return GrSDiv32(dividend, divisor, &q) ? q : 0;
}

WWFixedAsDWord GrUDivWWFixed(WWFixedAsDWord dividend, WWFixedAsDWord divisor)
{
    WWFixedAsDWord q;

    return GrUDiv32(dividend, divisor, &q) ? q : 0;
}

WWFixedAsDWord GrReciprocal32(WWFixedAsDWord v)
{
    WWFixedAsDWord q = 0;

    GrSDiv32(0x10000UL, v, &q);
    return q;
}

WWFixedAsDWord GrSqrWWFixed(WWFixedAsDWord i)
{
    return GrMulWWFixed(i, i);
}

/* GrSqrRoot32: 1 for 1 and less */
WWFixedAsDWord GrSqrRootWWFixed(WWFixedAsDWord i)
{
    word n = (word) (i >> 16), di = 1, bx;
    byte al, cl = 17, carry;
    uint32_t a;
    WWFixedAsDWord q;

    if (n <= 1)
	return 0x10000UL;
    al = (byte) (n >> 8);
    if (!al) {
	cl -= 8;
	al = (byte) n;
    }
    do {				/* the highest bit set */
	carry = (byte) (al & 0x80);
	al = (byte) (al << 1);
	cl--;
    } while (!carry);
    cl >>= 1;
    a = i;				/* first approximation */
    do {
	a >>= 1;
	di = (word) (di << 1);
    } while (--cl);
    bx = (word) ((a >> 16) + di);
    a = (((uint32_t) bx << 16) | (word) a) >> 1;
    q = 0;
    GrUDiv32(i, a, &q);			/* two Newton steps */
    a = (uint32_t) (a + q) >> 1;
    q = 0;
    GrUDiv32(i, a, &q);
    return (WWFixedAsDWord) ((uint32_t) (a + q) >> 1);
}

/* GrFastSine */
WWFixedAsDWord GrQuickSine(WWFixedAsDWord angle)
{
    Boolean neg = FALSE;
    sword a;
    word f, lo, hi, idx, r;
    uint32_t prod;

    if ((sdword) angle < 0) {
	neg = TRUE;
	angle = (WWFixedAsDWord) (0 - angle);
    }
    a = (sword) (angle >> 16);
    while (a >= 360)
	a -= 360;
    angle = GR_WWF(a, angle);
    if (a >= 90) {
	if (a >= 270) {
	    angle = (WWFixedAsDWord) (0 - angle) + GR_WWF(360, 0);
	    neg = (Boolean) !neg;
	} else if (a >= 180) {
	    angle -= GR_WWF(180, 0);
	    neg = (Boolean) !neg;
	} else
	    angle = (WWFixedAsDWord) (0 - angle) + GR_WWF(180, 0);
	if ((angle >> 16) == 90) {
	    angle = 0x10000UL;
	    goto done;
	}
    }
    idx = (word) (angle >> 16);
    f = (word) angle;
    r = grSinIntTable[idx];
    if (f) {
	r = grSinFracTable[idx];
	if (f != 0x8000) {
	    if (f < 0x8000) {
		lo = grSinIntTable[idx];
		hi = r;
	    } else {
		lo = r;
		hi = grSinIntTable[idx + 1];
		if (idx == 89)
		    lo--;
	    }
	    prod = (uint32_t) (word) (hi - lo) * (word) (f << 1);
	    r = (word) ((prod >> 16) + ((prod >> 15) & 1) + lo);
	}
    }
    angle = r;
done:
    return neg ? (WWFixedAsDWord) (0 - angle) : angle;
}

/* GrFastCosine: the sine of 90 - angle */
WWFixedAsDWord GrQuickCosine(WWFixedAsDWord angle)
{
    Boolean neg = FALSE;
    sword a;
    WWFixedAsDWord s;

    if ((sdword) angle < 0)
	angle = (WWFixedAsDWord) (0 - angle);
    a = (sword) (angle >> 16);
    while (a >= 360)
	a -= 360;
    angle = GR_WWF(a, angle);
    if (a >= 90) {
	if (a >= 270)
	    angle = (WWFixedAsDWord) (0 - angle) + GR_WWF(360, 0);
	else if (a >= 180) {
	    angle -= GR_WWF(180, 0);
	    neg = TRUE;
	} else {
	    angle = (WWFixedAsDWord) (0 - angle) + GR_WWF(180, 0);
	    neg = TRUE;
	}
    }
    s = GrQuickSine((WWFixedAsDWord) (0 - angle) + GR_WWF(90, 0));
    return neg ? (WWFixedAsDWord) (0 - s) : s;
}

WWFixedAsDWord GrQuickTangent(WWFixedAsDWord angle)
{
    return GrSDivWWFixed(GrQuickSine(angle), GrQuickCosine(angle));
}

/*
 * GrFastArcSine: the angle of a sine, in the quadrant the signs of the
 * sine and of origDeltaX give.  BinarySearch is kept as the asm had it.
 */
WWFixedAsDWord GrQuickArcSine(WWFixedAsDWord deltaYDivDistance,
			      word origDeltaX)
{
    WWFixedAsDWord s = deltaYDivDistance, r;
    word cx, lo = 0, hi = 90, mid, val, off;
    Boolean exact;

    if ((sdword) s < 0)
	s = (WWFixedAsDWord) (0 - s);
    if ((s >> 16) == 1)
	r = GR_WWF(90, 0);
    else {
	cx = (word) s;
	for (;;) {			/* BinarySearch */
	    mid = (word) ((lo + hi) >> 1);
	    val = grSinIntTable[mid];
	    if (hi < lo) {
		off = mid;
		exact = FALSE;
		break;
	    }
	    if (val < cx)
		lo = (word) (mid + 1);
	    else if (val == cx) {
		off = mid;
		exact = TRUE;
		break;
	    } else
		hi = (word) (mid - 1);
	}
	val = grSinIntTable[off];
	r = 0;
	if (!exact)
	    GrUDiv32(GR_WWF(cx - val, 0),
		     GR_WWF(grSinIntTable[off + 1] - val, 0), &r);
	r = GR_WWF(off, r);
    }
    /* the other quadrants: only the integer is mirrored, as in the asm */
    if ((sdword) deltaYDivDistance < 0)
	r = (sword) origDeltaX < 0 ? r + GR_WWF(180, 0) :
				     GR_WWF(360 - (r >> 16), r);
    else if ((sword) origDeltaX < 0)
	r = GR_WWF(180 - (r >> 16), r);
    return r;
}

/*
 * DWFixed: magnitudes of 48 bits as three 16-bit limbs, low first.
 */
static Boolean Mag48(const DWFixed *v, uint32_t l[3])
{
    uint64_t m = ((uint64_t) (uint32_t) v->DWF_int << 16) | v->DWF_frac;
    Boolean neg = v->DWF_int < 0;

    if (neg)
	m = ((uint64_t) 1 << 48) - m;
    m &= ((uint64_t) 1 << 48) - 1;
    l[0] = (uint32_t) (m & 0xffff);
    l[1] = (uint32_t) ((m >> 16) & 0xffff);
    l[2] = (uint32_t) (m >> 32);
    return neg;
}

static void SetDWF(DWFixed *d, uint64_t m, Boolean neg)
{
    if (neg)
	m = (uint64_t) 0 - m;
    d->DWF_frac = (word) m;
    d->DWF_int = (sdword) (uint32_t) (m >> 16);
}

/* GrMulDWFixedPtr: the product, the fraction truncated */
void GrMulDWFixed(const DWFixed *i, const DWFixed *j, DWFixed *result)
{
    uint32_t a[3], b[3];
    uint64_t limb[6], c = 0, r;
    Boolean neg = (Boolean) (Mag48(i, a) != Mag48(j, b));
    int x, y;

    for (x = 0; x < 6; x++)
	limb[x] = 0;
    for (x = 0; x < 3; x++)
	for (y = 0; y < 3; y++)
	    limb[x + y] += (uint64_t) a[x] * b[y];
    for (x = 0; x < 6; x++) {		/* carry through */
	limb[x] += c;
	c = limb[x] >> 16;
	limb[x] &= 0xffff;
    }
    r = limb[1] | (limb[2] << 16) | (limb[3] << 32);
    SetDWF(result, r, neg);
}

/* the quotient rounded to 1/65536 */
void GrSDivDWFbyWWF(const DWFixed *dividend, const WWFixed *divisor,
		    DWFixed *quotient)
{
    uint32_t a[3];
    Boolean neg = Mag48(dividend, a);
    uint64_t num = ((uint64_t) a[2] << 32) | ((uint64_t) a[1] << 16) | a[0];
    uint32_t den = Abs32((sdword) GR_WWF(divisor->WWF_int,
					 divisor->WWF_frac));
    uint64_t q, rem;

    if (divisor->WWF_int < 0)
	neg = (Boolean) !neg;
    if (!den) {
	SetDWF(quotient, ((uint64_t) 1 << 47) - 1, neg);
	return;
    }
    q = (num << 16) / den;
    rem = (num << 16) % den;
    if (((rem << 16) / den) & 0x8000)
	q++;
    SetDWF(quotient, q, neg);
}
