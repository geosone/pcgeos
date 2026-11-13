/*
 * cursorConvert216.c -- 1/216" driver coordinates (the C port of
 * cursorConvert216.asm).
 */
#include "printcom.h"

/* points (WWFixed) to 1/216": the integer part of three times it, as the
 * asm (twice it, rounded down, plus the integer) */
word PrConvertToDriverCoordinates(WWFixedAsDWord points)
{
    return (word) ((word) ((points << 1) >> 16) + (word) (points >> 16));
}

/* 1/216" to points (WWFixed) */
WWFixedAsDWord PrConvertFromDriverCoordinates(word units)
{
    return GrUDivWWFixed((WWFixedAsDWord) units << 16, 3UL << 16);
}
