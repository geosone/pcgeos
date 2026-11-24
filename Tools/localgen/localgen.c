/***********************************************************************
 * localgen.c -- the tables of the asm localization code
 * (Library/Kernel/Local, SBCS) as C for the C kernel.
 *
 *	localgen -o out.c [--header out.h] --chars char.def
 *		 --lexical lexical.def file.asm...
 *
 * Each data table of the files becomes a C array named "local" + the
 * asm label (UpcaseTable -> localUpcaseTable), each "NAME = value"
 * constant a const word LOCAL_NAME.  The tables are:
 *
 *	Label Type \		values separated by commas, over lines
 *	    v, v, ...
 *	Label label byte	then lines of "db v[, v]..."
 *
 * Type is a known one: CharClass (a record of 12 one-bit fields and 4
 * unused bits, a word), LexicalOrder and Lex1stOrder (byte enums of
 * Include/Internal/lexical.def), Chars and Char (bytes; Chars from
 * Include/char.def).  A value is a number (0x10, 10h, 16), a character
 * ('a'), a symbol of those enums or the constants before it, or a
 * record initializer Type<...>.  Conditionals take the SBCS side
 * (DBCS_PCGEOS is 0) of the standard product (PZ_PCGEOS is 0).
 *
 * The strings of a resource (localStrings.asm),
 *
 *	LocalDefString Name <"text", 0>	    a chunk, "text" and symbols
 *	LocalDefString <"text", 0>	    one without a name
 *	Name chunk.char "text", 0
 *
 * become localDefaultStrings[], one per chunk in their order, and the
 * header (--header) gets LS_Name for the index of each named one and
 * LS_COUNT, and the declarations of the constants.  The values are
 * resolved here: the C file has numbers, with the asm's comment of
 * each.
 ***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define CHARCLASS_FIELDS    12

static void
Fail(const char *what, const char *arg)
{
    fprintf(stderr, "localgen: %s%s%s\n", what, arg ? " " : "",
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

static void *
Grow(void *items, int count, int *size, size_t itemSize)
{
    void *n;

    if (count < *size) {
	return items;
    }
    *size = *size ? 2 * *size : 64;
    n = Alloc(*size * itemSize);
    if (count) {
	memcpy(n, items, count * itemSize);
    }
    free(items);
    return n;
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

static int
IsWordChar(int c)
{
    return isalnum(c) || c == '_';
}

/* a string that grows; it may hold null bytes */
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
ListAdd(List *l, const char *s)
{
    l->item = (char **) Grow(l->item, l->count, &l->size, sizeof(char *));
    l->item[l->count++] = Dup(s, strlen(s));
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

/*
 * The symbols: the enums of char.def and lexical.def, the constants
 */
typedef struct {
    char    *name;
    long    value;
} Sym;

static Sym  *syms;
static int  numSyms, sizeSyms;

static Sym *
FindSym(const char *name)
{
    int i;

    for (i = numSyms - 1; i >= 0; i--) {
	if (strcmp(syms[i].name, name) == 0) {
	    return &syms[i];
	}
    }
    return NULL;
}

static void
SetSym(const char *name, long value)
{
    Sym *s = FindSym(name);

    if (s == NULL) {
	syms = (Sym *) Grow(syms, numSyms, &sizeSyms, sizeof(Sym));
	s = &syms[numSyms++];
	s->name = Dup(name, strlen(name));
    }
    s->value = value;
}

/* the line up to its ";" comment (not in quotes): a new string */
static char *
StripComment(const char *line)
{
    const char	*p;
    char	q = 0;

    for (p = line; *p != '\0'; p++) {
	if (q) {
	    if (*p == q) {
		q = 0;
	    }
	} else if (*p == '\'' || *p == '"') {
	    q = *p;
	} else if (*p == ';') {
	    break;
	}
    }
    return Dup(line, p - line);
}

/* the same, without blanks around it */
static char *
Code(const char *line)
{
    char *s = StripComment(line), *t = Trim(s, strlen(s));

    free(s);
    return t;
}

/* the comment of a line, without its ";" and the blanks around it */
static char *
Comment(const char *line)
{
    char	*s = StripComment(line);
    const char	*p = line + strlen(s);

    free(s);
    while (*p == ';') {
	p++;
    }
    return Trim(p, strlen(p));
}

/***********************************************************************
 *				Evaluate
 ***********************************************************************
 * SYNOPSIS:	    The value of an asm expression: numbers (16, 0x10,
 *		    10h), characters ('a'), symbols, + - * / %, shl,
 *		    shr, or, and, not, parentheses.
 * RETURN:	    whether it has one (*result)
 ***********************************************************************/
static const char *evalAt;
static int	  evalFailed;

static long EvalOr(void);

static void
EvalBlanks(void)
{
    while (*evalAt == ' ' || *evalAt == '\t') {
	evalAt++;
    }
}

static long
EvalUnary(void)
{
    long v;

    EvalBlanks();
    if (*evalAt == '-') {
	evalAt++;
	return -EvalUnary();
    }
    if (*evalAt == '+') {
	evalAt++;
	return EvalUnary();
    }
    if (*evalAt == '~') {
	evalAt++;
	return ~EvalUnary();
    }
    if (*evalAt == '(') {
	evalAt++;
	v = EvalOr();
	EvalBlanks();
	if (*evalAt != ')') {
	    evalFailed = 1;
	    return 0;
	}
	evalAt++;
	return v;
    }
    if (isdigit((unsigned char) *evalAt)) {
	const char *s = evalAt;
	int nonzero = 0;

	v = 0;
	while (isdigit((unsigned char) *evalAt)) {
	    nonzero = nonzero || *evalAt != '0';
	    v = 10 * v + (*evalAt - '0');
	    evalAt++;
	}
	/* (no number has zeros before it) */
	if (evalAt - s > 1 && *s == '0' && nonzero) {
	    evalFailed = 1;
	}
	return v;
    }
    evalFailed = 1;
    return 0;
}

static long
EvalMul(void)
{
    long v = EvalUnary(), w;

    for (;;) {
	EvalBlanks();
	if (evalFailed) {
	    return 0;
	}
	if (*evalAt == '*') {
	    evalAt++;
	    v *= EvalUnary();
	} else if (*evalAt == '/' || *evalAt == '%') {
	    char op = *evalAt++;

	    w = EvalUnary();
	    if (w == 0) {
		evalFailed = 1;
		return 0;
	    }
	    v = op == '/' ? v / w : v % w;
	} else {
	    return v;
	}
    }
}

static long
EvalAdd(void)
{
    long v = EvalMul();

    for (;;) {
	EvalBlanks();
	if (evalFailed) {
	    return 0;
	}
	if (*evalAt == '+') {
	    evalAt++;
	    v += EvalMul();
	} else if (*evalAt == '-') {
	    evalAt++;
	    v -= EvalMul();
	} else {
	    return v;
	}
    }
}

static long
EvalShift(void)
{
    long v = EvalAdd(), w;

    for (;;) {
	EvalBlanks();
	if (evalFailed) {
	    return 0;
	}
	if ((evalAt[0] == '<' && evalAt[1] == '<') ||
	    (evalAt[0] == '>' && evalAt[1] == '>')) {
	    char op = *evalAt;

	    evalAt += 2;
	    w = EvalAdd();
	    if (w < 0 || w >= (long) (8 * sizeof(long))) {
		evalFailed = 1;
		return 0;
	    }
	    v = op == '<' ? (long) ((unsigned long) v << w) : v >> w;
	} else {
	    return v;
	}
    }
}

static long
EvalAnd(void)
{
    long v = EvalShift();

    for (;;) {
	EvalBlanks();
	if (evalFailed) {
	    return 0;
	}
	if (*evalAt == '&') {
	    evalAt++;
	    v &= EvalShift();
	} else {
	    return v;
	}
    }
}

static long
EvalXor(void)
{
    long v = EvalAnd();

    for (;;) {
	EvalBlanks();
	if (evalFailed) {
	    return 0;
	}
	if (*evalAt == '^') {
	    evalAt++;
	    v ^= EvalAnd();
	} else {
	    return v;
	}
    }
}

static long
EvalOr(void)
{
    long v = EvalXor();

    for (;;) {
	EvalBlanks();
	if (evalFailed) {
	    return 0;
	}
	if (*evalAt == '|') {
	    evalAt++;
	    v |= EvalXor();
	} else {
	    return v;
	}
    }
}

static int
AllHex(const char *s, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
	if (!isxdigit((unsigned char) s[i])) {
	    return 0;
	}
    }
    return n > 0;
}

static int
Evaluate(const char *expr, long *result)
{
    char    *e = Trim(expr, strlen(expr)), num[32];
    size_t  n = strlen(e), i;
    Str	    a = {NULL, 0, 0}, b = {NULL, 0, 0};
    int	    ok = 1;

    /* a character alone: '\\' is the backslash, '''' the quote */
    if (n == 4 && e[0] == '\'' && e[1] == '\\' && e[3] == '\'') {
	*result = (unsigned char) e[2];
	free(e);
	return 1;
    }
    if (n == 3 && e[0] == '\'' && e[2] == '\'') {
	*result = (unsigned char) e[1];
	free(e);
	return 1;
    }
    if (strcmp(e, "''''") == 0) {
	*result = '\'';
	free(e);
	return 1;
    }
    /* the characters in it as numbers */
    StrAdd(&a, "", 0);
    for (i = 0; i < n; ) {
	if (e[i] == '\'' && i + 2 < n && e[i + 2] == '\'') {
	    sprintf(num, "%d", (unsigned char) e[i + 1]);
	    StrAdd(&a, num, strlen(num));
	    i += 3;
	} else {
	    StrAdd(&a, e + i, 1);
	    i++;
	}
    }
    /* the words as numbers and operators */
    StrAdd(&b, "", 0);
    for (i = 0; i < a.len && ok; ) {
	size_t	w = i, len;
	char	*t;
	Sym	*s;

	if (!IsWordChar((unsigned char) a.text[i])) {
	    StrAdd(&b, a.text + i, 1);
	    i++;
	    continue;
	}
	while (i < a.len && IsWordChar((unsigned char) a.text[i])) {
	    i++;
	}
	len = i - w;
	t = Dup(a.text + w, len);
	if (len > 2 && t[0] == '0' && t[1] == 'x' && AllHex(t + 2, len - 2)) {
	    sprintf(num, "%ld", strtol(t + 2, NULL, 16));
	    StrAdd(&b, num, strlen(num));
	} else if (len > 1 && (t[len - 1] == 'h' || t[len - 1] == 'H') &&
		   isdigit((unsigned char) t[0]) && AllHex(t, len - 1)) {
	    t[len - 1] = '\0';
	    sprintf(num, "%ld", strtol(t, NULL, 16));
	    StrAdd(&b, num, strlen(num));
	} else if (strspn(t, "0123456789") == len) {
	    StrAdd(&b, t, len);
	} else if ((s = FindSym(t)) != NULL) {
	    sprintf(num, "%ld", s->value);
	    StrAdd(&b, num, strlen(num));
	} else if (strcmp(t, "shl") == 0) {
	    StrAdd(&b, "<<", 2);
	} else if (strcmp(t, "shr") == 0) {
	    StrAdd(&b, ">>", 2);
	} else if (strcmp(t, "or") == 0) {
	    StrAdd(&b, "|", 1);
	} else if (strcmp(t, "and") == 0) {
	    StrAdd(&b, "&", 1);
	} else if (strcmp(t, "not") == 0) {
	    StrAdd(&b, "~", 1);
	} else {
	    ok = 0;			/* an unknown symbol */
	}
	free(t);
    }
    if (ok) {
	evalAt = b.text;
	evalFailed = 0;
	*result = EvalOr();
	EvalBlanks();
	ok = !evalFailed && *evalAt == '\0';
    }
    free(a.text);
    free(b.text);
    free(e);
    return ok;
}

static long
MustEvaluate(const char *expr)
{
    long v;

    if (!Evaluate(expr, &v)) {
	Fail("no value:", expr);
    }
    return v;
}

/***********************************************************************
 *				LinesSbcs
 ***********************************************************************
 * SYNOPSIS:	    The lines of a file with the conditionals taken
 *		    (DBCS_PCGEOS and PZ_PCGEOS are 0), comments kept,
 *		    COMMENT blocks gone.
 ***********************************************************************/
static int
CondValue(const char *expr)
{
    char    *e = Trim(expr, strlen(expr)), *name;
    int	    neg = 0, v;

    name = e;
    if (strncmp(e, "not ", 4) == 0) {
	neg = 1;
	name = Trim(e + 4, strlen(e + 4));
	free(e);
	e = name;
    }
    if (strcmp(name, "DBCS_PCGEOS") != 0 && strcmp(name, "PZ_PCGEOS") != 0) {
	Fail("unknown condition:", name);
    }
    v = 0;
    free(e);
    return neg ? !v : v;
}

/* the keyword at s and no more of a word: what follows, or NULL */
static const char *
AfterKeyword(const char *s, const char *kw)
{
    size_t n = strlen(kw);

    if (strncmp(s, kw, n) != 0 || IsWordChar((unsigned char) s[n])) {
	return NULL;
    }
    return s + n;
}

static void
LinesSbcs(const char *path, List *out)
{
    FILE    *f = fopen(path, "rb");
    Str	    buf = {NULL, 0, 0};
    /* per level: was the part around it active, was a branch taken */
    int	    outer[64], taken[64], depth = 0;
    int	    active = 1, inComment = 0, c;

    if (f == NULL) {
	perror(path);
	exit(1);
    }
    do {
	char	    *line, *s;
	const char  *arg;

	buf.len = 0;
	StrAdd(&buf, "", 0);
	while ((c = fgetc(f)) != EOF && c != '\n') {
	    char ch = (char) c;

	    StrAdd(&buf, &ch, 1);
	}
	if (c == EOF && buf.len == 0) {
	    break;
	}
	while (buf.len > 0 && buf.text[buf.len - 1] == '\r') {
	    buf.text[--buf.len] = '\0';
	}
	line = buf.text;
	if (inComment) {
	    if (strchr(line, '@') != NULL || strchr(line, '}') != NULL) {
		inComment = 0;
	    }
	    continue;
	}
	s = Trim(line, strlen(line));
	if (strncmp(s, "COMMENT", 7) == 0) {
	    char *rest = Trim(s + 7, strlen(s + 7));

	    if ((rest[0] == '@' || rest[0] == '}') &&
		strchr(rest + 1, rest[0]) == NULL) {
		inComment = 1;
	    }
	    free(rest);
	    free(s);
	    continue;
	}
	if ((arg = AfterKeyword(s, "if")) != NULL) {
	    char    *a = Code(arg);
	    int	    v;

	    if (depth == 64) {
		Fail("if nested too deep in", path);
	    }
	    v = active && CondValue(a);
	    outer[depth] = active;
	    taken[depth] = v;
	    depth++;
	    active = v;
	    free(a);
	} else if ((arg = AfterKeyword(s, "elseif")) != NULL) {
	    char    *a = Code(arg);
	    int	    v;

	    if (depth == 0) {
		Fail("elseif without if in", path);
	    }
	    v = outer[depth - 1] && !taken[depth - 1] && CondValue(a);
	    taken[depth - 1] = taken[depth - 1] || v;
	    active = v;
	    free(a);
	} else if (AfterKeyword(s, "else") != NULL) {
	    if (depth == 0) {
		Fail("else without if in", path);
	    }
	    active = outer[depth - 1] && !taken[depth - 1];
	    taken[depth - 1] = 1;
	} else if (AfterKeyword(s, "endif") != NULL) {
	    if (depth == 0) {
		Fail("endif without if in", path);
	    }
	    active = outer[--depth];
	} else if (active) {
	    ListAdd(out, line);
	}
	free(s);
    } while (c != EOF);
    free(buf.text);
    fclose(f);
}

/*
 * Matchers: each moves *p on and says whether its part is there
 */
static char *
Word(const char **p)
{
    const char *s = *p;

    while (IsWordChar((unsigned char) **p)) {
	(*p)++;
    }
    return *p > s ? Dup(s, *p - s) : NULL;
}

static void
Blanks(const char **p)
{
    while (isspace((unsigned char) **p)) {
	(*p)++;
    }
}

static int
Blanks1(const char **p)
{
    if (!isspace((unsigned char) **p)) {
	return 0;
    }
    Blanks(p);
    return 1;
}

static int
Keyword(const char **p, const char *kw)
{
    size_t n = strlen(kw);

    if (strncmp(*p, kw, n) != 0) {
	return 0;
    }
    *p += n;
    return 1;
}

/***********************************************************************
 *				ReadEnums
 ***********************************************************************
 * SYNOPSIS:	    The symbols of a .def file: "NAME enum Type[, value]"
 *		    after "Type etype size[, first[, step]]", and
 *		    "NAME = value", "NAME equ value" where the value is
 *		    one this program can work out.
 ***********************************************************************/
typedef struct {
    char    *name;
    long    next, step;
} EnumType;

static void
ReadEnums(const char *path)
{
    List	lines = {NULL, 0, 0};
    EnumType	*types = NULL;
    int		numTypes = 0, sizeTypes = 0, i, k;

    LinesSbcs(path, &lines);
    for (i = 0; i < lines.count; i++) {
	char	    *s = Code(lines.item[i]), *name, *w;
	const char  *p = s, *q;
	long	    v;

	if ((name = Word(&p)) == NULL) {
	    free(s);
	    continue;
	}
	/* <name> etype <size>[, <first>[, <step>]] */
	q = p;
	if (Blanks1(&q) && Keyword(&q, "etype") && Blanks1(&q) &&
	    (w = Word(&q)) != NULL) {
	    long first = 0, step = 1;
	    int  ok = 1;

	    free(w);
	    Blanks(&q);
	    if (*q == ',') {
		const char *e;
		char	   *a;

		q++;
		Blanks(&q);
		for (e = q; *e != '\0' && *e != ','; e++) {
		    ;
		}
		if (e == q) {
		    ok = 0;
		} else {
		    a = Dup(q, e - q);
		    first = MustEvaluate(a);
		    free(a);
		    q = e;
		    if (*q == ',') {
			q++;
			Blanks(&q);
			if (*q == '\0') {
			    ok = 0;
			} else {
			    step = MustEvaluate(q);
			    q += strlen(q);
			}
		    }
		}
	    }
	    if (ok && *q == '\0') {
		for (k = 0; k < numTypes; k++) {
		    if (strcmp(types[k].name, name) == 0) {
			break;
		    }
		}
		if (k == numTypes) {
		    types = (EnumType *) Grow(types, numTypes, &sizeTypes,
					      sizeof(EnumType));
		    types[numTypes++].name = Dup(name, strlen(name));
		}
		types[k].next = first;
		types[k].step = step;
		free(name);
		free(s);
		continue;
	    }
	}
	/* <name> = <value>, <name> equ <value> */
	q = p;
	Blanks(&q);
	if ((*q == '=' && q[1] != '\0') ||
	    (q > p && AfterKeyword(q, "equ") != NULL)) {
	    q = *q == '=' ? q + 1 : q + 3;
	    Blanks(&q);
	    if (*q != '\0') {
		if (Evaluate(q, &v)) {
		    SetSym(name, v);
		}
		free(name);
		free(s);
		continue;
	    }
	}
	/* <name> enum <type>[, <value>] */
	q = p;
	if (Blanks1(&q) && Keyword(&q, "enum") && Blanks1(&q) &&
	    (w = Word(&q)) != NULL) {
	    for (k = 0; k < numTypes; k++) {
		if (strcmp(types[k].name, w) == 0) {
		    break;
		}
	    }
	    free(w);
	    Blanks(&q);
	    if (k < numTypes && (*q == '\0' || *q == ',')) {
		int ok = 1;

		if (*q == ',') {
		    q++;
		    Blanks(&q);
		    if (*q == '\0') {
			ok = 0;
		    } else {
			types[k].next = MustEvaluate(q);
		    }
		}
		if (ok) {
		    SetSym(name, types[k].next);
		    types[k].next += types[k].step;
		}
	    }
	}
	free(name);
	free(s);
    }
    ListFree(&lines);
}

/*
 * The values of a list: at the commas outside of <> and quotes, without
 * blanks around them, none that is empty
 */
static void
SplitValues(const char *text, List *out)
{
    Str		cur = {NULL, 0, 0};
    int		depth = 0;
    char	q = 0, *t;
    const char	*p;

    StrAdd(&cur, "", 0);
    for (p = text; ; p++) {
	if (*p != '\0' && q) {
	    StrAdd(&cur, p, 1);
	    if (*p == q) {
		q = 0;
	    }
	    continue;
	}
	if ((*p == '\'' || *p == '"') && depth == 0) {
	    q = *p;
	} else if (*p == '<') {
	    depth++;
	} else if (*p == '>') {
	    depth--;
	}
	if (*p == '\0' || (*p == ',' && depth == 0)) {
	    t = Trim(cur.text, cur.len);
	    if (*t != '\0') {
		ListAdd(out, t);
	    }
	    free(t);
	    cur.len = 0;
	    cur.text[0] = '\0';
	    if (*p == '\0') {
		break;
	    }
	} else {
	    StrAdd(&cur, p, 1);
	}
    }
    free(cur.text);
}

/* a value of a table: a number, or the word of a CharClass<...> */
static long
Value(const char *v)
{
    const char	*p = v;
    char	*name = Word(&p);
    size_t	n = strlen(v);

    if (name != NULL) {
	Blanks(&p);
	if (*p == '<' && v[n - 1] == '>' && v + n - 1 > p) {
	    char    *inner = Dup(p + 1, v + n - 1 - (p + 1));
	    char    *a = inner, *e;
	    long    w = 0;
	    int	    i = 0;

	    if (strcmp(name, "CharClass") != 0) {
		Fail("unknown record:", v);
	    }
	    for (;;) {
		long b;

		e = strchr(a, ',');
		if (e != NULL) {
		    *e = '\0';
		}
		b = MustEvaluate(a);
		if (i < CHARCLASS_FIELDS && b) {
		    w |= 1L << (15 - i);
		}
		i++;
		if (e == NULL) {
		    break;
		}
		a = e + 1;
	    }
	    free(inner);
	    free(name);
	    return w;
	}
	free(name);
    }
    return MustEvaluate(v);
}

/*
 * What this program writes
 */
typedef struct {
    long    value;
    char    *comment;
} TableValue;

typedef struct {
    char	*label;
    int		isWord;
    TableValue	*vals;
    int		count, size;
} Table;

typedef struct {
    char    *name;	/* NULL: none */
    Str	    bytes;
} DefString;

static Table	    *tables;
static int	    numTables, sizeTables;
static List	    constNames;		/* of the file read */
static DefString    *strings;
static int	    numStrings, sizeStrings;

static Table *
NewTable(const char *label, int isWord)
{
    Table *t;

    tables = (Table *) Grow(tables, numTables, &sizeTables, sizeof(Table));
    t = &tables[numTables++];
    t->label = Dup(label, strlen(label));
    t->isWord = isWord;
    t->vals = NULL;
    t->count = t->size = 0;
    return t;
}

/* the values of a list into the table, each with the line's comment */
static void
AddValues(int table, const char *text, const char *comment)
{
    List    v = {NULL, 0, 0};
    int	    i;

    SplitValues(text, &v);
    for (i = 0; i < v.count; i++) {
	Table *t = &tables[table];

	t->vals = (TableValue *) Grow(t->vals, t->count, &t->size,
				      sizeof(TableValue));
	t->vals[t->count].value = Value(v.item[i]);
	t->vals[t->count].comment = Dup(comment, strlen(comment));
	t->count++;
    }
    ListFree(&v);
}

/* the bytes of "text", 0, SYMBOL, ... without a final null */
static void
AddString(const char *name, const char *text)
{
    List	v = {NULL, 0, 0};
    DefString	*s;
    int		i;

    strings = (DefString *) Grow(strings, numStrings, &sizeStrings,
				 sizeof(DefString));
    s = &strings[numStrings++];
    s->name = name != NULL ? Dup(name, strlen(name)) : NULL;
    s->bytes.text = NULL;
    s->bytes.len = s->bytes.size = 0;
    StrAdd(&s->bytes, "", 0);
    SplitValues(text, &v);
    for (i = 0; i < v.count; i++) {
	const char *t = v.item[i];

	if (t[0] == '"') {
	    size_t k, n = strlen(t);

	    for (k = 1; k + 1 < n; k++) {
		StrAdd(&s->bytes, t + k, 1);
		if (t[k] == '"' && k + 2 < n && t[k + 1] == '"') {
		    k++;
		}
	    }
	} else {
	    char c = (char) (MustEvaluate(t) & 0xff);

	    StrAdd(&s->bytes, &c, 1);
	}
    }
    if (s->bytes.len > 0 && s->bytes.text[s->bytes.len - 1] == '\0') {
	s->bytes.len--;
    }
    ListFree(&v);
}

static int
IsByteType(const char *t)
{
    return strcmp(t, "LexicalOrder") == 0 || strcmp(t, "Lex1stOrder") == 0 ||
	   strcmp(t, "Chars") == 0 || strcmp(t, "Char") == 0 ||
	   strcmp(t, "byte") == 0;
}

static int
IsWordType(const char *t)
{
    return strcmp(t, "CharClass") == 0;
}

/***********************************************************************
 *				ParseTables
 ***********************************************************************
 * SYNOPSIS:	    The tables, constants and strings of a file.
 ***********************************************************************/
static void
ParseTables(const char *path)
{
    List    lines = {NULL, 0, 0};
    int	    i = 0;

    LinesSbcs(path, &lines);
    while (i < lines.count) {
	const char  *line = lines.item[i], *p, *q;
	char	    *s = Code(line), *name, *w;
	int	    atStart = !isspace((unsigned char) line[0]);
	int	    haveType;

	/* LocalDefString [<name>] <"text", 0> */
	p = s;
	if (Keyword(&p, "LocalDefString") && Blanks1(&p)) {
	    q = p;
	    name = Word(&q);
	    Blanks(&q);
	    if (*q != '<') {
		/* (no name: what looked like one is the text's start) */
		free(name);
		name = NULL;
		q = p;
	    }
	    if (*q == '<') {
		char *text = Dup(q + 1, strlen(q + 1));
		size_t len = strlen(text);

		if (len > 0 && text[len - 1] == '>') {
		    text[len - 1] = '\0';
		}
		AddString(name, text);
		free(text);
		free(name);
		free(s);
		i++;
		continue;
	    }
	    free(name);
	}
	/* <name> chunk.char "text", 0 */
	p = s;
	if ((name = Word(&p)) != NULL) {
	    q = p;
	    if (Blanks1(&q) && Keyword(&q, "chunk.char") && Blanks1(&q)) {
		AddString(name, q);
		free(name);
		free(s);
		i++;
		continue;
	    }
	    /* <name> = <value> */
	    q = p;
	    Blanks(&q);
	    if (*q == '=' && atStart) {
		q++;
		Blanks(&q);
		if (*q != '\0') {
		    long v;

		    if (Evaluate(q, &v)) {
			int k;

			SetSym(name, v);
			for (k = 0; k < constNames.count; k++) {
			    if (strcmp(constNames.item[k], name) == 0) {
				break;
			    }
			}
			if (k == constNames.count) {
			    ListAdd(&constNames, name);
			}
		    }
		    free(name);
		    free(s);
		    i++;
		    continue;
		}
	    }
	    /* <name> label byte, then lines of "db <values>" */
	    q = p;
	    if (Blanks1(&q) && Keyword(&q, "label") && Blanks1(&q) &&
		Keyword(&q, "byte") && *q == '\0') {
		int t = (int) (NewTable(name, 0) - tables);

		i++;
		while (i < lines.count) {
		    char	*body = Code(lines.item[i]);
		    char	*com = Comment(lines.item[i]);
		    const char	*b = body;

		    if (*body == '\0') {
			i++;
		    } else if (Keyword(&b, "db") && Blanks1(&b) &&
			       *b != '\0') {
			AddValues(t, b, com);
			i++;
		    } else {
			free(body);
			free(com);
			break;
		    }
		    free(body);
		    free(com);
		}
		free(name);
		free(s);
		continue;
	    }
	    /* <name> <type> [\] [<values>]; "<name> \" has the type of
	     * the records that follow */
	    w = NULL;
	    haveType = 0;
	    q = p;
	    Blanks(&q);
	    if (atStart && q[0] == '\\' && q[1] == '\0') {
		if (i + 1 < lines.count) {
		    char	*next = Code(lines.item[i + 1]);
		    const char	*x = next;
		    char	*type = Word(&x);

		    if (type != NULL) {
			Blanks(&x);
			if (*x == '<' && IsWordType(type)) {
			    w = type;
			    type = NULL;
			    haveType = 1;
			}
		    }
		    free(type);
		    free(next);
		}
	    } else if (q > p) {
		w = Word(&q);
		haveType = w != NULL;
	    }
	    if (w != NULL && atStart && haveType &&
		(IsByteType(w) || IsWordType(w))) {
		int	    isWord = IsWordType(w), t, backslash = 0;
		const char  *rest;

		Blanks(&q);
		if (*q == '\\') {
		    backslash = 1;
		    q++;
		}
		Blanks(&q);
		rest = backslash ? "" : q;
		t = (int) (NewTable(name, isWord) - tables);
		i++;
		if (*rest != '\0') {
		    AddValues(t, rest, "");
		} else {
		    while (i < lines.count) {
			char	*body = Code(lines.item[i]);
			char	*com = Comment(lines.item[i]);
			size_t	len = strlen(body);
			int	more;

			if (*body == '\0') {
			    free(body);
			    free(com);
			    i++;
			    continue;
			}
			if (!isspace((unsigned char) lines.item[i][0])) {
			    free(body);
			    free(com);
			    break;
			}
			more = body[len - 1] == ',';
			AddValues(t, body, com);
			free(body);
			free(com);
			i++;
			if (!more) {
			    break;
			}
		    }
		}
		free(w);
		free(name);
		free(s);
		continue;
	    }
	    free(w);
	    free(name);
	}
	free(s);
	i++;
    }
    ListFree(&lines);
}

static const char *
BaseName(const char *path)
{
    const char *p, *base = path;

    for (p = path; *p != '\0'; p++) {
	if (*p == '/') {
	    base = p + 1;
	}
    }
    return base;
}

static void
CString(FILE *f, const Str *b)
{
    size_t i;

    fputc('"', f);
    for (i = 0; i < b->len; i++) {
	unsigned char c = (unsigned char) b->text[i];

	if (c == 0x22 || c == 0x5c) {
	    fprintf(f, "\\%c", c);
	} else if (c >= 0x20 && c < 0x7f) {
	    fputc(c, f);
	} else {
	    fprintf(f, "\\%03o", c);
	}
    }
    fputc('"', f);
}

int
main(int argc, char **argv)
{
    const char	*out = NULL, *header = NULL, *chars = NULL, *lexical = NULL;
    List	allConsts = {NULL, 0, 0};
    int		ac = 1, first, i, k, n;
    FILE	*f;

    while (ac + 1 < argc && argv[ac][0] == '-') {
	if (strcmp(argv[ac], "-o") == 0) {
	    out = argv[ac + 1];
	} else if (strcmp(argv[ac], "--header") == 0) {
	    header = argv[ac + 1];
	} else if (strcmp(argv[ac], "--chars") == 0) {
	    chars = argv[ac + 1];
	} else if (strcmp(argv[ac], "--lexical") == 0) {
	    lexical = argv[ac + 1];
	} else {
	    break;
	}
	ac += 2;
    }
    if (out == NULL || chars == NULL || lexical == NULL || ac >= argc ||
	argv[ac][0] == '-') {
	fprintf(stderr, "usage: localgen -o out.c [--header out.h] "
		"--chars char.def --lexical lexical.def file.asm...\n");
	return 2;
    }
    first = ac;
    ReadEnums(chars);
    ReadEnums(lexical);

    if ((f = fopen(out, "w")) == NULL) {
	perror(out);
	return 1;
    }
    fprintf(f, "/* generated by localgen from ");
    for (i = first; i < argc; i++) {
	fprintf(f, "%s%s", i > first ? ", " : "", BaseName(argv[i]));
    }
    fprintf(f, " -- do not edit */\n#include <geos.h>\n\n");
    for (i = first; i < argc; i++) {
	numTables = 0;
	constNames.count = 0;
	ParseTables(argv[i]);
	fprintf(f, "/* %s */\n", BaseName(argv[i]));
	for (k = 0; k < constNames.count; k++) {
	    fprintf(f, "const word LOCAL_%s = %ld;\n", constNames.item[k],
		    FindSym(constNames.item[k])->value);
	    ListAdd(&allConsts, constNames.item[k]);
	}
	for (k = 0; k < numTables; k++) {
	    Table *t = &tables[k];

	    fprintf(f, "const %s local%c%s[%d] = {\n",
		    t->isWord ? "word" : "byte",
		    toupper((unsigned char) t->label[0]), t->label + 1,
		    t->count);
	    for (n = 0; n < t->count; n++) {
		const char *c = t->vals[n].comment;

		if (t->isWord) {
		    fprintf(f, "    0x%04lx",
			    (unsigned long) t->vals[n].value & 0xffffUL);
		} else {
		    fprintf(f, "    0x%02lx",
			    (unsigned long) t->vals[n].value & 0xffUL);
		}
		if (n + 1 < t->count) {
		    fputc(',', f);
		}
		if (*c != '\0') {
		    fprintf(f, " /* ");
		    for (; *c != '\0'; c++) {
			if (c[0] == '*' && c[1] == '/') {
			    fprintf(f, "* /");
			    c++;
			} else {
			    fputc(*c, f);
			}
		    }
		    fprintf(f, " */");
		}
		fputc('\n', f);
	    }
	    fprintf(f, "};\n");
	}
	fprintf(f, "\n");
    }
    if (numStrings > 0) {
	fprintf(f, "const char *const localDefaultStrings[%d] = {\n",
		numStrings);
	for (n = 0; n < numStrings; n++) {
	    fprintf(f, "    ");
	    CString(f, &strings[n].bytes);
	    fprintf(f, "%s /* %s */\n", n + 1 < numStrings ? "," : "",
		    strings[n].name != NULL ? strings[n].name : "");
	}
	fprintf(f, "};\n");
    }
    if (fclose(f) != 0) {
	perror(out);
	return 1;
    }
    if (header != NULL) {
	if ((f = fopen(header, "w")) == NULL) {
	    perror(header);
	    return 1;
	}
	fprintf(f, "/* generated by localgen -- do not edit */\n"
		"#ifndef __LOCALSTRINGS_H\n#define __LOCALSTRINGS_H\n\n");
	for (n = 0; n < numStrings; n++) {
	    if (strings[n].name != NULL) {
		fprintf(f, "#define LS_%s %d\n", strings[n].name, n);
	    }
	}
	fprintf(f, "#define LS_COUNT %d\n\n", numStrings);
	for (k = 0; k < allConsts.count; k++) {
	    fprintf(f, "extern const word LOCAL_%s;\n", allConsts.item[k]);
	}
	fprintf(f, "\nextern const char *const "
		"localDefaultStrings[LS_COUNT];\n\n#endif\n");
	if (fclose(f) != 0) {
	    perror(header);
	    return 1;
	}
    }
    return 0;
}
