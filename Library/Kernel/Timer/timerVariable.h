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

/* Port32: the ended timers, to be freed by TimerReap (timerList.c;
 * CheckRealTimeTimers of timerInt.c adds to them) */
extern KTimer *endedList;

#endif /* H_TIMERVARIABLE */
