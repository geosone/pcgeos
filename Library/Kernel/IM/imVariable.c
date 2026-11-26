/*
 * imVariable.c -- after imVariable.def: the variables of the module that
 * the port has, under their asm names.
 */
#include "kernelC.h"
#include <Internal/videoDr.h>
#include <lmem.h>
#include <timer.h>
#include <system.h>
#include <input.h>
#include <win.h>
#include "imInt.h"

KSem semMonChain = { 1, 0 };
Monitor headMonitor;	/* M_monRoutine NULL: the start */
const byte buttonConvTab[IM_MAX_BUTTONS] = {
    BI_B0_DOWN, BI_B1_DOWN, BI_B2_DOWN, BI_B3_DOWN
};

KSem semPtrImage = { 1, 0 };

Monitor combineMonitor;
Monitor outputMonitor;
