/*
 * cursorConvert288.c -- 1/288" driver coordinates (the C port of
 * cursorConvert288.asm).
 */
#include "printcom.h"

/* points (WWFixed) to 1/288": the integer part of four times it */
word PrConvertToDriverCoordinates(WWFixedAsDWord points)
{
    return (word) ((points << 2) >> 16);
}

/* 1/288" to points (WWFixed) */
WWFixedAsDWord PrConvertFromDriverCoordinates(word units)
{
    return (WWFixedAsDWord) ((sdword) (sword) units << 14);
}
