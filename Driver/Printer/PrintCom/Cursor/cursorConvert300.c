/*
 * cursorConvert300.c -- points and 1/300" (the C port of
 * cursorConvert300.asm): the factor 4.17 (4 + 11141/65536).
 */
#include "printcom.h"

#define PR_300_FACTOR	((4UL << 16) | 11141)

/* points (WWFixed) to 1/300": the integer part */
word PrConvertToDriverCoordinates(WWFixedAsDWord points)
{
    return (word) (GrMulWWFixed(points, PR_300_FACTOR) >> 16);
}

/* 1/300" to points (WWFixed) */
WWFixedAsDWord PrConvertFromDriverCoordinates(word units)
{
    return GrUDivWWFixed((WWFixedAsDWord) units << 16, PR_300_FACTOR);
}
