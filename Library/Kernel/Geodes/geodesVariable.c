/*
 * geodesVariable.c -- after geodesVariable.def: the variables of the module that
 * the port has, under their asm names.
 */
#include "kernelC.h"
#include <stdlib.h>
#include <geosdriver.h>
#include <string.h>
#include <vm.h>
#include "geodesVariable.h"

KSem privSem = { 1, 0 };

KSem geodeSem = { 1, 0 };
GeodeHandle geodeListPtr;

GeodeHandle systemDriverList[MAX_SYSTEM_DRIVERS];
GeodeHandle defaultDrivers[GDDT_TASK / 2 + 1];
