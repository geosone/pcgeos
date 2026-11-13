/*
 * cursorConvert360.c -- 1/360" driver coordinates (the C port of
 * cursorConvert360.asm).
 */
#include "printcom.h"

/* points (WWFixed) to 1/360": the integer part of five times it */
word PrConvertToDriverCoordinates(WWFixedAsDWord points)
{
    return (word) ((points * 5) >> 16);
}

/* 1/360" to points (WWFixed) */
WWFixedAsDWord PrConvertFromDriverCoordinates(word units)
{
    return GrUDivWWFixed((WWFixedAsDWord) units << 16, 5UL << 16);
}
