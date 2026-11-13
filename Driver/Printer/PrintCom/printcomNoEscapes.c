/*
 * printcomNoEscapes.c -- the escape table of a driver without escapes
 * (the C port of printcomNoEscapes.asm).
 */
#include "printcom.h"

const PrintEscEntry printEscTable[1] = { { 0, NULL } };
const word printNumEscapes = 0;
