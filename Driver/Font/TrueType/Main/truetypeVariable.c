/*
 * truetypeVariable.c -- after truetypeVariable.def: the variables of the
 * TrueType font driver (declared in ../Adapter/ttadapter.h).
 */
#include "../Adapter/ttadapter.h"

MemHandle bitmapHandle;			/* the picture of a character */
MemHandle variableHandle;		/* TrueTypeVars */
TEngine_Instance engineInstance;	/* the FreeType engine */
