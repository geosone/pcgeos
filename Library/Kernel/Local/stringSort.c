/*
 * stringSort.c -- comparing characters and strings in dictionary order
 * (after stringSort.asm, SBCS).  Strings are compared first by Lex1stOrder
 * (stringSortTables.asm: case and accents do not count), and only if equal
 * there by LexicalOrder; the NoCase routines stop after the first.
 *
 * The routines return -1, 0 or 1 for less, equal or greater: what the C
 * stubs of localC.asm (CCmpStringsFinish) made of the flags of the asm.
 * A size of 0 compares up to the null.
 *
 * Not here: SORT_DOUBLE_S_CORRECTLY (the German double s as "ss"), which
 * only the MULTILANG product sets.
 */
#include "localInt.h"
#include <sllang.h>

#undef LocalIsSpace

static sword Order(byte a, byte b)
{
    return (sword) (a == b ? 0 : a < b ? -1 : 1);
}

/* DoCharCompare */
static sword CmpChar(const byte *table, wchar ch1, wchar ch2)
{
    return Order(table[ch1 & 0xff], table[ch2 & 0xff]);
}

/* DoStringCompare: to the first difference, the end of both strings, or
 * size characters */
static sword CmpString(const byte *table, const char *str1, const char *str2,
		       word size)
{
    byte a, b;

    for (;;) {
	a = table[(byte) *str1++];
	b = table[(byte) *str2++];
	if (a != b)
	    return Order(a, b);
	if (!a || !--size)
	    return 0;
    }
}

/* DoStringCompareNoSpace: spaces and punctuation are skipped, but count
 * against the size (so "o'leary" and "o'" are equal for a size of 2) */
static sword CmpStringNoSpace(const byte *table, const char *str1,
			      const char *str2, word size)
{
    const word skip = CC_SPACE | CC_PUNCTUATION;
    byte a, b, c;
    int ignored;

    for (;;) {
	ignored = 0;	/* source characters skipped since the last compare */
	for (;;) {
	    c = (byte) *str1++;
	    if (!(localCharClassTable[c] & skip))
		break;
	    ignored++;
	    if (!--size)
		return 0;
	}
	a = table[c];
	for (;;) {
	    c = (byte) *str2++;
	    if (!(localCharClassTable[c] & skip))
		break;
	    if (--ignored >= 0)
		continue;
	    if (!--size)
		return 0;
	}
	b = table[c];
	if (a != b)
	    return Order(a, b);
	if (!a || !--size)
	    return 0;
    }
}

word LocalGetLanguage(void)
{
    return SL_ENGLISH;		/* curLanguage of stringSortTables.asm */
}

sword LocalCmpStrings(const char *str1, const char *str2, word strSize)
{
    sword r = CmpString(localLexical1stOrderTable, str1, str2, strSize);

    return r ? r : CmpString(localLexicalOrderTable, str1, str2, strSize);
}

sword LocalCmpStringsNoCase(const char *str1, const char *str2, word strSize)
{
    return CmpString(localLexical1stOrderTable, str1, str2, strSize);
}

sword LocalCmpStringsNoSpace(const char *str1, const char *str2,
			     word strSize)
{
    sword r = CmpStringNoSpace(localLexical1stOrderTable, str1, str2,
			       strSize);

    return r ? r : CmpStringNoSpace(localLexicalOrderTable, str1, str2,
				    strSize);
}

sword LocalCmpStringsNoSpaceCase(const char *str1, const char *str2,
				 word strSize)
{
    return CmpStringNoSpace(localLexical1stOrderTable, str1, str2, strSize);
}

/* LocalCmpChars: do not use it for languages that sort sequences of
 * characters (Spanish "ch" after "c") */
sword LocalCmpChars(wchar ch1, wchar ch2)
{
    sword r = CmpChar(localLexical1stOrderTable, ch1, ch2);

    return r ? r : CmpChar(localLexicalOrderTable, ch1, ch2);
}

sword LocalCmpCharsNoCase(wchar ch1, wchar ch2)
{
    return CmpChar(localLexical1stOrderTable, ch1, ch2);
}

/* ConvertCharDOSToGEOS */
static byte DosToGeos(const LocalCodePage *cp, byte c, byte defaultChar)
{
    if (c < MIN_MAP_CHAR)
	return c;
    c = cp->LCP_toGeos[c - MIN_MAP_CHAR];
    return c ? c : defaultChar;
}

/* DoStringCompareDOSToGEOS */
static sword CmpStringDosToGeos(const byte *table, const LocalCodePage *cp,
				const char *str1, const char *str2, word size,
				byte defaultChar,
				LocalCmpStringsDosToGeosFlags flags)
{
    byte a, b;

    for (;;) {
	a = (byte) *str1++;
	if (!(flags & LCSDTGF_NO_CONVERT_STRING_1))
	    a = DosToGeos(cp, a, defaultChar);
	b = (byte) *str2++;
	if (!(flags & LCSDTG_NO_CONVERT_STRING_2))
	    b = DosToGeos(cp, b, defaultChar);
	a = table[a];
	b = table[b];
	if (a != b)
	    return Order(a, b);
	if (!a || !--size)
	    return 0;
    }
}

/* LocalCmpStringsDosToGeos: the strings in the current DOS code page, but
 * the one the flags say is GEOS already */
sword LocalCmpStringsDosToGeos(const char *str1, const char *str2,
			       word strSize, wchar defaultChar,
			       LocalCmpStringsDosToGeosFlags flags)
{
    const LocalCodePage *cp = LocalCurrentCodePage();
    sword r = CmpStringDosToGeos(localLexical1stOrderTable, cp, str1, str2,
				 strSize, (byte) defaultChar, flags);

    return r ? r : CmpStringDosToGeos(localLexicalOrderTable, cp, str1, str2,
				      strSize, (byte) defaultChar, flags);
}
