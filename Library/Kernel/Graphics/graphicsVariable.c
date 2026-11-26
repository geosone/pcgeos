/*
 * graphicsVariable.c -- after graphicsVariable.def: the variables of the module that
 * the port has, under their asm names.
 */
#include "kernelC.h"
#include <stdlib.h>
#include <initfile.h>
#include <fileEnum.h>
#include <Internal/fontDr.h>

MemHandle fontBlkHandle;
FontID defaultFontID = DEFAULT_FONT_ID;
word defaultFontSize = DEFAULT_FONT_SIZE;
Handle defaultFontHandle;
