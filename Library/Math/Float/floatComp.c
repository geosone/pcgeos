/*
 * floatComp.c -- after floatComp.asm: Comparison routines for the
 * floating point library.
 */
#include <assert.h>
#include <math.h>
#include <string.h>
#include "fpstack.h"

int FloatEq0(void) { return FloatPopValue() == 0.0; }
