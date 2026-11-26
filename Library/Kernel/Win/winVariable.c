/*
 * winVariable.c -- after winVariable.def: the variables of the module that
 * the port has, under their asm names.
 */
#include "kernelC.h"
#include <stdlib.h>
#include "winInt.h"

KSem winTreeSem;

KSem winPtrImageSem = { 1, 0 };

word wGeodeWinVarsOffset;
GeodeHandle wPtrActiveGeode;
