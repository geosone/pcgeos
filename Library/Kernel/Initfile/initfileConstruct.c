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
#include "initfileConstant.h"
#include "ghal.h"

#include <string.h>

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

#define AT(t, p)	((byte) (t)[p])

/* the end of a value that is no blob: before a comment, the end of the
 * line, and the spaces and tabs before them */
static word PlainEnd(const char *t, word p)
{
    word end = p;
    byte c;

    for (;;) {
	c = AT(t, p);
	if (c == INI_EOF || c == INI_COMMENT || c == '\r' || c == '\n')
	    return end;
	p++;
	if (c != ' ' && c != '\t')
	    end = p;
    }
}

static char Convert(byte c, byte conv)
{
    if (conv == IFCC_UPCASE && c >= 'a' && c <= 'z')
	return (char) (c - 'a' + 'A');
    if (conv == IFCC_DOWNCASE && c >= 'A' && c <= 'Z')
	return (char) (c - 'A' + 'a');
    return (char) c;
}

/*
 * ReconstructString: the value at body without braces and escapes, in
 * `out' (at most outSize-1 characters and a null), converted by `conv'
 * (InitFileCharConvert).  Returns the characters stored.  The value
 * is never longer than IniValueSize.
 */
word ReconstructString(const char *t, word body, char *out, word outSize,
		  byte conv)
{
    word p = body, n = 0, end;
    byte c;
    Boolean blob = AT(t, p) == '{';

#define PUT(ch) \
    do { if (n + 1 < outSize) out[n++] = Convert((byte) (ch), conv); \
    } while (0)

    if (blob) {
	p++;
	if (AT(t, p) == '\r')
	    p++;
	if (AT(t, p) == '\n')
	    p++;
	for (;;) {
	    c = AT(t, p++);
	    if (c == INI_EOF || c == '}')
		break;
	    if (c == '\\') {
		c = AT(t, p);
		if (c == '{' || c == '[' || c == '}' || c == '\\') {
		    p++;
		    PUT(c);
		} else if (c == 'n') {
		    p++;
		    PUT('\n');
		} else if (c == 'r') {
		    p++;
		    PUT('\r');
		} else
		    PUT('\\');
		continue;
	    }
	    PUT(c);
	}
	/* the line end before the "}" is the blob's, not the value's */
	if (n && out[n - 1] == '\n')
	    n--;
	if (n && out[n - 1] == '\r')
	    n--;
    } else {
	end = PlainEnd(t, p);
	while (p < end) {
	    c = AT(t, p++);
	    if (c == '\\' && p < end && (AT(t, p) == '{' || AT(t, p) == '['))
		c = AT(t, p++);
	    PUT(c);
	}
    }
    if (outSize)
	out[n] = 0;
    return n;
#undef PUT
}

#define AT(t, p)	((byte) (t)[p])

static int Nibble(int c)
{
    if (c >= '0' && c <= '9')
	return c - '0';
    if (c >= 'A' && c <= 'F')
	return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
	return c - 'a' + 10;
    return -1;
}

/*
 * ReconstructData: the bytes of the value at body, two hex digits each,
 * in a blob over several lines.  At most outSize; with count set, the
 * number of bytes of the whole value (out not used).
 */
word ReconstructData(const char *t, word body, byte *out, word outSize,
		Boolean count)
{
    word p = body, n = 0;
    Boolean blob = FALSE;
    int c, hi, lo;

    IniSkipWhiteSpace(t, &p);
    if (AT(t, p) == '{') {
	p++;
	blob = TRUE;
    }
    for (;;) {
	if (!count && n >= outSize)
	    break;
	c = IniGetChar(t, &p);
	if (c < 0 || c == '}')
	    break;
	if (c == '\r' || c == '\n') {
	    if (blob)
		continue;
	    break;
	}
	hi = Nibble(c);
	if (hi < 0)
	    break;
	lo = Nibble(IniGetChar(t, &p));
	if (lo < 0)
	    break;
	if (!count)
	    out[n] = (byte) (hi << 4 | lo);
	n++;
    }
    return n;
}
