/*
 * cursorConvert144.c -- 1/144" driver coordinates (the C port of
 * cursorConvert144.asm).
 */
#include "printcom.h"

/* points (WWFixed) to 1/144": the integer part of twice it */
word PrConvertToDriverCoordinates(WWFixedAsDWord points)
{
    return (word) ((points << 1) >> 16);
}

/* 1/144" to points (WWFixed) */
WWFixedAsDWord PrConvertFromDriverCoordinates(word units)
{
    return (WWFixedAsDWord) ((sdword) (sword) units << 15);
}
