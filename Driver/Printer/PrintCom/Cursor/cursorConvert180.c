/*
 * cursorConvert180.c -- 1/180" driver coordinates (the C port of
 * cursorConvert180.asm).
 */
#include "printcom.h"

/* points (WWFixed) to 1/180": the integer part of 2.5 times it, as the
 * asm (twice it plus half of it) */
word PrConvertToDriverCoordinates(WWFixedAsDWord points)
{
    return (word) (((points << 1) + (WWFixedAsDWord) ((sdword) points >> 1))
		   >> 16);
}

/* 1/180" to points (WWFixed) */
WWFixedAsDWord PrConvertFromDriverCoordinates(word units)
{
    return GrUDivWWFixed((WWFixedAsDWord) units << 16, 0x28000UL);
}
