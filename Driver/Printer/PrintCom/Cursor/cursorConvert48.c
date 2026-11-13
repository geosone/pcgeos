/*
 * cursorConvert48.c -- 1/48" driver coordinates (the C port of
 * cursorConvert48.asm).
 */
#include "printcom.h"

/* points (WWFixed) to 1/48": the integer part of two thirds of it */
word PrConvertToDriverCoordinates(WWFixedAsDWord points)
{
    return (word) (GrMulWWFixed(points, 43690) >> 16);
}

/* 1/48" to points (WWFixed): one and a half times */
WWFixedAsDWord PrConvertFromDriverCoordinates(word units)
{
    return (WWFixedAsDWord) ((sdword) (sword) units * 0x18000L);
}
