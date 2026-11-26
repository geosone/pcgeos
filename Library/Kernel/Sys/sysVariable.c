/*
 * sysVariable.c -- after sysVariable.def: the variables of the module that
 * the port has, under their asm names.
 */
#include "kernelC.h"
#include <sysstats.h>
#include <geode.h>
#include <object.h>
#include <geosdriver.h>
#include <Internal/powerDr.h>
#include <Internal/videoDr.h>
#include <timer.h>
#include <input.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <localize.h>
#include <ec.h>
#include <system.h>
#include "sysVariable.h"

KSem shutdownBroadcastSem = { 1, 0 };
KSem shutdownConfirmSem = { 1, 0 };
dword shutdownConfirmCount;
Boolean shutdownOK = TRUE;
optr shutdownAckOD;
Message shutdownAckMsg;
ByteFlags sysConfig = SCF_RTC | SCF_COPROC;
ErrorCheckingFlags sysECLevel;
MemHandle sysECBlock;

SubsystemNotificationElement sysNotificationTable[SST_COUNT];

SysStats curStats;	/* SysStatistics: this second */
SysStats lastStats;	/* and the last */
