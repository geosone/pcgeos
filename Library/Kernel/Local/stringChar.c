/*
 * stringChar.c -- character classes and lexical values (after
 * stringChar.asm, SBCS): the CharClass of a character in
 * localCharClassTable (stringCharTables.asm, made C at build time).
 *
 * Port32: the routines return a Boolean where the asm cleared the Z flag.
 * LocalIsDateChar, LocalIsTimeChar and LocalIsNumChar are in
 * localFormatChars.c.
 */
#include "localInt.h"

/* localize.h makes these isupper() etc. for C; the kernel has them as
 * routines of their own (geos.gp) */
#undef LocalIsUpper
#undef LocalIsLower
#undef LocalIsAlpha
#undef LocalIsSpace
#undef LocalIsDigit

Boolean LocalIsAlpha(wchar ch)	{ return LocalCharIs(ch, CC_ALPHA); }
Boolean LocalIsSpace(wchar ch)	{ return LocalCharIs(ch, CC_SPACE); }
Boolean LocalIsPunctuation(wchar ch) { return LocalCharIs(ch, CC_PUNCTUATION); }
Boolean LocalIsSymbol(wchar ch)	{ return LocalCharIs(ch, CC_SYMBOL); }
Boolean LocalIsUpper(wchar ch)	{ return LocalCharIs(ch, CC_UPPER); }
Boolean LocalIsLower(wchar ch)	{ return LocalCharIs(ch, CC_LOWER); }
Boolean LocalIsControl(wchar ch) { return LocalCharIs(ch, CC_CONTROL); }
Boolean LocalIsDigit(wchar ch)	{ return LocalCharIs(ch, CC_DIGIT); }
Boolean LocalIsHexDigit(wchar ch) { return LocalCharIs(ch, CC_HEX); }
Boolean LocalIsAlphaNumeric(wchar ch)
{
    return LocalCharIs(ch, CC_ALPHA | CC_DIGIT);
}
Boolean LocalIsPrintable(wchar ch) { return LocalCharIs(ch, CC_PRINTABLE); }
Boolean LocalIsGraphic(wchar ch) { return LocalCharIs(ch, CC_GRAPHIC); }

/* LocalLexicalValue: the LexicalOrder of the character */
word LocalLexicalValue(wchar ch)
{
    return localLexicalOrderTable[ch & 0xff];
}

/* LocalLexicalValueNoCase: the Lex1stOrder of the character */
word LocalLexicalValueNoCase(wchar ch)
{
    return localLexical1stOrderTable[ch & 0xff];
}
