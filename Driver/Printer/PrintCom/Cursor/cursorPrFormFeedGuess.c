/*
 * cursorPrFormFeedGuess.c -- PrFormFeed (the C port of
 * cursorPrFormFeedGuess.asm): a form feed, the printer's page length
 * taken as it is.
 */
#include "printcom.h"

Boolean PrFormFeed(PState *ps)
{
    return PrintStreamWriteByte(ps, C_FF);
}
