/*
 * fileEC.c -- after fileEC.asm: ECCheckFileHandle.
 */
#include "kernelC.h"
#include "ghal.h"

void ECCheckFileHandle(FileHandle fh)	    { ECCheckHandleType(fh, SIG_FILE, FALSE); }
