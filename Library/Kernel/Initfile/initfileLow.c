/*
 * initfileLow.c -- scanning an init file (after initfileLow.asm and the
 * reconstruction half of initfileConstruct.asm).
 *
 * Differences from the asm kernel, all for files it would misread:
 *  - a line without "=" in a category is skipped by itself (the asm
 *    kernel searched on for the next "=" and skipped that entry too);
 *  - "key =" with nothing after it is an empty value, unless a blob
 *    starts on the next line (the asm kernel took the next line);
 *  - a blob may start with "{" and a lone LF (files edited on Linux);
 *  - "key = true" at the very end of the file is a valid boolean.
 */
#include "initfileInt.h"

#include <string.h>

#define AT(t, p)	((byte) (t)[p])

int IniGetChar(const char *t, word *pos)
{
    byte c = AT(t, *pos);

    if (c == INI_EOF)
	return -1;
    (*pos)++;
    return c;
}

static Boolean IsWhite(byte c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

Boolean IniSkipWhiteSpace(const char *t, word *pos)
{
    while (IsWhite(AT(t, *pos)))
	(*pos)++;
    return AT(t, *pos) != INI_EOF;
}

static void SkipBlanks(const char *t, word *pos)
{
    while (AT(t, *pos) == ' ' || AT(t, *pos) == '\t')
	(*pos)++;
}

/*
 * FindChar: the next `ch' that is not escaped and not in a comment; pos
 * after it.  *blob tells whether pos is inside a blob, where "\" escapes
 * any character; outside one only "\{" and "\[" are escapes.  At the end
 * of the text pos stays at the C_CTRL_Z and the result is FALSE.
 */
Boolean IniFindChar(const char *t, word *pos, char ch, Boolean *blob)
{
    word p = *pos;
    byte c;

    for (;;) {
	c = AT(t, p++);
	if (c == INI_EOF)
	    goto eof;
	while (c == '\\') {
	    c = AT(t, p++);
	    if (c == INI_EOF)
		goto eof;
	    if (*blob || c == '{' || c == '[') {
		c = 0;
		break;
	    }
	}
	if (!c)
	    continue;
	if (c == INI_COMMENT) {
	    do {
		c = AT(t, p++);
		if (c == INI_EOF)
		    goto eof;
	    } while (c != '\n');
	} else if (c == '{')
	    *blob = TRUE;
	else if (c == '}')
	    *blob = FALSE;
	if (c == (byte) ch) {
	    *pos = p;
	    return TRUE;
	}
    }
eof:
    *pos = (word) (p - 1);
    return FALSE;
}

static Boolean SkipToEndOfLine(const char *t, word *pos)
{
    Boolean blob = FALSE;

    return IniFindChar(t, pos, '\n', &blob);
}

/* LegalCmpChar: -1 for characters that do not count, else upper case */
static int CmpChar(byte c)
{
    if (c == ' ' || c == '\t')
	return -1;
    if (c >= 'a' && c <= 'z')
	return c - 'a' + 'A';
    return c;
}

/* CmpString: does the text at pos start with `str' (case, spaces and
 * tabs do not count)?  pos after the match, unchanged if none. */
Boolean IniCmpString(const char *t, word *pos, const char *str)
{
    word p = *pos;
    int a, b;

    for (;;) {
	do {
	    a = (byte) *str++;
	    if (!a) {
		*pos = p;
		return TRUE;
	    }
	    a = CmpChar((byte) a);
	} while (a < 0);
	do {
	    b = IniGetChar(t, &p);
	    if (b < 0)
		return FALSE;
	    b = CmpChar((byte) b);
	} while (b < 0);
	if (a != b)
	    return FALSE;
    }
}

/* FindCategory: pos after the "]" of [cat] */
Boolean IniFindCategory(const char *t, const char *cat, word *pos)
{
    word p = 0;
    Boolean blob = FALSE;

    for (;;) {
	if (!IniFindChar(t, &p, '[', &blob))
	    return FALSE;
	if (!IniCmpString(t, &p, cat))
	    continue;
	IniSkipWhiteSpace(t, &p);
	if (AT(t, p) == ']') {
	    *pos = (word) (p + 1);
	    return TRUE;
	}
    }
}

/* where the value after "=" (pos after it) starts */
static word BodyStart(const char *t, word p)
{
    word q;

    SkipBlanks(t, &p);
    if (AT(t, p) == '\r' || AT(t, p) == '\n') {
	q = p;
	IniSkipWhiteSpace(t, &q);
	if (AT(t, q) == '{')
	    return q;
    }
    return p;
}

/*
 * FindKey: the key in the category whose body starts at pos.  Found:
 * pos at the value, keyStart at the key.  Not found: pos at the "[" of
 * the next category or at the end, where a new key would go.
 */
Boolean IniFindKey(const char *t, word *pos, const char *key, word *keyStart)
{
    word p = *pos, ks;
    Boolean blob;
    byte c;

    for (;;) {
	if (!IniSkipWhiteSpace(t, &p))
	    break;
	c = AT(t, p);
	if (c == '[')
	    break;
	if (c == INI_COMMENT) {
	    if (!SkipToEndOfLine(t, &p))
		break;
	    continue;
	}
	ks = p;
	if (*key && IniCmpString(t, &p, key)) {
	    IniSkipWhiteSpace(t, &p);
	    if (AT(t, p) == '=') {
		*keyStart = ks;
		*pos = BodyStart(t, (word) (p + 1));
		return TRUE;
	    }
	}
	/* not this one: skip its line, and its blob if it has one */
	for (p = ks; ; p++) {
	    c = AT(t, p);
	    if (c == INI_EOF || c == '\n' || c == INI_COMMENT || c == '=')
		break;
	}
	if (c == '=') {
	    p = BodyStart(t, (word) (p + 1));
	    if (AT(t, p) == '{') {
		p++;
		blob = TRUE;
		if (!IniFindChar(t, &p, '}', &blob))
		    break;
		continue;
	    }
	}
	if (!SkipToEndOfLine(t, &p))
	    break;
    }
    *pos = p;
    return FALSE;
}

/* the end of the entry whose value starts at body: after its line */
word IniEntryEnd(const char *t, word body)
{
    word p = body;
    Boolean blob = FALSE;

    if (AT(t, p) == '{') {
	p++;
	blob = TRUE;
	IniFindChar(t, &p, '}', &blob);
	blob = FALSE;
    }
    IniFindChar(t, &p, '\n', &blob);
    return p;
}

/* the text of the entry from body on: more than its value can need */
word IniValueSize(const char *t, word body)
{
    return (word) (IniEntryEnd(t, body) - body);
}

/* AsciiToHex: a decimal number (modulo 65536); FALSE without digits */
Boolean IniGetDecimal(const char *t, word body, word *value)
{
    word p = body, v = 0;
    Boolean any = FALSE;

    while (AT(t, p) >= '0' && AT(t, p) <= '9') {
	v = (word) (v * 10 + (AT(t, p) - '0'));
	p++;
	any = TRUE;
    }
    if (any)
	*value = v;
    return any;
}

/*
 * GetStringSectionByIndex: the index'th section of the string s (len
 * characters).  A section is a run of characters from the space up,
 * the sections are separated by control characters (line ends).
 */
Boolean IniSection(const char *s, word len, word index, word *start,
		   word *slen)
{
    word p = 0, b;

    for (;;) {
	while (p < len && (byte) s[p] < ' ')
	    p++;
	if (p == len)
	    return FALSE;
	b = p;
	while (p < len && (byte) s[p] >= ' ')
	    p++;
	if (index-- == 0) {
	    *start = b;
	    *slen = (word) (p - b);
	    return TRUE;
	}
    }
}

void VInitFile(void)
{
    if (--iniNesting == 0) {
	iniOwner = 0;
	KSemV(&initFileSem);
    }
}
