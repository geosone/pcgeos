/*
 * pscriptControlCodes.c -- the control codes of the driver (the C port
 * of pscriptControlCodes.asm): a count byte, then the bytes.
 */
#include "pscript.h"

#define C_ESC	0x1b
#define C_CR	0x0d
#define C_LF	0x0a

/* PJL: the universal exit language code */
const byte pr_codes_PJLUEL[] = { 9, C_ESC, '%', '-', '1', '2', '3', '4',
    '5', 'X' };
const byte pr_codes_PJLCRLF[] = { 6, '@', 'P', 'J', 'L', C_CR, C_LF };
const byte pr_codes_PJLEnterPostScript[] =
    "\042@PJL ENTER LANGUAGE = POSTSCRIPT\r\n";
const byte pr_codes_PJLMsgPScript[] =
    "\045@PJL RDYMSG DISPLAY = \"GEOS PS JOB\"\r\n";
const byte pr_codes_PJLMsgClear[] = "\032@PJL RDYMSG DISPLAY = \"\"\r\n";

/* IBM: set initial conditions; the tray byte follows the first */
const byte pr_codes_IBMSelectTraySIC[] = { 11, C_ESC, '[', 'K', 7, 0, 0,
    49, 1, 0, 0, 0 };
const byte pr_codes_IBMEnterPostScriptSIC[] = { 8, C_ESC, '[', 'K', 3, 0,
    0, 49, 8 };
