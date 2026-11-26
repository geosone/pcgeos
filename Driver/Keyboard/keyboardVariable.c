/*
 * keyboardVariable.c -- after keyboardVariable.def: the driver's
 * variables that the port has (declared in keyboard.h).
 */
#include "keyboard.h"

Monitor kbdMonitor;

const byte bitTable[8] = { 0x01, 0x02, 0x04, 0x08,
				  0x10, 0x20, 0x40, 0x80 };
