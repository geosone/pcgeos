/*
 * initfileInt.h -- inside the init file module of the C kernel
 * (initfileLow.c, initfileConstruct.c, initfileHigh.c).
 *
 * An init file is held as its text followed by C_CTRL_Z (the DOS end of
 * a text file, as in the buffers of the asm kernel), so the scanner needs
 * no length: every routine stops at the C_CTRL_Z.  Positions are offsets
 * into the text.
 *
 * The format (Port32/ABI.md section 13h):
 *	[category]
 *	key = value		; comment
 *	key = {
 *	a value of several lines (a "blob")
 *	}
 * Categories and keys compare without case and without spaces and tabs.
 */
#ifndef INITFILEINT_H
#define INITFILEINT_H

#include "kernelC.h"
#include <initfile.h>

#define INI_EOF		0x1a		/* C_CTRL_Z */
#define INI_COMMENT	';'
#define INI_MAX_SIZE	0xfff0		/* the text, as the asm kernel's */

/* initfileLow.c: scanning */
int	IniGetChar(const char *t, word *pos);	/* -1 at the end */
Boolean	IniSkipWhiteSpace(const char *t, word *pos);	/* FALSE at the end */
Boolean	IniFindChar(const char *t, word *pos, char ch, Boolean *blob);
Boolean	IniCmpString(const char *t, word *pos, const char *str);
Boolean	IniFindCategory(const char *t, const char *cat, word *pos);
Boolean	IniFindKey(const char *t, word *pos, const char *key, word *keyStart);
word	IniEntryEnd(const char *t, word body);
word	IniValueSize(const char *t, word body);
word	IniGetString(const char *t, word body, char *out, word outSize,
		     byte conv);
word	IniGetData(const char *t, word body, byte *out, word outSize,
		   Boolean count);
Boolean	IniGetDecimal(const char *t, word body, word *value);
Boolean	IniGetBoolean(const char *t, word body, Boolean *value);
Boolean	IniSection(const char *s, word len, word index, word *start,
		   word *slen);

/* initfileConstruct.c: the text of an entry, "key = value\r\n"
 * (ghal_mem; NULL if it would not fit in an init file) */
char	*IniBuildString(const char *key, const char *str, word *size);
char	*IniBuildData(const char *key, const byte *buf, word bufSize,
		      word *size);

#endif
