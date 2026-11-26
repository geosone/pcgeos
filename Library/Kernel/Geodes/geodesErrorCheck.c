/*
 * geodesErrorCheck.c -- after geodesErrorCheck.asm:
 * ECCheckResourceHandle, ECCheckGeodeHandle, ECCheckProcessHandle,
 * ECCheckDriverHandle, ECCheckLibraryHandle, ECCheckQueueHandle,
 * ECCheckStack.
 */
#include "kernelC.h"
#include "ghal.h"

void ECCheckResourceHandle(MemHandle mh)    { ECCheckHandleType(mh, HT_MEM, FALSE); }
void ECCheckGeodeHandle(GeodeHandle gh)	    { ECCheckHandleType(gh, HT_MEM, FALSE); }
void ECCheckProcessHandle(GeodeHandle gh)   { ECCheckHandleType(gh, HT_MEM, FALSE); }
void ECCheckDriverHandle(GeodeHandle gh)    { ECCheckHandleType(gh, HT_MEM, FALSE); }
void ECCheckLibraryHandle(GeodeHandle gh)   { ECCheckHandleType(gh, HT_MEM, FALSE); }
void ECCheckQueueHandle(QueueHandle qh)	    { ECCheckHandleType(qh, SIG_QUEUE, FALSE); }
void ECCheckStack(void)			    { }
