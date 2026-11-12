/*
 * graphicsTransformUtils.c -- transformation matrices (after
 * graphicsTransformUtils.asm: ScaleCommon, RotateCommon, TMatrixPreMul,
 * ComposeFullMatrix, CalcInverse, TransCoordFixed, UnTransCoordFixed;
 * graphicsUtils.asm: TransCoordCommon, TransComplex, TComplex;
 * graphicsPathLow.asm: SetTMatrixFlags).
 *
 * A point is a row vector: [x' y' 1] = [x y 1] * TMatrix, so
 *	x' = 11 x + 21 y + 31,	y' = 12 x + 22 y + 32.
 * The 2x2 part is WWFixed, the translation DWFixed.
 *
 * The asm multiplied only the elements that are not 0 or 1 (by the
 * flags); here one matrix product does every case.  As each product is
 * the asm's (GrMulWWFixed and MulWWFbyDWF: the magnitude, truncated)
 * and the sums are exact, the results are the same.
 */
#include "graphicsInt.h"

/* the elements as plain numbers: WWFixed raw 32 bits, DWFixed 48 */
static sdword W(const WWFixed *w)
{
    return (sdword) GR_WWF(w->WWF_int, w->WWF_frac);
}

static void SetW(WWFixed *w, sdword v)
{
    w->WWF_int = (sword) ((dword) v >> 16);
    w->WWF_frac = (word) v;
}

int64_t GrDWF(const DWFixed *d)
{
    return (int64_t) ((uint64_t) (int64_t) d->DWF_int << 16) | d->DWF_frac;
}

void GrSetDWF(DWFixed *d, int64_t v)
{
    d->DWF_frac = (word) v;
    d->DWF_int = (sdword) (uint32_t) ((uint64_t) v >> 16);
}

/* MulWWFbyDWF: the product of the magnitudes, truncated */
int64_t GrMulWD(sdword w, int64_t d)
{
    uint64_t a = w < 0 ? (uint64_t) 0 - (uint64_t) (int64_t) w : (uint64_t) w;
    uint64_t b = d < 0 ? (uint64_t) 0 - (uint64_t) d : (uint64_t) d;
    uint64_t r = a * (b >> 16) + ((a * (b & 0xffff)) >> 16);

    r &= ((uint64_t) 1 << 48) - 1;
    return ((w < 0) != (d < 0)) ? -(int64_t) r : (int64_t) r;
}

static sdword MulWW(sdword a, sdword b)
{
    return (sdword) GrMulWWFixed((WWFixedAsDWord) a, (WWFixedAsDWord) b);
}

/* SetTMatrixFlags, with CheckForInteger: x.0001 and x.ffff become whole */
void GrMatSetFlags(TMatrix *m)
{
    WWFixed *e[4];
    TransFlags f = 0;
    int i;

    e[0] = &m->TM_11; e[1] = &m->TM_22; e[2] = &m->TM_21; e[3] = &m->TM_12;
    for (i = 0; i < 4; i++)
	if (e[i]->WWF_frac == 1)
	    e[i]->WWF_frac = 0;
	else if (e[i]->WWF_frac == 0xffff) {
	    e[i]->WWF_int++;
	    e[i]->WWF_frac = 0;
	}
    if (GrDWF(&m->TM_31) || GrDWF(&m->TM_32))
	f |= TF_TRANSLATED;
    if (W(&m->TM_12) || W(&m->TM_21))
	f |= TF_ROTATED | TF_SCALED;
    if (W(&m->TM_11) != 0x10000L || W(&m->TM_22) != 0x10000L)
	f |= TF_SCALED;
    m->TM_flags = f;
}

void GrMatIdentity(TMatrix *m)
{
    memset(m, 0, sizeof(*m));
    m->TM_11.WWF_int = 1;
    m->TM_22.WWF_int = 1;
}

/*
 * r = a * b (r may be a or b).  The flags: the two together, as the
 * asm's compose (TMatrixPreMul callers recalculate them).
 */
void GrMatMul(TMatrix *r, const TMatrix *a, const TMatrix *b)
{
    sdword a11 = W(&a->TM_11), a12 = W(&a->TM_12), a21 = W(&a->TM_21),
	   a22 = W(&a->TM_22), b11 = W(&b->TM_11), b12 = W(&b->TM_12),
	   b21 = W(&b->TM_21), b22 = W(&b->TM_22);
    int64_t a31 = GrDWF(&a->TM_31), a32 = GrDWF(&a->TM_32);
    int64_t t31 = GrMulWD(b11, a31) + GrMulWD(b21, a32) + GrDWF(&b->TM_31);
    int64_t t32 = GrMulWD(b12, a31) + GrMulWD(b22, a32) + GrDWF(&b->TM_32);
    TransFlags f = (TransFlags) ((a->TM_flags | b->TM_flags) &
				 ~TF_INV_VALID);

    SetW(&r->TM_11, (sdword) ((dword) MulWW(a11, b11) + (dword) MulWW(a12, b21)));
    SetW(&r->TM_12, (sdword) ((dword) MulWW(a11, b12) + (dword) MulWW(a12, b22)));
    SetW(&r->TM_21, (sdword) ((dword) MulWW(a21, b11) + (dword) MulWW(a22, b21)));
    SetW(&r->TM_22, (sdword) ((dword) MulWW(a21, b12) + (dword) MulWW(a22, b22)));
    GrSetDWF(&r->TM_31, t31);
    GrSetDWF(&r->TM_32, t32);
    r->TM_flags = f;
}

/* ScaleCommon: m = scale * m */
void GrMatScale(TMatrix *m, WWFixedAsDWord sx, WWFixedAsDWord sy)
{
    SetW(&m->TM_11, MulWW(W(&m->TM_11), (sdword) sx));
    SetW(&m->TM_12, MulWW(W(&m->TM_12), (sdword) sx));
    SetW(&m->TM_21, MulWW(W(&m->TM_21), (sdword) sy));
    SetW(&m->TM_22, MulWW(W(&m->TM_22), (sdword) sy));
    GrMatSetFlags(m);
}

/* RotateCommon: m = [cos -sin; sin cos] * m */
void GrMatRotate(TMatrix *m, WWFixedAsDWord angle)
{
    sdword c = (sdword) GrQuickCosine(angle), s = (sdword) GrQuickSine(angle);
    sdword m11 = W(&m->TM_11), m12 = W(&m->TM_12), m21 = W(&m->TM_21),
	   m22 = W(&m->TM_22);

    SetW(&m->TM_11, (sdword) ((dword) MulWW(m11, c) + (dword) MulWW(m21, -s)));
    SetW(&m->TM_21, (sdword) ((dword) MulWW(m11, s) + (dword) MulWW(m21, c)));
    SetW(&m->TM_22, (sdword) ((dword) MulWW(m22, c) + (dword) MulWW(m12, s)));
    SetW(&m->TM_12, (sdword) ((dword) MulWW(m22, -s) + (dword) MulWW(m12, c)));
    GrMatSetFlags(m);
}

/* PreApplyTranslation, PreApplyExtTranslation: m = translation * m;
 * tx, ty in 1/65536 */
void GrMatTranslate(TMatrix *m, int64_t tx, int64_t ty)
{
    GrSetDWF(&m->TM_31, GrDWF(&m->TM_31) + GrMulWD(W(&m->TM_11), tx) +
	     GrMulWD(W(&m->TM_21), ty));
    GrSetDWF(&m->TM_32, GrDWF(&m->TM_32) + GrMulWD(W(&m->TM_12), tx) +
	     GrMulWD(W(&m->TM_22), ty));
    m->TM_flags = (TransFlags) ((m->TM_flags & ~TF_INV_VALID) |
				TF_TRANSLATED);
}

void GrMatFromTransMatrix(TMatrix *m, const TransMatrix *tm)
{
    m->TM_11 = tm->TM_e11;
    m->TM_12 = tm->TM_e12;
    m->TM_21 = tm->TM_e21;
    m->TM_22 = tm->TM_e22;
    m->TM_31 = tm->TM_e31;
    m->TM_32 = tm->TM_e32;
    GrMatSetFlags(m);
}

void GrMatToTransMatrix(const TMatrix *m, TransMatrix *tm)
{
    tm->TM_e11 = m->TM_11;
    tm->TM_e12 = m->TM_12;
    tm->TM_e21 = m->TM_21;
    tm->TM_e22 = m->TM_22;
    tm->TM_e31 = m->TM_31;
    tm->TM_e32 = m->TM_32;
}

/* p' = p * m, p in 1/65536 (TransCoordDWFixed, TComplex) */
void GrMatTransDWF(const TMatrix *m, int64_t *x, int64_t *y)
{
    int64_t px = *x, py = *y;

    if (m->TM_flags & TM_COMPLEX) {
	px = GrMulWD(W(&m->TM_11), *x) + GrMulWD(W(&m->TM_21), *y);
	py = GrMulWD(W(&m->TM_22), *y) + GrMulWD(W(&m->TM_12), *x);
    }
    *x = px + GrDWF(&m->TM_31);
    *y = py + GrDWF(&m->TM_32);
}

/* the integer of a value in 1/65536, rounded (+ 0x8000); FALSE if it is
 * no sword (CheckDWordResult) */
Boolean GrRoundToWord(int64_t v, sword *r)
{
    int64_t i = (v + 0x8000) >> 16;

    *r = (sword) i;
    return i >= -32768 && i <= 32767;
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

/*
 * CalcInverse: for a scaled matrix 1/11 and 1/22 (GrReciprocal32), for
 * a rotated one 1 / (11 22 - 12 21) as DDFixed (32.32), its low word
 * dropped when the high word of the fraction is TM_WFRAC_MIN or more.
 */
static void SetDDF(DDFixed *d, int64_t v)
{
    d->DDF_frac = (dword) v;
    d->DDF_int = (sdword) (uint32_t) ((uint64_t) v >> 32);
}

static int64_t DDF(const DDFixed *d)
{
    return (int64_t) (((uint64_t) (uint32_t) d->DDF_int << 32) | d->DDF_frac);
}

void GrMatCalcInverse(TMatrix *m)
{
    if (m->TM_flags & TF_INV_VALID)
	return;
    if (!(m->TM_flags & TM_ROTATED)) {
	sdword x = W(&m->TM_11), y = W(&m->TM_22);
	SetDDF(&m->TM_xInv, (int64_t) (x ? (sdword) GrReciprocal32(x) : 0)
	       << 16);
	SetDDF(&m->TM_yInv, (int64_t) (y ? (sdword) GrReciprocal32(y) : 0)
	       << 16);
    } else {
	int64_t det = (int64_t) W(&m->TM_11) * W(&m->TM_22) -
		      (int64_t) W(&m->TM_12) * W(&m->TM_21);
	uint64_t d = det < 0 ? (uint64_t) 0 - (uint64_t) det : (uint64_t) det;
	uint64_t q = 0;
	int64_t inv;

	if (d > 1) {			/* 2^64 / d */
	    q = ~(uint64_t) 0 / d;
	    if (~(uint64_t) 0 % d == d - 1)
		q++;
	} else if (d)
	    q = ~(uint64_t) 0 >> 1;
	inv = det < 0 ? -(int64_t) q : (int64_t) q;
	if (((uint64_t) inv & 0xffff0000UL) >= ((uint64_t) TM_WFRAC_MIN << 16))
	    inv &= ~(int64_t) 0xffff;
	SetDDF(&m->TM_xInv, inv);
	SetDDF(&m->TM_yInv, inv);
    }
    m->TM_flags |= TF_INV_VALID;
}

/* MulDDF: n (1/65536) * inv (1/2^32), in 1/2^32, the magnitude
 * truncated */
static int64_t MulDDF(int64_t n, int64_t inv)
{
    uint64_t a = n < 0 ? (uint64_t) 0 - (uint64_t) n : (uint64_t) n;
    uint64_t b = inv < 0 ? (uint64_t) 0 - (uint64_t) inv : (uint64_t) inv;
    uint64_t hi, lo, r;

    MulU64(a, b, &hi, &lo);
    r = (lo >> 16) | (hi << 48);
    return ((n < 0) != (inv < 0)) ? -(int64_t) r : (int64_t) r;
}

/* UnTransCoordFixed: p = p' * m^-1, in 1/65536 */
void GrMatUntransDWF(TMatrix *m, int64_t *x, int64_t *y)
{
    int64_t dx = *x - GrDWF(&m->TM_31), dy = *y - GrDWF(&m->TM_32);

    if (!(m->TM_flags & TM_COMPLEX)) {
	*x = dx;
	*y = dy;
	return;
    }
    GrMatCalcInverse(m);
    if (!(m->TM_flags & TM_ROTATED)) {
	*x = GrMulWD((sdword) (DDF(&m->TM_xInv) >> 16), dx);
	*y = GrMulWD((sdword) (DDF(&m->TM_yInv) >> 16), dy);
    } else {
	int64_t nx = GrMulWD(W(&m->TM_22), dx) - GrMulWD(W(&m->TM_21), dy);
	int64_t ny = GrMulWD(W(&m->TM_11), dy) - GrMulWD(W(&m->TM_12), dx);
	*x = (MulDDF(nx, DDF(&m->TM_xInv)) + 0x8000) >> 16;
	*y = (MulDDF(ny, DDF(&m->TM_yInv)) + 0x8000) >> 16;
    }
}
