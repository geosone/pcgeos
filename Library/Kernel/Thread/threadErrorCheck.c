/*
 * threadErrorCheck.c -- after threadErrorCheck.asm: ECCheckThreadHandle.
 */
#include "kernelC.h"
#include "ghal.h"

void ECCheckThreadHandle(ThreadHandle th)   { ECCheckHandleType(th, SIG_THREAD, FALSE); }
