/*
 * geodesVariable.h -- after geodesVariable.def: the variables of the module
 * (defined in geodesVariable.c).
 */
#ifndef H_GEODESVARIABLE
#define H_GEODESVARIABLE

#include <driver.h>
#include "geodesConstant.h"

extern KSem privSem;

extern KSem geodeSem;
extern GeodeHandle geodeListPtr;

extern GeodeHandle systemDriverList[MAX_SYSTEM_DRIVERS];
extern GeodeHandle defaultDrivers[GDDT_TASK / 2 + 1];

#endif /* H_GEODESVARIABLE */
