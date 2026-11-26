/*
 * floatTrans.c -- after floatTrans.asm: Transcendental Functions
 */
#include <assert.h>
#include <math.h>
#include <string.h>
#include "fpstack.h"

void FloatLog(void) { FloatPushValue(log10(FloatPopValue())); }
