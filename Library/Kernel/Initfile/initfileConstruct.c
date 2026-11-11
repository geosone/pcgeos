/*
 * initfileConstruct.c -- the text of an entry for an init file (after
 * BuildEntryFromString, BuildEntryFromData and InitEntry of
 * initfileConstruct.asm; the reconstruction is in initfileLow.c).
 *
 * A string becomes a blob when it starts or ends with a blank or holds
 * a line end or a ";"; the empty string is "{}".  "[" and "{" are always
 * escaped, in a blob also "}" and "\", a lone CR as "\r" and a LF that
 * follows no CR as "\n".  Data are two hex digits a byte, 20 bytes a
 * line, in a blob when the entry would be longer than 40 columns.
 */
#include "initfileInt.h"
#include "ghal.h"

#include <string.h>

#define DATA_ENTRY_LINE_SIZE	40	/* columns */

typedef struct {
    char	*B_text;
    dword	B_len;
    dword	B_cap;
} Build;

static void Put(Build *b, char c)
{
    if (b->B_len < b->B_cap)
	b->B_text[b->B_len] = c;
    b->B_len++;
}

static void Puts(Build *b, const char *s)
{
    while (*s)
	Put(b, *s++);
}

/* InitEntry: room for `body' characters and "key = " */
static Boolean Begin(Build *b, const char *key, dword body)
{
    b->B_cap = strlen(key) + body + 12;
    b->B_len = 0;
    b->B_text = ghal_mem_alloc(b->B_cap, 0);
    if (!b->B_text)
	return FALSE;
    Puts(b, key);
    Puts(b, " = ");
    return TRUE;
}

static char *End(Build *b, word *size)
{
    Puts(b, "\r\n");
    if (b->B_len > b->B_cap || b->B_len > INI_MAX_SIZE) {
	ghal_mem_free(b->B_text);
	return NULL;
    }
    *size = (word) b->B_len;
    return b->B_text;
}

char *IniBuildString(const char *key, const char *str, word *size)
{
    Build b;
    dword len = strlen(str);
    Boolean blob = TRUE;
    const char *s;

    if (len && str[0] != ' ' && str[0] != '\t' &&
	str[len - 1] != ' ' && str[len - 1] != '\t' &&
	!strchr(str, '\r') && !strchr(str, '\n') && !strchr(str, INI_COMMENT))
	blob = FALSE;
    if (!Begin(&b, key, 2 * len + 6))
	return NULL;
    if (!len) {
	Puts(&b, "{}");
	return End(&b, size);
    }
    if (blob)
	Puts(&b, "{\r\n");
    for (s = str; *s; s++) {
	char c = *s;
	if (c == '[' || c == '{' ||
	    (blob && (c == '}' || c == '\\'))) {
	    Put(&b, '\\');
	} else if (blob && c == '\r') {
	    if (s[1] == '\n') {
		Puts(&b, "\r\n");
		s++;
		continue;
	    }
	    Put(&b, '\\');
	    c = 'r';
	} else if (blob && c == '\n') {
	    Put(&b, '\\');
	    c = 'n';
	}
	Put(&b, c);
    }
    if (blob)
	Puts(&b, "\r\n}");
    return End(&b, size);
}

char *IniBuildData(const char *key, const byte *buf, word bufSize,
		   word *size)
{
    static const char nibbles[] = "0123456789ABCDEF";
    Build b;
    dword numbers = 2 * (dword) bufSize;
    Boolean blob;
    word i, col = 0;

    if (!Begin(&b, key, numbers + 2 * (numbers / DATA_ENTRY_LINE_SIZE) + 8))
	return NULL;
    blob = b.B_len + numbers > DATA_ENTRY_LINE_SIZE;
    if (blob)
	Puts(&b, "{\r\n");
    for (i = 0; i < bufSize; i++) {
	Put(&b, nibbles[buf[i] >> 4]);
	Put(&b, nibbles[buf[i] & 15]);
	col += 2;
	if (col == DATA_ENTRY_LINE_SIZE) {
	    Puts(&b, "\r\n");
	    col = 0;
	}
    }
    if (blob) {
	if (col)
	    Puts(&b, "\r\n");
	Put(&b, '}');
    }
    return End(&b, size);
}
