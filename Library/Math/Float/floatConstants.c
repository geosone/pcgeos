/*
 * floatConstants.c -- after floatConstants.asm: File containing routines
 * that place commonly used numeric constants on the floating point
 * stack.
 */
#include <assert.h>
#include <math.h>
#include <string.h>
#include "fpstack.h"

/* --- constants --- */
void Float0(void)  { FloatPushValue(0.0); }
void Float1(void)  { FloatPushValue(1.0); }
void Float5(void)  { FloatPushValue(5.0); }
void Float10(void) { FloatPushValue(10.0); }
