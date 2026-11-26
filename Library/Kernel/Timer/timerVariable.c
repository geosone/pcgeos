/*
 * timerVariable.c -- after timerVariable.def: the variables of the module that
 * the port has, under their asm names.
 */
#include "kernelC.h"
#include "timerVariable.h"

KTimer *timeListPtr;	/* sorted by TI_due */
word timerID = 1;

dword decayCount = TIMER_INTERRUPT_RATE;

KTimer *realTimeListPtr;	/* sorted by TI_due (date, time) */
