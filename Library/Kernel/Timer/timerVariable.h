/*
 * timerVariable.h -- after timerVariable.def: the variables of the module
 * (defined in timerVariable.c).
 */
#ifndef H_TIMERVARIABLE
#define H_TIMERVARIABLE

#include "timerConstant.h"

extern KTimer *timeListPtr;
extern word timerID;

extern dword decayCount;

extern KTimer *realTimeListPtr;

#endif /* H_TIMERVARIABLE */
