/*
 * lmemErrorCheck.c -- after lmemErrorCheck.asm: ECCheckLMemHandle,
 * ECCheckLMemHandleNS.
 */
#include "kernelC.h"
#include "ghal.h"

void ECCheckLMemHandle(MemHandle mh)	    { ECCheckHandleType(mh, HT_MEM, FALSE); }
void ECCheckLMemHandleNS(MemHandle mh)	    { ECCheckHandleType(mh, HT_MEM, FALSE); }
