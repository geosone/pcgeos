/*
 * graphicsMathObscure.c -- after graphicsMathObscure.asm: GrMulDWFixed,
 * GrSDivDWFbyWWF, GrMulDDF.
 */
#include "graphicsInt.h"
#include <stdint.h>

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
    uint32_t den = GrAbs32((sdword) GR_WWF(divisor->WWF_int,
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

/* the 128-bit product of two magnitudes */
static void MulU64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)
{
    uint64_t al = a & 0xffffffffUL, ah = a >> 32, bl = b & 0xffffffffUL,
	     bh = b >> 32, m1 = ah * bl, m2 = al * bh, l = al * bl, c;

    c = (l >> 32) + (m1 & 0xffffffffUL) + (m2 & 0xffffffffUL);
    *lo = (l & 0xffffffffUL) | (c << 32);
    *hi = ah * bh + (m1 >> 32) + (m2 >> 32) + (c >> 32);
}

/* MulDDF: n (1/65536) * inv (1/2^32), in 1/2^32, the magnitude
 * truncated */
int64_t GrMulDDF(int64_t n, int64_t inv)
{
    uint64_t a = n < 0 ? (uint64_t) 0 - (uint64_t) n : (uint64_t) n;
    uint64_t b = inv < 0 ? (uint64_t) 0 - (uint64_t) inv : (uint64_t) inv;
    uint64_t hi, lo, r;

    MulU64(a, b, &hi, &lo);
    r = (lo >> 16) | (hi << 48);
    return ((n < 0) != (inv < 0)) ? -(int64_t) r : (int64_t) r;
}
