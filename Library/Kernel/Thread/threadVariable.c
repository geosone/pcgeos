/*
 * threadVariable.c -- after threadVariable.def: the variables of the module that
 * the port has, under their asm names.
 */
#include "kernelC.h"
#include <string.h>
#include <sysstats.h>
#include "threadVariable.h"

Handle runQueue;	/* ready threads, via T_nextQ */
byte threadTimer = TIME_SLICE;	/* ticks of the running thread */
ThreadHandle currentThread;
