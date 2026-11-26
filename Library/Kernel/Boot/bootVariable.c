/*
 * bootVariable.c -- after bootVariable.def: the variables of the module that
 * the port has, under their asm names.
 */
#include "kernelC.h"
#include <system.h>

ExitFlags exitFlags;	/* EndGeos reads EF_RESET */
