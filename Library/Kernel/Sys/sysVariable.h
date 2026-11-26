/*
 * sysVariable.h -- after sysVariable.def: the variables of the module
 * (defined in sysVariable.c).
 */
#ifndef H_SYSVARIABLE
#define H_SYSVARIABLE

#include <ec.h>
#include <sysstats.h>
#include "sysConstant.h"

extern KSem shutdownBroadcastSem;
extern KSem shutdownConfirmSem;
extern dword shutdownConfirmCount;
extern Boolean shutdownOK;
extern optr shutdownAckOD;
extern Message shutdownAckMsg;
extern ErrorCheckingFlags sysECLevel;
extern MemHandle sysECBlock;

extern SubsystemNotificationElement sysNotificationTable[SST_COUNT];

extern SysStats curStats;
extern SysStats lastStats;

#endif /* H_SYSVARIABLE */
