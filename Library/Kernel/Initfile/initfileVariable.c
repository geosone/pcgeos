/*
 * initfileVariable.c -- after initfileVariable.def: the variables of the module that
 * the port has, under their asm names.
 */
#include "kernelC.h"
#include <string.h>
#include "initfileInt.h"

KSem initFileSem = { 1, 0 };
dword initfileLastModified;
const char initfileBackupName[] = "ini.bak";
