/*
 * vidcomVariable.c -- after vidcomVariable.def: the driver's variables
 * that more than one file uses (declared in vidcom.h); the others are
 * static in the file that has them.
 */
#include "vidcom.h"

Rectangle exclBound;
SemaphoreHandle videoSem;
