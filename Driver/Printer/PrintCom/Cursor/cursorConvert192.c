/*
 * cursorConvert192.c -- PrConvertToDriverCoordinates and
 * PrConvertFromDriverCoordinates (the C port of cursorConvert192.asm):
 * points and 1/192" (2.6667 a point).
 */
#include "printcom.h"

#define PR_192_FACTOR	((2UL << 16) | 43691)

/* points (WWFixed) to 1/192": the integer part */
word PrConvertToDriverCoordinates(WWFixedAsDWord points)
{
    return (word) (GrMulWWFixed(points, PR_192_FACTOR) >> 16);
}

/* 1/192" to points (WWFixed) */
WWFixedAsDWord PrConvertFromDriverCoordinates(word units)
{
    return GrUDivWWFixed((WWFixedAsDWord) units << 16, PR_192_FACTOR);
}
