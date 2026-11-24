/***********************************************************************
 * kmapgen.c -- turn a keyboard map of the asm keyboard driver
 * (Driver/Keyboard/.../kmap*.def, SBCS) into C for the C driver.
 *
 *	kmapgen kmapGermanExt.def out.c [symbol]
 *
 * The output defines "const KeyboardMap symbol" (default
 * kbdKeyboardMap) with the tables of the map
 * (CInclude/Internal/kbdMap.h).  Values stay symbolic (VC_*, C_*,
 * KEY_*, KD_*, ...): the C compiler resolves them from input.h, char.h,
 * localize.h, kbdMap.h and Driver/Keyboard/keyboard.h, so the generator
 * only changes the syntax:
 *
 *	byte a, b, c	    values of the tables, in order
 *	x or y, mask EVB_Z  x | y, EV_Z
 *	<C_BSW x> <>	    x, 0
 *	'c' '''' '\\'	    the character's code
 *	ExtendedDef <...>   one ExtendedDef; AccentDef <<...>> one AccentDef
 *
 * The map is a sequence of labelled parts (kmapUS.def):
 * KbdHeader KbdConvHeader <...>, KbdExtendedScanCodes,
 * KbdExtendedScanTable, KbdKeyDefTable (up to KBD_MAX_SCAN),
 * KbdExtendedDefTable, KbdAccentables, KbdAccentTable.
 ***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define KBD_NUM_EXTSCANMAPS 16

static const char *const parts[6] = {
    "KbdExtendedScanCodes", "KbdExtendedScanTable", "KbdKeyDefTable",
    "KbdExtendedDefTable", "KbdAccentables", "KbdAccentTable"
};

static void
Fail(const char *what, const char *arg)
{
    fprintf(stderr, "kmapgen: %s%s%s\n", what, arg ? " " : "",
	    arg ? arg : "");
    exit(1);
}

static void *
Alloc(size_t size)
{
    void *p = malloc(size ? size : 1);

    if (p == NULL) {
	Fail("out of memory", NULL);
    }
    return p;
}

static char *
Dup(const char *s, size_t n)
{
    char *d = (char *) Alloc(n + 1);

    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

/* a string that grows */
typedef struct {
    char    *text;
    size_t  len;
    size_t  size;
} Str;

static void
StrAdd(Str *s, const char *t, size_t n)
{
    if (s->len + n + 1 > s->size) {
	char *d;

	s->size = 2 * (s->len + n) + 64;
	d = (char *) Alloc(s->size);
	if (s->len) {
	    memcpy(d, s->text, s->len);
	}
	free(s->text);
	s->text = d;
    }
    memcpy(s->text + s->len, t, n);
    s->len += n;
    s->text[s->len] = '\0';
}

/* a list of strings */
typedef struct {
    char    **item;
    int	    count;
    int	    size;
} List;

static void
ListAddN(List *l, const char *s, size_t n)
{
    if (l->count == l->size) {
	char **d;

	l->size = l->size ? 2 * l->size : 64;
	d = (char **) Alloc(l->size * sizeof(char *));
	if (l->count) {
	    memcpy(d, l->item, l->count * sizeof(char *));
	}
	free(l->item);
	l->item = d;
    }
    l->item[l->count++] = Dup(s, n);
}

static void
ListAdd(List *l, const char *s)
{
    ListAddN(l, s, strlen(s));
}

static void
ListFree(List *l)
{
    int i;

    for (i = 0; i < l->count; i++) {
	free(l->item[i]);
    }
    free(l->item);
    l->item = NULL;
    l->count = l->size = 0;
}

/* at least "width" items (more "0"), and no more */
static void
ListFit(List *l, int width)
{
    while (l->count < width) {
	ListAdd(l, "0");
    }
    while (l->count > width) {
	free(l->item[--l->count]);
    }
}

static int
IsWordChar(int c)
{
    return isalnum(c) || c == '_';
}

/* without the blanks around it: a new string */
static char *
Trim(const char *s, size_t n)
{
    while (n > 0 && isspace((unsigned char) *s)) {
	s++, n--;
    }
    while (n > 0 && isspace((unsigned char) s[n - 1])) {
	n--;
    }
    return Dup(s, n);
}

/***********************************************************************
 *				StripText
 ***********************************************************************
 * SYNOPSIS:	    Comments, COMMENT blocks and carriage returns out
 *		    (quotes kept), lines that end in a backslash joined
 *		    with the next.
 ***********************************************************************/
static char *
StripText(const char *in)
{
    Str	    a = {NULL, 0, 0}, b = {NULL, 0, 0}, c = {NULL, 0, 0};
    const char *p, *from;
    char    q;

    /* the carriage returns */
    StrAdd(&a, "", 0);
    for (p = in; *p != '\0'; p++) {
	if (*p != '\r') {
	    StrAdd(&a, p, 1);
	}
    }
    /* COMMENT <c> ... <c>, the word first in its line */
    StrAdd(&b, "", 0);
    from = a.text;
    for (;;) {
	const char *start = NULL, *delim = NULL, *ls;

	for (ls = from; ; ) {
	    const char *t = ls;

	    while (isspace((unsigned char) *t)) {
		t++;
	    }
	    if (toupper((unsigned char) t[0]) == 'C' &&
		toupper((unsigned char) t[1]) == 'O' &&
		toupper((unsigned char) t[2]) == 'M' &&
		toupper((unsigned char) t[3]) == 'M' &&
		toupper((unsigned char) t[4]) == 'E' &&
		toupper((unsigned char) t[5]) == 'N' &&
		toupper((unsigned char) t[6]) == 'T' &&
		isspace((unsigned char) t[7])) {
		t += 7;
		while (isspace((unsigned char) *t)) {
		    t++;
		}
		if (*t != '\0') {
		    start = ls;
		    delim = t;
		    break;
		}
	    }
	    ls = strchr(ls, '\n');
	    if (ls == NULL) {
		break;
	    }
	    ls++;
	}
	if (start == NULL) {
	    StrAdd(&b, from, strlen(from));
	    break;
	}
	StrAdd(&b, from, start - from);
	from = strchr(delim + 1, *delim);
	if (from == NULL) {
	    break;
	}
	from++;
    }
    /* the ; comments, the continuation lines */
    StrAdd(&c, "", 0);
    for (p = b.text, q = 0; *p != '\0'; p++) {
	if (*p == '\n') {
	    size_t n = c.len;

	    /* a backslash and blanks before it: one line with the next */
	    while (n > 0 && (c.text[n - 1] == ' ' || c.text[n - 1] == '\t')) {
		n--;
	    }
	    if (n > 0 && c.text[n - 1] == '\\') {
		c.len = n - 1;
		c.text[c.len] = '\0';
		StrAdd(&c, " ", 1);
	    } else {
		StrAdd(&c, "\n", 1);
	    }
	    q = 0;
	} else if (q) {
	    StrAdd(&c, p, 1);
	    if (*p == q) {
		q = 0;
	    }
	} else if (*p == '\'' || *p == '"') {
	    q = *p;
	    StrAdd(&c, p, 1);
	} else if (*p == ';') {
	    while (p[1] != '\0' && p[1] != '\n') {
		p++;
	    }
	} else {
	    StrAdd(&c, p, 1);
	}
    }
    free(a.text);
    free(b.text);
    return c.text;
}

/*
 * Split at the commas outside of quotes and <>
 */
static void
SplitTop(const char *s, List *out)
{
    Str	    cur = {NULL, 0, 0};
    int	    depth = 0;
    char    q = 0, *t;
    size_t  i, n = strlen(s);

    StrAdd(&cur, "", 0);
    for (i = 0; i < n; i++) {
	char ch = s[i];

	if (q) {
	    StrAdd(&cur, &ch, 1);
	    if (ch == q) {
		if (i + 1 < n && s[i + 1] == q) {	/* '' in quotes */
		    StrAdd(&cur, &ch, 1);
		    i++;
		} else {
		    q = 0;
		}
	    }
	} else if (ch == '\'' || ch == '"') {
	    q = ch;
	    StrAdd(&cur, &ch, 1);
	} else if (ch == '<') {
	    depth++;
	    StrAdd(&cur, &ch, 1);
	} else if (ch == '>') {
	    depth--;
	    StrAdd(&cur, &ch, 1);
	} else if (ch == ',' && depth == 0) {
	    t = Trim(cur.text, cur.len);
	    ListAdd(out, t);
	    free(t);
	    cur.len = 0;
	    cur.text[0] = '\0';
	} else {
	    StrAdd(&cur, &ch, 1);
	}
    }
    t = Trim(cur.text, cur.len);
    if (*t != '\0') {
	ListAdd(out, t);
    }
    free(t);
    free(cur.text);
}

static int
AllOf(const char *s, size_t n, const char *set)
{
    size_t i;

    for (i = 0; i < n; i++) {
	if (strchr(set, s[i]) == NULL || s[i] == '\0') {
	    return 0;
	}
    }
    return n > 0;
}

#define HEXDIGITS   "0123456789abcdefABCDEF"

/***********************************************************************
 *				Value
 ***********************************************************************
 * SYNOPSIS:	    One asm value as a C expression (a new string).
 ***********************************************************************/
static char *
Value(const char *in)
{
    char    *v = Trim(in, strlen(in)), *r;
    size_t  n = strlen(v), i;
    Str	    s = {NULL, 0, 0};
    int	    blank = 0;

    if (n == 0 || strcmp(v, "<>") == 0) {
	free(v);
	return Dup("0", 1);
    }
    if (n >= 2 && v[0] == '<' && v[n - 1] == '>') {
	/* <C_BSW x>: x; <x>: x */
	const char *p = v + 1;
	char	   *inner;

	while (isspace((unsigned char) *p)) {
	    p++;
	}
	if (p < v + n - 1 &&
	    (strncmp(p, "C_BSW", 5) == 0 || strncmp(p, "C_CTRL", 6) == 0 ||
	     strncmp(p, "C_UI", 4) == 0)) {
	    const char *q = p + (p[2] == 'B' ? 5 : p[2] == 'C' ? 6 : 4);

	    if (isspace((unsigned char) *q) && q < v + n - 1) {
		while (isspace((unsigned char) *q) && q < v + n - 1) {
		    q++;
		}
		inner = Dup(q, v + n - 1 - q);
		r = Value(inner);
		free(inner);
		free(v);
		return r;
	    }
	}
	inner = Dup(v + 1, n - 2);
	r = Value(inner);
	free(inner);
	free(v);
	return r;
    }
    if (n >= 2 && (v[0] == '\'' || v[0] == '"') && v[n - 1] == v[0]) {
	/* a character: '' for the quote itself, \\ for a backslash */
	Str body = {NULL, 0, 0};

	StrAdd(&body, "", 0);
	for (i = 1; i < n - 1; i++) {
	    StrAdd(&body, v + i, 1);
	    if (v[i] == v[0] && i + 1 < n - 1 && v[i + 1] == v[0]) {
		i++;
	    }
	}
	if (strcmp(body.text, "\\\\") == 0) {
	    body.len = 1;
	}
	if (body.len != 1) {
	    Fail("character", v);
	}
	r = (char *) Alloc(8);
	sprintf(r, "0x%02x", (unsigned char) body.text[0]);
	free(body.text);
	free(v);
	return r;
    }
    /* word by word */
    StrAdd(&s, "", 0);
    for (i = 0; i < n; ) {
	size_t	w = i, len;
	char	*t;

	if (!IsWordChar((unsigned char) v[i])) {
	    StrAdd(&s, v + i, 1);
	    i++;
	    continue;
	}
	while (i < n && IsWordChar((unsigned char) v[i])) {
	    i++;
	}
	len = i - w;
	t = v + w;
	if (len == 4 && strncmp(t, "mask", 4) == 0) {
	    /* mask EVB_X: EV_X */
	    size_t k = i;

	    while (k < n && isspace((unsigned char) v[k])) {
		k++;
	    }
	    if (k > i && strncmp(v + k, "EVB_", 4) == 0) {
		StrAdd(&s, "EV_", 3);
		i = k + 4;
		while (i < n && IsWordChar((unsigned char) v[i])) {
		    StrAdd(&s, v + i, 1);
		    i++;
		}
		continue;
	    }
	    Fail("value", v);
	}
	if (len > 1 && (t[len - 1] == 'h' || t[len - 1] == 'H') &&
	    isdigit((unsigned char) t[0]) && AllOf(t, len - 1, HEXDIGITS)) {
	    StrAdd(&s, "0x", 2);
	    StrAdd(&s, t, len - 1);
	} else if (len > 1 && (t[len - 1] == 'b' || t[len - 1] == 'B') &&
		   AllOf(t, len - 1, "01") &&
		   !(len > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))) {
	    char    num[32];
	    unsigned long b = 0;
	    size_t  k;

	    for (k = 0; k < len - 1; k++) {
		b = 2 * b + (unsigned long) (t[k] - '0');
	    }
	    sprintf(num, "%lu", b);
	    StrAdd(&s, num, strlen(num));
	} else if (len == 2 && strncmp(t, "or", 2) == 0) {
	    StrAdd(&s, "|", 1);
	} else if (len == 3 && strncmp(t, "and", 3) == 0) {
	    StrAdd(&s, "&", 1);
	} else if (len == 3 && strncmp(t, "not", 3) == 0) {
	    StrAdd(&s, "~", 1);
	} else if (len == 3 && strncmp(t, "shl", 3) == 0) {
	    StrAdd(&s, "<<", 2);
	} else {
	    StrAdd(&s, t, len);
	}
    }
    if (strchr(s.text, '\'') != NULL || strchr(s.text, '"') != NULL) {
	Fail("value", v);
    }
    free(v);
    v = Trim(s.text, s.len);
    free(s.text);
    for (i = 0; v[i] != '\0'; i++) {
	if (v[i] == ' ') {
	    blank = 1;
	}
    }
    if (!blank) {
	return v;
    }
    r = (char *) Alloc(strlen(v) + 3);
    sprintf(r, "(%s)", v);
    free(v);
    return r;
}

/* the values of a comma separated list, each as C */
static void
Values(const char *s, List *out)
{
    List    items = {NULL, 0, 0};
    int	    i;

    SplitTop(s, &items);
    for (i = 0; i < items.count; i++) {
	char *v = Value(items.item[i]);

	ListAdd(out, v);
	free(v);
    }
    ListFree(&items);
}

/* "byte" and blanks at p: where the values begin, or NULL */
static const char *
AfterByte(const char *p)
{
    if (strncmp(p, "byte", 4) != 0 || !isspace((unsigned char) p[4])) {
	return NULL;
    }
    p += 4;
    while (isspace((unsigned char) *p)) {
	p++;
    }
    return p;
}

/*
 * The values of the "[label] byte a, b" lines
 */
static void
BytesOf(const char *text, List *vals)
{
    const char *p = text;

    while (*p != '\0') {
	const char  *eol = strchr(p, '\n'), *q, *rest = NULL;
	char	    *line;

	line = Dup(p, eol != NULL ? (size_t) (eol - p) : strlen(p));
	p = eol != NULL ? eol + 1 : p + strlen(p);
	for (q = line; isspace((unsigned char) *q); q++) {
	    ;
	}
	if (isalpha((unsigned char) *q) || *q == '_') {
	    const char *l = q;

	    while (IsWordChar((unsigned char) *l)) {
		l++;
	    }
	    if (isspace((unsigned char) *l)) {
		while (isspace((unsigned char) *l)) {
		    l++;
		}
		rest = AfterByte(l);
	    }
	}
	if (rest == NULL) {
	    rest = AfterByte(q);
	}
	if (rest != NULL) {
	    Values(rest, vals);
	}
	free(line);
    }
}

/*
 * The <...> initialisers after "name" (ExtendedDef, AccentDef)
 */
static void
Structs(const char *text, const char *name, List *out)
{
    const char	*body = strstr(text, name), *start = NULL, *p;
    int		depth = 0;
    char	q = 0;

    if (body == NULL) {
	Fail("no", name);
    }
    for (p = body + strlen(name); *p != '\0'; p++) {
	if (q) {
	    if (*p == q) {
		q = 0;
	    }
	} else if (*p == '\'' || *p == '"') {
	    q = *p;
	} else if (*p == '<') {
	    if (depth == 0) {
		start = p + 1;
	    }
	    depth++;
	} else if (*p == '>') {
	    depth--;
	    if (depth == 0) {
		ListAddN(out, start, p - start);
	    }
	}
    }
}

/* the word in the text (not a part of a longer one), or NULL */
static const char *
FindWord(const char *text, const char *word)
{
    const char	*p = text;
    size_t	n = strlen(word);

    while ((p = strstr(p, word)) != NULL) {
	if ((p == text || !IsWordChar((unsigned char) p[-1])) &&
	    !IsWordChar((unsigned char) p[n])) {
	    return p;
	}
	p++;
    }
    return NULL;
}

/*
 * A table given as "byte" lines or as "name <...>, <...>"
 */
static void
TableOf(const char *text, const char *name, int width, List *vals)
{
    const char	*p = text;
    size_t	n = strlen(name);
    int		isStruct = 0, i;

    /* the name as a word's end, blanks, "<" */
    while ((p = strstr(p, name)) != NULL) {
	const char *q = p + n;

	if (p == text || !IsWordChar((unsigned char) p[-1])) {
	    while (isspace((unsigned char) *q)) {
		q++;
	    }
	    if (*q == '<') {
		isStruct = 1;
		break;
	    }
	}
	p++;
    }
    if (!isStruct) {
	BytesOf(text, vals);
	return;
    }
    {
	List s = {NULL, 0, 0};

	Structs(text, name, &s);
	for (i = 0; i < s.count; i++) {
	    List    v = {NULL, 0, 0};
	    int	    j;

	    Values(s.item[i], &v);
	    ListFit(&v, width);
	    for (j = 0; j < v.count; j++) {
		ListAdd(vals, v.item[j]);
	    }
	    ListFree(&v);
	}
	ListFree(&s);
    }
}

/* the text after the label, up to the next one (a new string) */
static char *
Section(const char *text, const char *label, const char *next)
{
    const char *p = FindWord(text, label), *e = NULL;

    if (p == NULL) {
	Fail("no", label);
    }
    p += strlen(label);
    if (next != NULL) {
	e = FindWord(p, next);
    }
    return Dup(p, e != NULL ? (size_t) (e - p) : strlen(p));
}

static char *
ReadFile(const char *path)
{
    FILE    *f = fopen(path, "rb");
    char    *text;
    size_t  len = 0, size = 65536, n;

    if (f == NULL) {
	perror(path);
	exit(1);
    }
    text = (char *) Alloc(size);
    while ((n = fread(text + len, 1, size - len - 1, f)) > 0) {
	len += n;
	if (len + 1 >= size) {
	    char *t = (char *) Alloc(size * 2);

	    memcpy(t, text, len);
	    free(text);
	    text = t;
	    size *= 2;
	}
    }
    fclose(f);
    text[len] = '\0';
    return text;
}

static void
PrintJoined(FILE *f, const List *l, int first, int count)
{
    int i;

    for (i = 0; i < count; i++) {
	fprintf(f, "%s%s", i ? ", " : "", l->item[first + i]);
    }
}

int
main(int argc, char **argv)
{
    const char	*src, *dst, *sym, *p, *shown;
    char	*raw, *text, *sec, *flags, *v0, *v2;
    List	hv = {NULL, 0, 0};
    List	extCodes = {NULL, 0, 0}, extScan = {NULL, 0, 0};
    List	keydefs = {NULL, 0, 0}, extdefs = {NULL, 0, 0};
    List	accentables = {NULL, 0, 0}, accents = {NULL, 0, 0};
    FILE	*f;
    int		k, n;

    if (argc < 3) {
	fprintf(stderr, "usage: kmapgen kmap.def out.c [symbol]\n");
	return 2;
    }
    src = argv[1];
    dst = argv[2];
    sym = argc > 3 ? argv[3] : "kbdKeyboardMap";
    raw = ReadFile(src);
    text = StripText(raw);
    free(raw);

    /* KbdHeader KbdConvHeader <a, b, c, flags> */
    for (p = text; (p = strstr(p, "KbdHeader")) != NULL; p++) {
	const char *q = p + 9;

	if (!isspace((unsigned char) *q)) {
	    continue;
	}
	while (isspace((unsigned char) *q)) {
	    q++;
	}
	if (strncmp(q, "KbdConvHeader", 13) != 0) {
	    continue;
	}
	q += 13;
	while (isspace((unsigned char) *q)) {
	    q++;
	}
	if (*q == '<' && strchr(q, '>') != NULL) {
	    char *h = Dup(q + 1, strchr(q, '>') - q - 1);

	    SplitTop(h, &hv);
	    free(h);
	    break;
	}
    }
    while (hv.count < 4) {
	ListAdd(&hv, "0");
    }
    if (hv.item[3][0] == '\0' || strcmp(hv.item[3], "0") == 0) {
	flags = Dup("0", 1);
    } else {
	flags = Value(hv.item[3]);
    }

    sec = Section(text, parts[0], parts[1]);
    BytesOf(sec, &extCodes);
    free(sec);
    sec = Section(text, parts[1], parts[2]);
    TableOf(sec, "ExtendedScanDef", 3, &extScan);
    free(sec);
    sec = Section(text, parts[2], "KBD_MAX_SCAN");
    TableOf(sec, "KeyDef", 4, &keydefs);
    free(sec);
    sec = Section(text, parts[3], parts[4]);
    Structs(sec, "ExtendedDef", &extdefs);
    free(sec);
    sec = Section(text, parts[4], parts[5]);
    BytesOf(sec, &accentables);
    free(sec);
    sec = Section(text, parts[5], NULL);
    Structs(sec, "AccentDef", &accents);
    free(sec);

    if (keydefs.count % 4) {
	fprintf(stderr, "%s: %d KeyDef bytes\n", src, keydefs.count);
	return 1;
    }
    ListFit(&extCodes, 4);
    ListFit(&extScan, 48);
    ListFit(&accentables, 16);
    v0 = Value(hv.item[0]);
    v2 = Value(hv.item[2]);

    if ((f = fopen(dst, "w")) == NULL) {
	perror(dst);
	return 1;
    }
    /* (the file as it is named below Driver/Keyboard) */
    shown = src;
    for (p = src; (p = strstr(p, "Driver/Keyboard/")) != NULL; ) {
	p += 16;
	shown = p;
    }
    fprintf(f, "/* Generated by kmapgen from %s -- do not edit */\n", shown);
    fprintf(f, "#include \"keyboard.h\"\n\n");
    fprintf(f, "static const byte extScanCodes[4] = { ");
    PrintJoined(f, &extCodes, 0, 4);
    fprintf(f, " };\n");
    fprintf(f, "static const ExtendedScanDef extScanTab[%d] = {\n",
	    KBD_NUM_EXTSCANMAPS);
    for (k = 0; k < 48; k += 3) {
	fprintf(f, "    { (word) ((%s) | ((%s) << 8)), %s },\n",
		extScan.item[k], extScan.item[k + 1], extScan.item[k + 2]);
    }
    fprintf(f, "};\n");
    fprintf(f, "static const KeyDef keyDefTab[%d] = {\n", keydefs.count / 4);
    for (k = 0; k < keydefs.count; k += 4) {
	fprintf(f, "    { ");
	PrintJoined(f, &keydefs, k, 4);
	fprintf(f, " },\t/* scan 0x%02x */\n", k / 4 + 1);
    }
    fprintf(f, "};\n");
    fprintf(f, "static const ExtendedDef extDefTab[%d] = {\n",
	    extdefs.count > 0 ? extdefs.count : 1);
    for (n = 0; n < extdefs.count; n++) {
	List vals = {NULL, 0, 0};

	Values(extdefs.item[n], &vals);
	ListFit(&vals, 8);
	fprintf(f, "    { ");
	PrintJoined(f, &vals, 0, 8);
	fprintf(f, " },\t/* %d */\n", n);
	ListFree(&vals);
    }
    if (extdefs.count == 0) {
	fprintf(f, "    { 0 }\n");
    }
    fprintf(f, "};\n");
    fprintf(f, "static const byte accentables[16] = { ");
    PrintJoined(f, &accentables, 0, 16);
    fprintf(f, " };\n");
    fprintf(f, "static const AccentDef accentTab[16] = {\n");
    for (n = 0; n < accents.count && n < 16; n++) {
	List	vals = {NULL, 0, 0};
	char	*e = Trim(accents.item[n], strlen(accents.item[n]));
	size_t	len = strlen(e);

	/* <<...>>: the inner one */
	if (len >= 2 && e[0] == '<' && e[len - 1] == '>') {
	    e[len - 1] = '\0';
	    Values(e + 1, &vals);
	} else {
	    Values(accents.item[n], &vals);
	}
	free(e);
	ListFit(&vals, 8);
	fprintf(f, "    { { ");
	PrintJoined(f, &vals, 0, 8);
	fprintf(f, " } },\n");
	ListFree(&vals);
    }
    fprintf(f, "};\n\n");
    fprintf(f, "const KeyboardMap %s = {\n", sym);
    fprintf(f, "    %s, %s, %s,\n", v0, v2, flags);
    fprintf(f, "    extScanCodes, extScanTab, keyDefTab, %d, extDefTab,\n",
	    keydefs.count / 4);
    fprintf(f, "    accentables, accentTab\n};\n");
    if (fclose(f) != 0) {
	perror(dst);
	return 1;
    }
    return 0;
}
