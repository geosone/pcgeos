/***********************************************************************
 * PROJECT:	  PCGEOS
 * MODULE:	  UIC -- output as goc source
 * FILE:	  outgoc.c
 *
 * DESCRIPTION:
 *	uic -g: the objects, monikers and chunks of a .ui file as goc
 *	source, for the C port (Port32).  The output is a piece of a
 *	.goc file: the geode's own .goc file @includes it, as its asm
 *	included the .rdef file.  goc -cg then lays the resources out,
 *	so the two tools share one description of the object blocks.
 *
 *	Where a .ui file passes Esp source through (values of byte, word
 *	and dword components, the data of hints and chunks), it is
 *	translated to C: the expressions (EspExpr) and the data
 *	directives (EspData).
 *
 *	The classes of a .uih file know instance data only in its order,
 *	goc by its names: an instance variable is written as
 *	__uic_<index>_<count>_<class>Class, which goc's scanner turns
 *	into the name of the <index>th instance variable the class
 *	defines after checking that there are <count> of them.
 *
 *	Options with -g:
 *	-G <resource>	write this resource only (repeated: these); for
 *			the time a geode has not all its classes in C yet
 *	-x <file>	write "@extern" declarations of everything
 *			written to <file>, for the .goc files that use it
 *	-i <file>	begin the output with @include <<file>> (repeated:
 *			in this order): the headers of the classes
 *
 ***********************************************************************/
#include    <config.h>
#include    "uic.h"
#include    "parse.h"
#include    "map.h"
#include    "sttab.h"

#include    <ctype.h>
#include    <stdio.h>
#include    <stdlib.h>
#include    <compat/string.h>

extern Symbol *curResource;

/* a moniker that is written with its object (a flag of the symbol) */
#define SYM_GOC_INLINE	0x40000000

static void OutputGocVisMonikerDef(Symbol *sym);

#define MAX_GOC_OPTIONS	64
static char *onlyResources[MAX_GOC_OPTIONS];
static int  numOnlyResources = 0;
static char *includes[MAX_GOC_OPTIONS];
static int  numIncludes = 0;
static char *externFile = NULL;
/* -N: vardata not written (goc takes no vardata of another class, such
 * as a specific UI's on a generic object: the code adds it) */
static char *omitVarData[MAX_GOC_OPTIONS];
static int  numOmitVarData = 0;

static int
VarDataOmitted(const char *name)
{
    int	i;

    for (i = 0; i < numOmitVarData; i++) {
	if (strcmp(name, omitVarData[i]) == 0) {
	    return 1;
	}
    }
    return 0;
}

void
GocOption(int option, char *arg)
{
    if (option == 'G' && numOnlyResources < MAX_GOC_OPTIONS) {
	onlyResources[numOnlyResources++] = arg;
    } else if (option == 'i' && numIncludes < MAX_GOC_OPTIONS) {
	includes[numIncludes++] = arg;
    } else if (option == 'x') {
	externFile = arg;
    } else if (option == 'N' && numOmitVarData < MAX_GOC_OPTIONS) {
	omitVarData[numOmitVarData++] = arg;
    }
}

static int
ResourceWanted(Symbol *resource)
{
    int i;

    for (i = 0; i < numOnlyResources; i++) {
	if (strcmp(resource->name, onlyResources[i]) == 0) {
	    return 1;
	}
    }
    return numOnlyResources == 0;
}
extern char *Scan_KbdKeyName(int key);

/*
 * A growing string.
 */
typedef struct {
    char    *text;
    int	    len, max;
} Str;

static void
StrAdd(Str *s, const char *text, int len)
{
    if (len < 0) {
	len = strlen(text);
    }
    if (s->len + len + 1 > s->max) {
	s->max = (s->len + len + 1) * 2 + 64;
	s->text = (char *)realloc(s->text, s->max);
    }
    memcpy(s->text + s->len, text, len);
    s->len += len;
    s->text[s->len] = '\0';
}

static int
WordIs(const char *word, int len, const char *is)
{
    int i;

    for (i = 0; i < len; i++) {
	if (is[i] == '\0' || tolower((unsigned char)word[i]) != is[i]) {
	    return 0;
	}
    }
    return is[len] == '\0';
}

/*
 * A symbol of this .ui file that is a chunk (an object, a moniker, ...)
 */
static Symbol *
ChunkSym(const char *name, int len)
{
    char    buf[256];
    Symbol  *sym;

    if (len >= (int)sizeof(buf)) {
	return NullSymbol;
    }
    memcpy(buf, name, len);
    buf[len] = '\0';
    /* (symbols are found by their address in the string table) */
    sym = Symbol_Find(String_Enter(buf, len), ALL_SCOPES);
    if (sym != NullSymbol) {
	switch (sym->type) {
	    case OBJECT_SYM: case VIS_MONIKER_SYM: case CHUNK_SYM:
	    case HINT_LIST_SYM: case HELP_ENTRY_SYM: case ACTIVE_LIST_SYM:
	    case GCN_LIST_SYM: case GCN_LIST_OF_LISTS_SYM:
		return sym;
	}
    }
    return NullSymbol;
}

/***********************************************************************
 *				EspExpr
 ***********************************************************************
 * SYNOPSIS:	    An Esp expression in C.
 * RETURN:	    the C text (allocated)
 *
 * STRATEGY:	    Token by token:
 *		    mask X	    X (the C headers name the mask)
 *		    offset X	    OptrToChunk(@X) for a chunk of this
 *				    file, else X_OFFSET (a record field)
 *		    size X	    sizeof(X)
 *		    BM_MASK ...	    BMT_MASK ... (graphics.def's old names)
 *		    or and xor not shl shr mod	the C operators
 *		    eq ne lt le gt ge		the C operators
 *		    12h 0ffh 101b 10d		C numbers (101b in hex)
 *		    < a, b >	    { a, b } (a structure's values)
 *		    the rest as it is
 ***********************************************************************/
static void
EspExprTo(Str *out, const char *in, int len)
{
    static const struct { const char *esp, *c; } ops[] = {
	{"or", "|"}, {"and", "&"}, {"xor", "^"}, {"not", "~"},
	{"shl", "<<"}, {"shr", ">>"}, {"mod", "%"}, {"eq", "=="},
	{"ne", "!="}, {"lt", "<"}, {"le", "<="}, {"gt", ">"}, {"ge", ">="},
	{NULL, NULL}
    };
    const char	*p = in, *end = in + (len < 0 ? (int)strlen(in) : len);
    int	    	pending = 0;	/* 1: after "offset", 2: after "size" */

    while (p < end) {
	unsigned char c = *p;

	if (isalpha(c) || c == '_' || c == '@' || c == '?') {
	    const char	*start = p;
	    int	    	n, i;

	    while (p < end && (isalnum((unsigned char)*p) || *p == '_' ||
			       *p == '@' || *p == '?')) {
		p++;
	    }
	    n = p - start;
	    if (WordIs(start, n, "mask")) {
		continue;
	    } else if (WordIs(start, n, "offset")) {
		pending = 1;
		continue;
	    } else if (WordIs(start, n, "size")) {
		pending = 2;
		continue;
	    }
	    for (i = 0; ops[i].esp != NULL; i++) {
		if (WordIs(start, n, ops[i].esp)) {
		    StrAdd(out, ops[i].c, -1);
		    break;
		}
	    }
	    if (ops[i].esp != NULL) {
		continue;
	    }
	    if (pending == 0 && n >= 6 && strncmp(start, "BM_", 3) == 0 &&
		(WordIs(start + 3, n - 3, "palette") ||
		 WordIs(start + 3, n - 3, "huge") ||
		 WordIs(start + 3, n - 3, "mask") ||
		 WordIs(start + 3, n - 3, "complex"))) {
		/* BMTypeOld's names (graphics.def): BMType's in C */
		StrAdd(out, "BMT_", 4);
		StrAdd(out, start + 3, n - 3);
		continue;
	    }
	    if (pending == 1) {
		if (ChunkSym(start, n) != NullSymbol) {
		    StrAdd(out, "OptrToChunk(@", -1);
		    StrAdd(out, start, n);
		    StrAdd(out, ")", 1);
		} else {
		    StrAdd(out, start, n);
		    StrAdd(out, "_OFFSET", -1);
		}
	    } else if (pending == 2) {
		StrAdd(out, "sizeof(", -1);
		StrAdd(out, start, n);
		StrAdd(out, ")", 1);
	    } else {
		StrAdd(out, start, n);
	    }
	    pending = 0;
	} else if (isdigit(c)) {
	    const char	*start = p;
	    int	    	n;
	    char    	last;

	    while (p < end && isalnum((unsigned char)*p)) {
		p++;
	    }
	    n = p - start;
	    last = tolower((unsigned char)start[n - 1]);
	    if (n > 1 && start[0] == '0' &&
		tolower((unsigned char)start[1]) == 'x') {
		StrAdd(out, start, n);
	    } else if (last == 'h') {
		StrAdd(out, "0x", 2);
		StrAdd(out, start, n - 1);
	    } else if (last == 'b' && strspn(start, "01") == (size_t)(n - 1)) {
		/* in hex: goc does not read 0b */
		unsigned long v = strtoul(start, NULL, 2);
		char	      hex[24];

		sprintf(hex, "0x%02lx", v);
		StrAdd(out, hex, -1);
	    } else if (last == 'd' && n > 1) {
		StrAdd(out, start, n - 1);
	    } else {
		/* decimal: no leading zeros, C would read octal */
		while (n > 1 && *start == '0' && isdigit((unsigned char)start[1])) {
		    start++, n--;
		}
		StrAdd(out, start, n);
	    }
	} else if (c == '"' || c == '\'') {
	    const char	*start = p++;

	    while (p < end && *p != (char)c) {
		if (*p == '\\' && p + 1 < end) {
		    p++;
		}
		p++;
	    }
	    if (p < end) {
		p++;
	    }
	    StrAdd(out, start, p - start);
	} else if (c == '<') {
	    StrAdd(out, "{", 1);
	    p++;
	} else if (c == '>') {
	    StrAdd(out, "}", 1);
	    p++;
	} else {
	    StrAdd(out, (const char *)p, 1);
	    p++;
	}
    }
}

static char *
EspExpr(const char *in)
{
    Str s = {NULL, 0, 0};

    StrAdd(&s, "", 0);
    EspExprTo(&s, in, -1);
    return s.text;
}

/***********************************************************************
 *				EspData
 ***********************************************************************
 * SYNOPSIS:	    Esp data directives as the values of a C initializer.
 * RETURN:	    the C text (allocated); *count the number of values
 *
 * STRATEGY:	    A directive a line (a ';' starts a comment):
 *		    byte word dword db dw dd sbyte sword sdword
 *				    the expressions
 *		    char wchar	    the strings and characters; a 0 that
 *				    ends a string is C's
 *		    lptr nptr	    OptrToChunk(@X)
 *		    optr I_OPTR UN_OPTR	    @X
 *		    hptr I_HPTR UN_HPTR	    the resource's handle
 *		    UN_DD fptr	    a class, else the expression
 *		    <Type> <...>    { ... }
 *		    "bytes": for a gstring or a chunk of data, which goc
 *		    takes as an array of bytes.  Then:
 *		    word a	    GOC_WORD(a)	    (dword: GOC_SDW)
 *		    char "ab"	    'a', 'b'
 *		    lptr A	    GOC_WORD(GOCR_SCHUNK(A))
 *		    optr A	    GOC_SOPTR(A), the 8 bytes of the optr
 *		    GSBeginString   nothing (goc's gstring starts one)
 *		    GS<Name> a, b   GS<Name>(a, b), the macro of gstring.h
 *		    Bitmap <a, b>   Bitmap(a, b), the same
 *		    <Type> <a, <b>> GOC_<Type>(a, b), a macro to be had
 *				    (alone in a chunk: see OutputGocChunk)
 *		    if elif else endif	Esp's conditions, decided here
 *				    (EspCondition): the lines of the
 *				    branch had, the others not
 *		    X label byte    nothing: X is the number of bytes
 *				    before it (EspLabels), for the size of
 *				    a bitmap of the gstring, (EndX-StartX)
 *		    A "|" ends a line (macros of .ui files use it).
 *		    espDataError: why the data cannot be written (else
 *		    NULL), for the caller to report.
 ***********************************************************************/
/*
 * The labels of a gstring: the bytes before them.  The size of a macro
 * of gstring.h is not known here: after one the bytes are counted anew
 * (part), and a difference of labels across one cannot be had.
 */
#define ESP_MAX_LABELS	64
static struct {
    char    *name;
    int	    part, off;
} espLabels[ESP_MAX_LABELS];
static int  	espNumLabels;
static const char *espDataError;

static int
EspLabel(const char *name, int len)
{
    int	i;

    for (i = 0; i < espNumLabels; i++) {
	if ((int)strlen(espLabels[i].name) == len &&
	    strncmp(espLabels[i].name, name, len) == 0) {
	    return i;
	}
    }
    return -1;
}

/*
 * The labels in the text (made by EspData) replaced by their numbers of
 * bytes; the labels of an operand must be of the same part.
 */
static char *
EspLabels(char *text)
{
    Str	    	s = {NULL, 0, 0};
    const char	*p = text;
    int	    	depth = 0, part = -1;

    StrAdd(&s, "", 0);
    while (*p != '\0') {
	if (isalpha((unsigned char)*p) || *p == '_') {
	    const char	*start = p;
	    int	    	i;

	    while (isalnum((unsigned char)*p) || *p == '_') {
		p++;
	    }
	    i = EspLabel(start, p - start);
	    if (i < 0) {
		StrAdd(&s, start, p - start);
	    } else {
		char	num[16];

		if (part >= 0 && part != espLabels[i].part) {
		    espDataError = "uic -g: a difference of labels across a "
			"macro of gstring.h (its size is not known)";
		}
		part = espLabels[i].part;
		sprintf(num, "%d", espLabels[i].off);
		StrAdd(&s, num, -1);
	    }
	    continue;
	}
	if (*p == '(') {
	    depth++;
	} else if (*p == ')') {
	    depth--;
	} else if (*p == ',' && depth == 0) {
	    part = -1;
	}
	StrAdd(&s, p, 1);
	p++;
    }
    free(text);
    return s.text;
}

/*
 * The value of the expression of an Esp condition (if, elif), in C
 * (EspExprTo): numbers, TRUE and FALSE, ( ), the unary - ~ ! and the
 * binary operators of C.  A name with no value (not given with -D, which
 * uicpp puts in) sets espDataError.
 */
static const char *espEvalP;

static long EspEvalBinary(int prec);

static void
EspEvalSpace(void)
{
    while (isspace((unsigned char)*espEvalP)) {
	espEvalP++;
    }
}

static long
EspEvalUnary(void)
{
    EspEvalSpace();
    if (*espEvalP == '(') {
	long	v;

	espEvalP++;
	v = EspEvalBinary(0);
	EspEvalSpace();
	if (*espEvalP == ')') {
	    espEvalP++;
	}
	return v;
    } else if (*espEvalP == '-' || *espEvalP == '~' || *espEvalP == '!' ||
	       *espEvalP == '+') {
	char	op = *espEvalP++;
	long	v = EspEvalUnary();

	return op == '-' ? -v : op == '~' ? ~v : op == '!' ? !v : v;
    } else if (isdigit((unsigned char)*espEvalP)) {
	return strtol(espEvalP, (char **)&espEvalP, 0);
    } else if (isalpha((unsigned char)*espEvalP) || *espEvalP == '_') {
	const char	*start = espEvalP;

	while (isalnum((unsigned char)*espEvalP) || *espEvalP == '_') {
	    espEvalP++;
	}
	if (WordIs(start, espEvalP - start, "true")) {
	    return -1;
	} else if (WordIs(start, espEvalP - start, "false")) {
	    return 0;
	}
	espDataError = "uic -g: a name with no value in a condition of the "
	    "data (an Esp constant: give it with -D)";
	return 0;
    }
    espDataError = "uic -g: a condition of the data not understood";
    return 0;
}

static long
EspEvalBinary(int prec)
{
    static const struct { const char *op; int prec; } ops[] = {
	{"||", 1}, {"&&", 2}, {"|", 3}, {"^", 4}, {"&", 5},
	{"==", 6}, {"!=", 6}, {"<=", 7}, {">=", 7}, {"<<", 8}, {">>", 8},
	{"<", 7}, {">", 7}, {"+", 9}, {"-", 9}, {"*", 10}, {"/", 10},
	{"%", 10}, {NULL, 0}
    };
    long    v = EspEvalUnary();

    for (;;) {
	int	i;
	long	w;

	EspEvalSpace();
	for (i = 0; ops[i].op != NULL; i++) {
	    size_t  n = strlen(ops[i].op);

	    if (strncmp(espEvalP, ops[i].op, n) == 0 &&
		!(n == 1 && (espEvalP[1] == '|' || espEvalP[1] == '&') &&
		  espEvalP[1] == *espEvalP)) {
		break;
	    }
	}
	if (ops[i].op == NULL || ops[i].prec <= prec) {
	    return v;
	}
	espEvalP += strlen(ops[i].op);
	w = EspEvalBinary(ops[i].prec);
	switch (ops[i].op[0] * 256 + ops[i].op[1]) {
	    case '|' * 256 + '|': v = v || w; break;
	    case '&' * 256 + '&': v = v && w; break;
	    case '|' * 256: v |= w; break;
	    case '^' * 256: v ^= w; break;
	    case '&' * 256: v &= w; break;
	    case '=' * 256 + '=': v = v == w; break;
	    case '!' * 256 + '=': v = v != w; break;
	    case '<' * 256 + '=': v = v <= w; break;
	    case '>' * 256 + '=': v = v >= w; break;
	    case '<' * 256 + '<': v <<= w; break;
	    case '>' * 256 + '>': v >>= w; break;
	    case '<' * 256: v = v < w; break;
	    case '>' * 256: v = v > w; break;
	    case '+' * 256: v += w; break;
	    case '-' * 256: v -= w; break;
	    case '*' * 256: v *= w; break;
	    case '/' * 256: v = w ? v / w : 0; break;
	    case '%' * 256: v = w ? v % w : 0; break;
	}
    }
}

static int
EspCondition(const char *args, int alen)
{
    Str	    c = {NULL, 0, 0};
    long    v;

    StrAdd(&c, "", 0);
    EspExprTo(&c, args, alen);
    espEvalP = c.text;
    v = EspEvalBinary(0);
    free(c.text);
    return v != 0;
}

static char *
EspData(const char *in, int *count, int bytes)
{
    Str	    	s = {NULL, 0, 0};
    const char	*p = in;
    int	    	n = 0;
    const char	*lastWord = NULL;
    int	    	lastWlen = 0;
    int	    	off = 0, part = 0;	/* the bytes (bytes != 0) */
    int	    	i;
    /* Esp's if/elif/else/endif: for each level, whether its lines are
     * had (1), not yet (0), or no more (2: a branch was had) */
    int	    	cond[32], ncond = 0, skip = 0;

    for (i = 0; i < espNumLabels; i++) {
	free(espLabels[i].name);
    }
    espNumLabels = 0;
    espDataError = NULL;
    StrAdd(&s, "", 0);
    while (*p != '\0') {
	const char  *word, *args, *eol, *q;
	int 	    wlen, depth, alen;
	char	    quote;

	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
	    p++;
	}
	if (*p == '\0') {
	    break;
	}
	/* the line, without a comment */
	for (eol = p, quote = 0; *eol != '\0' && *eol != '\n'; eol++) {
	    if (quote) {
		if (*eol == '\\' && eol[1] != '\0') {
		    eol++;
		} else if (*eol == quote) {
		    quote = 0;
		}
	    } else if (*eol == '"' || *eol == '\'') {
		quote = *eol;
	    } else if (*eol == ';' || *eol == '|') {
		break;
	    }
	}
	word = p;
	while (p < eol && !isspace((unsigned char)*p) && *p != '<') {
	    p++;
	}
	wlen = p - word;
	if (lastWord != NULL &&
	    (isdigit((unsigned char)*word) || *word == '"' ||
	     *word == '\'' || *word == '-' || *word == '(')) {
	    /* more operands of the line before (it ended with a comma) */
	    p = word;
	    word = lastWord;
	    wlen = lastWlen;
	} else {
	    lastWord = word;
	    lastWlen = wlen;
	}
	while (p < eol && isspace((unsigned char)*p)) {
	    p++;
	}
	args = p;
	alen = eol - args;
	while (alen > 0 && isspace((unsigned char)args[alen - 1])) {
	    alen--;
	}
	if (wlen > 0 && (WordIs(word, wlen, "if") || WordIs(word, wlen, "elif") ||
			 WordIs(word, wlen, "else") ||
			 WordIs(word, wlen, "endif"))) {
	    /* a condition of Esp (cpp's #if is uicpp's) */
	    if (WordIs(word, wlen, "if")) {
		if (ncond == 32) {
		    espDataError = "uic -g: conditions too deep in the data";
		} else {
		    cond[ncond++] = skip ? 2 : EspCondition(args, alen);
		}
	    } else if (ncond == 0) {
		espDataError = "uic -g: an elif, else or endif without if";
	    } else if (WordIs(word, wlen, "endif")) {
		ncond--;
	    } else if (cond[ncond - 1] == 1) {
		cond[ncond - 1] = 2;
	    } else if (cond[ncond - 1] == 0) {
		cond[ncond - 1] = WordIs(word, wlen, "else") ? 1 :
		    EspCondition(args, alen);
	    }
	    for (skip = 0, i = 0; i < ncond; i++) {
		if (cond[i] != 1) {
		    skip = 1;
		}
	    }
	    lastWord = NULL;
	    wlen = 0;
	} else if (skip) {
	    lastWord = NULL;
	    wlen = 0;
	}
	if (bytes && wlen > 0 && alen >= 5 && WordIs(args, 5, "label") &&
	    (alen == 5 || isspace((unsigned char)args[5]))) {
	    /* X label byte: the bytes before it */
	    if (EspLabel(word, wlen) >= 0) {
		espDataError = "uic -g: a label twice in a gstring";
	    } else if (espNumLabels == ESP_MAX_LABELS) {
		espDataError = "uic -g: too many labels in a gstring";
	    } else {
		espLabels[espNumLabels].name = malloc(wlen + 1);
		memcpy(espLabels[espNumLabels].name, word, wlen);
		espLabels[espNumLabels].name[wlen] = '\0';
		espLabels[espNumLabels].part = part;
		espLabels[espNumLabels].off = off;
		espNumLabels++;
	    }
	    lastWord = NULL;
	    wlen = 0;
	}
	if (wlen > 0) {
	    int	isChar = WordIs(word, wlen, "char") ||
			 WordIs(word, wlen, "wchar");
	    int	isChunk = WordIs(word, wlen, "lptr") ||
			  WordIs(word, wlen, "nptr");
	    int	isOptr = WordIs(word, wlen, "optr") ||
			 WordIs(word, wlen, "i_optr") ||
			 WordIs(word, wlen, "un_optr");
	    int	isClass = WordIs(word, wlen, "un_dd") ||
			  WordIs(word, wlen, "fptr");
	    int isPlain = WordIs(word, wlen, "byte") ||
			  WordIs(word, wlen, "word") ||
			  WordIs(word, wlen, "dword") ||
			  WordIs(word, wlen, "sbyte") ||
			  WordIs(word, wlen, "sword") ||
			  WordIs(word, wlen, "sdword") ||
			  WordIs(word, wlen, "db") ||
			  WordIs(word, wlen, "dw") ||
			  WordIs(word, wlen, "dd");

	    int width = (WordIs(word, wlen, "word") ||
			 WordIs(word, wlen, "sword") ||
			 WordIs(word, wlen, "dw")) ? 2 :
			(WordIs(word, wlen, "dword") ||
			 WordIs(word, wlen, "sdword") ||
			 WordIs(word, wlen, "dd")) ? 4 : 1;

	    if (bytes && WordIs(word, wlen, "gsbeginstring")) {
		/* nothing */
	    } else if (bytes && !(isChar || isChunk || isOptr || isClass ||
				  isPlain)) {
		/* a macro of gstring.h */
		Str a = {NULL, 0, 0};
		int i;

		if (wlen == 6 && strncmp(word, "Bitmap", 6) == 0) {
		    off += 6;	/* (Bitmap: 2 words, 2 bytes) */
		} else {
		    part++, off = 0;
		}

		if (n++ > 0) {
		    StrAdd(&s, ",\n\t", 3);
		}
		if (strncmp(word, "GS", 2) != 0 &&
		    !(wlen == 6 && strncmp(word, "Bitmap", 6) == 0)) {
		    StrAdd(&s, "GOC_", 4);
		}
		StrAdd(&s, word, wlen);
		StrAdd(&s, "(", 1);
		StrAdd(&a, "", 0);
		EspExprTo(&a, args, alen);
		if (a.len >= 2 && a.text[0] == '{' && a.text[a.len - 1] == '}' &&
		    strchr(a.text, ',') == NULL) {
		    /* <(x)>: a value of its own */
		    a.text[0] = ' ';
		    a.text[a.len - 1] = ' ';
		}
		if (strncmp(word, "GS", 2) != 0) {
		    /* (a structure's values are the macro's arguments) */
		    for (i = 0; i < a.len; i++) {
			if (a.text[i] == '{' || a.text[i] == '}') {
			    a.text[i] = ' ';
			}
		    }
		}
		StrAdd(&s, a.text, a.len);
		free(a.text);
		StrAdd(&s, ")", 1);
	    } else if (!(isChar || isChunk || isOptr || isClass || isPlain)) {
		/* a structure: <Type> <values> */
		if (n++ > 0) {
		    StrAdd(&s, ", ", 2);
		}
		if (WordIs(word, wlen, "specwidth") ||
		    WordIs(word, wlen, "specheight") ||
		    WordIs(word, wlen, "specsizespec")) {
		    /* a record that is a word in C: its fields or'ed */
		    const char *a = args, *e = args + alen;
		    int	       parts = 0;

		    if (a < e && *a == '<') {
			a++;
		    }
		    if (e > a && e[-1] == '>') {
			e--;
		    }
		    StrAdd(&s, "(", 1);
		    while (a < e) {
			const char *c = a;

			while (c < e && *c != ',') {
			    c++;
			}
			while (a < c && isspace((unsigned char)*a)) {
			    a++;
			}
			if (a < c) {
			    if (parts++ > 0) {
				StrAdd(&s, " | ", 3);
			    }
			    StrAdd(&s, "(", 1);
			    EspExprTo(&s, a, c - a);
			    StrAdd(&s, ")", 1);
			}
			a = c < e ? c + 1 : e;
		    }
		    if (parts == 0) {
			StrAdd(&s, "0", 1);
		    }
		    StrAdd(&s, ")", 1);
		} else if (alen == 0) {
		    StrAdd(&s, "{0}", 3);
		} else {
		    EspExprTo(&s, args, alen);
		}
	    } else {
		/* the operands, at the commas outside of <>, () and quotes */
		q = args;
		while (q < args + alen) {
		    const char	*op = q;
		    int	    	olen;

		    for (depth = 0, quote = 0; q < args + alen; q++) {
			if (quote) {
			    if (*q == '\\') {
				q++;
			    } else if (*q == quote) {
				quote = 0;
			    }
			} else if (*q == '"' || *q == '\'') {
			    quote = *q;
			} else if (*q == '<' || *q == '(') {
			    depth++;
			} else if (*q == '>' || *q == ')') {
			    depth--;
			} else if (*q == ',' && depth == 0) {
			    break;
			}
		    }
		    olen = q - op;
		    if (q < args + alen) {
			q++;
		    }
		    while (olen > 0 && isspace((unsigned char)*op)) {
			op++, olen--;
		    }
		    while (olen > 0 && isspace((unsigned char)op[olen - 1])) {
			olen--;
		    }
		    if (olen == 0) {
			continue;
		    }
		    if (isChar && olen == 1 && *op == '0' && n > 0 &&
			s.len > 0 && s.text[s.len - 1] == '"') {
			/* the 0 after a string */
			continue;
		    }
		    if (n++ > 0) {
			StrAdd(&s, ", ", 2);
		    }
		    if (bytes) {
			if (isChunk) {
			    off += 2;
			} else if (isOptr) {
			    off += 8;
			} else if (isChar && olen >= 2 && *op == '"') {
			    off += olen - 2;	/* (near enough: a label
						 * after a string with \ is
						 * not had) */
			} else if (isChar) {
			    off += WordIs(word, wlen, "wchar") ? 2 : 1;
			} else if (isPlain) {
			    off += width;
			} else {
			    part++, off = 0;
			}
		    }
		    if (bytes && (isChunk || isOptr)) {
			if (olen == 1 && *op == '0') {
			    StrAdd(&s, isChunk ? "0, 0" :
				   "0, 0, 0, 0, 0, 0, 0, 0", -1);
			} else {
			    StrAdd(&s, isChunk ? "GOC_WORD(GOCR_SCHUNK(" :
				   "GOC_SOPTR(", -1);
			    StrAdd(&s, op, olen);
			    StrAdd(&s, isChunk ? "))" : ")", -1);
			}
		    } else if (bytes && isChar && olen >= 2 && *op == '"') {
			/* a string, character by character */
			int i, m = 0;

			for (i = 1; i < olen - 1; i++) {
			    if (m++ > 0) {
				StrAdd(&s, ", ", 2);
			    }
			    StrAdd(&s, "'", 1);
			    if (op[i] == '\\' && i + 1 < olen - 1) {
				StrAdd(&s, op + i++, 2);
			    } else if (op[i] == '\'') {
				StrAdd(&s, "\\'", 2);
			    } else {
				StrAdd(&s, op + i, 1);
			    }
			    StrAdd(&s, "'", 1);
			}
			if (m == 0) {
			    n--;
			    if (s.len >= 2 && n > 0) {
				s.len -= 2;	/* (the ", ") */
				s.text[s.len] = '\0';
			    }
			}
		    } else if (bytes && isPlain && width > 1) {
			StrAdd(&s, width == 2 ? "GOC_WORD(" : "GOC_SDW(", -1);
			EspExprTo(&s, op, olen);
			StrAdd(&s, ")", 1);
		    } else if (isChunk || (isOptr && ChunkSym(op, olen)) ||
			       (WordIs(word, wlen, "word") &&
				ChunkSym(op, olen))) {
			/* (a word that names a chunk is its handle) */
			int ch = isChunk || !isOptr;

			if (olen == 1 && *op == '0') {
			    StrAdd(&s, "0", 1);
			} else {
			    StrAdd(&s, ch ? "OptrToChunk(@" : "@", -1);
			    StrAdd(&s, op, olen);
			    if (ch) {
				StrAdd(&s, ")", 1);
			    }
			}
		    } else if (isClass && olen > 5 &&
			       strncmp(op + olen - 5, "Class", 5) == 0) {
			StrAdd(&s, "(ClassStruct *) &", -1);
			StrAdd(&s, op, olen);
		    } else {
			EspExprTo(&s, op, olen);
		    }
		}
	    }
	}
	p = eol;
	if (*p == '|') {
	    p++;
	} else {
	    while (*p != '\0' && *p != '\n') {
		p++;
	    }
	}
    }
    if (ncond != 0) {
	espDataError = "uic -g: an if without endif in the data";
    }
    *count = n;
    return espNumLabels > 0 ? EspLabels(s.text) : s.text;
}

/*
 * The name of an instance variable for goc: by its position
 */
static void
OutputFieldName(Symbol *class, int index, int count)
{
    Output("    __uic_%d_%d_%sClass = ", index, count, class->name);
}

/*
 * The number of instance variables a component stands for
 */
static int
CompFields(Symbol *comp)
{
    return comp->type == ACTION_COMP_SYM ? 2 : 1;
}

static void
OutputOptrValue(ObjectField *field)
{
    switch (field->data.fieldOptr.type) {
	case OPTR_NULL:
	    Output("NullOptr");
	    break;
	case OPTR_PROCESS:
	    Output("process");
	    break;
	case OPTR_OBJECT:
	    Output("@%s", field->data.fieldOptr.data.obj.dest->name);
	    break;
	case OPTR_STRING:
	case OPTR_STRING_OPTR: {
	    char *c = EspExpr(field->data.fieldOptr.data.proc.extra);

	    Output("(%s)", c);
	    free(c);
	    break;
	}
    }
}

static void
OutputKbdAccelerator(Symbol *object, ObjectField *field)
{
    int	    flags = field->data.fieldKbdAccelerator.flags;
    int	    key = field->data.fieldKbdAccelerator.key;
    char    *name;

    if (field->data.fieldKbdAccelerator.specificUI) {
	Output("specificUI ");
    }
    if (flags & M_ALT) {
	Output("alt ");
    }
    if (flags & M_CTRL) {
	Output("control ");
    }
    if (flags & M_SHIFT) {
	Output("shift ");
    }
    switch (key >> 16) {
	case KEY_ALPHA: case KEY_DIGIT: case KEY_PUNCT:
	    if ((key & 0xffff) == '\'' || (key & 0xffff) == '\\') {
		Output("'\\%c'", key & 0xffff);
	    } else {
		Output("'%c'", key & 0xffff);
	    }
	    break;
	default:
	    name = Scan_KbdKeyName(key);
	    if (name == NULL) {
		uicerror(object, "keyboard accelerator: key 0x%x has no name",
			 key);
		name = "SPACE";
	    }
	    Output("%s", name);
	    break;
    }
}

/*
 * The instance data a class gives the object, superclasses first
 */
static void
OutputClassData(Symbol *class, Symbol *object)
{
    Symbol	*comp;
    ObjectField	*field, *tempField;
    int		index, count;

    if (class->data.symClass.superclass != NullSymbol) {
	OutputClassData(class->data.symClass.superclass, object);
    }
    for (count = 0, comp = class->data.symClass.componentPtr;
	 comp != NullSymbol; comp = comp->data.symByteComp.next) {
	count += CompFields(comp);
    }
    if ((class->flags & SYM_CLASS_VARIANT) && count == 0) {
	return;
    }
    for (index = 0, comp = class->data.symClass.componentPtr;
	 comp != NullSymbol;
	 index += CompFields(comp), comp = comp->data.symByteComp.next) {
	int explicit = 0;

	if (comp->flags & SYM_STATIC) {
	    continue;
	}
	field = NullObjectField;
	for (tempField = object->data.symObject.firstField;
	     tempField != NullObjectField; tempField = tempField->next) {
	    if (tempField->type == comp) {
		field = tempField;
		explicit = 1;
		break;
	    }
	}
	if (field == NullObjectField) {
	    field = FindDefault(object->data.symObject.class, comp);
	    /* a default of a class counts, the component's own is goc's */
	    explicit = (field != comp->data.symByteComp.defaultValue);
	}
	switch (comp->type) {
	    case BIT_FIELD_COMP_SYM: {
		extern int FindBitFieldValue(ObjectField *, Symbol *, Symbol *);
		int v = FindBitFieldValue(field,
					  object->data.symObject.class, comp);

		if (object->flags & SYM_HAS_KBD_ACCEL) {
		    v |= comp->data.symBitFieldComp.kbdPathMask;
		}
		OutputFieldName(class, index, count);
		Output("0x%x;\n", v);
		break;
	    }
	    case ENUM_COMP_SYM:
		OutputFieldName(class, index, count);
		Output("%d;\n", field->data.fieldEnum.value);
		break;
	    case BYTE_COMP_SYM: case WORD_COMP_SYM: case DWORD_COMP_SYM:
	    case FPTR_COMP_SYM: {
		char *c;

		if (!explicit || field == NullObjectField) {
		    break;
		}
		c = EspExpr(field->data.fieldByte.value);
		OutputFieldName(class, index, count);
		if (comp->type != FPTR_COMP_SYM &&
		    field->data.fieldByte.type != NULL) {
		    Output("{ %s };\n", c);
		} else {
		    Output("%s;\n", c);
		}
		free(c);
		break;
	    }
	    case TYPE_COMP_SYM: {
		char *c;

		if (!explicit || field == NullObjectField ||
		    field->data.fieldType.value[0] == '\0') {
		    break;
		}
		c = EspExpr(field->data.fieldType.value);
		OutputFieldName(class, index, count);
		Output("{ %s };\n", c);
		free(c);
		break;
	    }
	    case COMPOSITE_COMP_SYM: {
		Symbol *child;
		ObjectField *link;
		Symbol *linkComp = comp->data.symCompositeComp.linkSym;
		int n = 0;

		if (field == NullObjectField ||
		    field->data.fieldComposite.firstChild == NullSymbol) {
		    break;
		}
		OutputFieldName(class, index, count);
		/* the children: along the links, to the one back up */
		for (child = field->data.fieldComposite.firstChild;
		     child != NullSymbol && child != object; ) {
		    Output("%s@%s", n++ ? ",\n\t" : "", child->name);
		    for (link = child->data.symObject.firstField;
			 link != NullObjectField; link = link->next) {
			if (link->type == linkComp) {
			    break;
			}
		    }
		    if (link == NullObjectField ||
			link->data.fieldLink.isParentLink) {
			break;
		    }
		    child = link->data.fieldLink.link;
		}
		Output(";\n");
		break;
	    }
	    case LINK_COMP_SYM:
		/* (goc links the children of a composite itself) */
		break;
	    case VIS_MONIKER_COMP_SYM:
		if (field == NullObjectField ||
		    field->data.fieldVisMoniker.moniker == NullSymbol) {
		    break;
		}
		OutputFieldName(class, index, count);
		if (field->data.fieldVisMoniker.moniker->flags &
		    SYM_GOC_INLINE) {
		    OutputGocVisMonikerDef(
				field->data.fieldVisMoniker.moniker);
		} else {
		    Output("%s;\n",
			   field->data.fieldVisMoniker.moniker->name);
		}
		break;
	    case KBD_ACCELERATOR_COMP_SYM:
		if (field == NullObjectField) {
		    break;
		}
		OutputFieldName(class, index, count);
		OutputKbdAccelerator(object, field);
		Output(";\n");
		break;
	    case OPTR_COMP_SYM:
		if (!explicit || field == NullObjectField ||
		    field->data.fieldOptr.type == OPTR_NULL) {
		    break;
		}
		OutputFieldName(class, index, count);
		OutputOptrValue(field);
		Output(";\n");
		break;
	    case ACTION_COMP_SYM:
		if (!explicit || field == NullObjectField) {
		    break;
		}
		if (field->data.fieldAction.type != OPTR_NULL) {
		    OutputFieldName(class, index, count);
		    OutputOptrValue(field);
		    Output(";\n");
		}
		if (field->data.fieldAction.method != NullSymbol) {
		    OutputFieldName(class, index + 1, count);
		    Output("%s;\n", field->data.fieldAction.method->name);
		}
		break;
	    case NPTR_COMP_SYM:
		if (field == NullObjectField ||
		    field->data.fieldNptr.target == NullSymbol) {
		    break;
		}
		OutputFieldName(class, index, count);
		Output("OptrToChunk(@%s);\n",
		       field->data.fieldNptr.target->name);
		break;
	    case HPTR_COMP_SYM:
		if (field == NullObjectField ||
		    field->data.fieldHptr.target == NullSymbol) {
		    break;
		}
		uicerror(object, "uic -g: component <%s> of %s: a handle is "
			 "not written as goc yet", comp->name, class->name);
		break;
	    case VARIANT_PTR_SYM:
		/* (goc resolves the variant superclass) */
		break;
	    case HINT_COMP_SYM:
		/* (variable data, see OutputVarData) */
		break;
	    default:
		uicerror(object, "uic -g: component <%s> of %s is of a kind "
			 "that is not written as goc yet",
			 comp->name, class->name);
		break;
	}
    }
}

static void
OutputVarData(Symbol *object)
{
    Symbol	*hSym = object->data.symObject.varData;
    Symbol	*lists = object->data.symObject.gcnListOfLists;
    ObjectField	*hint;

    if (hSym != NullSymbol) {
	for (hint = hSym->data.symHintList.firstHint;
	     hint != NullObjectField; hint = hint->next) {
	    if (VarDataOmitted(hint->data.fieldHintEntry.name->name)) {
		continue;
	    } else if (hint->data.fieldHintEntry.data != NULL) {
		int	n;
		char	*c = EspData(hint->data.fieldHintEntry.data, &n, 0);
		char	*name = hint->data.fieldHintEntry.name->name;

		/*
		 * A trigger's action data is as many words as the message
		 * takes: C has a name for each number of them (gTrigC.goh).
		 */
		if (strcmp(name, "ATTR_GEN_TRIGGER_ACTION_DATA") == 0) {
		    if (n == 2) {
			name = "ATTR_GEN_TRIGGER_ACTION_TWO_WORDS";
		    } else if (n == 3) {
			name = "ATTR_GEN_TRIGGER_ACTION_THREE_WORDS";
		    }
		}
		if (n == 2 && (strcmp(name, "HINT_FIXED_SIZE") == 0 ||
			       strcmp(name, "HINT_INITIAL_SIZE") == 0 ||
			       strcmp(name, "HINT_MINIMUM_SIZE") == 0 ||
			       strcmp(name, "HINT_MAXIMUM_SIZE") == 0)) {
		    /* CompSizeHintArgs without its count of children */
		    Output("    %s = { %s, 0 };\n", name, c);
		} else if (n == 1 && strncmp(c, "(ClassStruct *)", 15) != 0) {
		    /* (a class is in a structure: DestinationClassArgs) */
		    Output("    %s = %s;\n", name, c);
		} else {
		    Output("    %s = { %s };\n", name, c);
		}
		free(c);
	    } else {
		Output("    %s;\n", hint->data.fieldHintEntry.name->name);
	    }
	}
    }
    if (lists != NullSymbol) {
	Symbol *list;
	ObjectField *item;

	for (list = lists->data.symGCNListOfLists.firstList;
	     list != NullSymbol; list = list->data.symGCNList.nextList) {
	    char *manuf = EspExpr(list->data.symGCNList.manufID);
	    char *type = EspExpr(list->data.symGCNList.type);
	    int n = 0;

	    Output("    gcnList(%s, %s) = ", manuf, type);
	    for (item = list->data.symGCNList.firstItem;
		 item != NullObjectField; item = item->next) {
		Output("%s@%s", n++ ? ", " : "",
		       item->data.fieldActive.element->name);
	    }
	    Output(";\n");
	    free(manuf);
	    free(type);
	}
    }
}

static void
OutputGocObject(Symbol *sym)
{
    Output("@object %sClass %s = ", sym->data.symObject.class->name,
	   sym->name);
    if (sym->flags & SYM_IGNORE_DIRTY) {
	Output("@ignoreDirty ");
    }
    Output("{\n");
    OutputClassData(sym->data.symObject.class, sym);
    OutputVarData(sym);
    Output("}\n");
}

/*
 * A moniker's definition: what follows the "=", with the ";"
 */
static void
OutputGocVisMonikerDef(Symbol *sym)
{
    static const char *styles[] = {"text", "abbrevText", "graphicText",
				   "icon", "tool"};
    static const char *sizes[] = {"tiny", "standard", "large", "huge"};
    static const char *colors[] = {"gray1", "gray2", "gray4", "gray8",
				   "color2", "color4", "color8", "colorRGB"};
    static const char *ratios[] = {"normal", "squished", "verySquished"};
    ObjectField *field;
    int		n = 0, flags;
    char	*data;

    if (sym->flags & SYM_LIST_MONIKER) {
	Output("list {");
	for (field = sym->data.symVisMoniker.data.list.firstField;
	     field != NullObjectField; field = field->next) {
	    Output("%s\n\t@%s", n++ ? "," : "",
		   field->data.fieldVMElement.element->name);
	}
	Output("\n    }\n");
	return;
    }
    flags = sym->data.symVisMoniker.data.nonList.flags;
    data = sym->data.symVisMoniker.data.nonList.data;
    if (sym->flags & (SYM_GRAPHIC_MONIKER | SYM_DATA_MONIKER)) {
	int	count;
	char	*c;

	if (sym->flags & SYM_DATA_MONIKER) {
	    uicerror(sym, "uic -g: a moniker of data is not written as goc "
		     "yet");
	    Output("\"\";\n");
	    return;
	}
	c = EspData(data, &count, 1);
	if (espDataError != NULL) {
	    uicerror(sym, "%s", espDataError);
	}
	Output("{\n");
	Output("    style = %s;\n", styles[(flags & V20_VMT_STYLE) >>
					   V20_VMT_STYLE_FIELD]);
	Output("    size = %s;\n", sizes[(flags & VMT_GS_SIZE) >>
					 VMT_GS_SIZE_FIELD]);
	Output("    color = %s;\n", colors[((flags & VMT_GS_COLOR) >>
					    VMT_GS_COLOR_FIELD) - 1]);
	Output("    aspectRatio = %s;\n",
	       ratios[(flags & VMT_GS_ASPECT_RATIO) >>
		      VMT_GS_ASPECT_RATIO_FIELD]);
	Output("    cachedSize = %d, %d;\n",
	       sym->data.symVisMoniker.data.nonList.xSize,
	       sym->data.symVisMoniker.data.nonList.ySize);
	Output("    gstring {\n\t%s\n    }\n}\n", c);
	free(c);
	return;
    }
    if (sym->flags & SYM_CONST_NAV_MONIKER) {
	Output("%d, ", sym->data.symVisMoniker.data.nonList.nav.navChar);
    } else if (sym->flags & SYM_STRING_NAV_MONIKER) {
	char *c = EspExpr(sym->data.symVisMoniker.data.nonList.nav.navString);

	/* (goc knows these only as numbers) */
	if (strcmp(c, "VMO_CANCEL") == 0) {
	    Output("253, ");
	} else if (strcmp(c, "VMO_MNEMONIC_NOT_IN_MKR_TEXT") == 0) {
	    Output("254, ");
	} else if (strcmp(c, "VMO_NO_MNEMONIC") == 0) {
	    Output("255, ");
	} else {
	    Output("%s, ", c);
	}
	free(c);
    } else if (sym->data.symVisMoniker.data.nonList.nav.navChar != -1) {
	int c = sym->data.symVisMoniker.data.nonList.nav.navChar;

	if (c == '\'' || c == '\\') {
	    Output("'\\%c', ", c);
	} else {
	    Output("'%c', ", c);
	}
    }
    Output("\"%s\";\n", data);
}

static void
OutputGocVisMoniker(Symbol *sym)
{
    Output("@visMoniker %s = ", sym->name);
    OutputGocVisMonikerDef(sym);
}

/*
 * The monikers that uic named after an object and its component
 * (<object>_<component>) are written with the object.
 */
static void
MarkInlineMonikers(Symbol *resource)
{
    Symbol	*sym;
    ObjectField	*field;

    for (sym = resource->data.symResource.firstObject; sym != NullSymbol;
	 sym = sym->data.symObject.next) {
	if (sym->type != OBJECT_SYM) {
	    continue;
	}
	for (field = sym->data.symObject.firstField;
	     field != NullObjectField; field = field->next) {
	    Symbol *mon;
	    int n = strlen(sym->name);

	    if (field->type == NullSymbol ||
		field->type->type != VIS_MONIKER_COMP_SYM ||
		(mon = field->data.fieldVisMoniker.moniker) == NullSymbol) {
		continue;
	    }
	    if (mon->type == VIS_MONIKER_SYM &&
		strncmp(mon->name, sym->name, n) == 0 &&
		mon->name[n] == '_' &&
		strcmp(mon->name + n + 1, field->type->name) == 0) {
		mon->flags |= SYM_GOC_INLINE;
	    }
	}
    }
}

/***********************************************************************
 *				MonikerList
 ***********************************************************************
 * SYNOPSIS:	    A chunk of data that is a moniker list made by hand
 *		    (cmainLowDisk.ui): "VisMonikerListEntryType <...>"
 *		    and an optr (UN_OPTR, I_OPTR, optr) to the moniker,
 *		    as often as there are monikers.  It is written as a
 *		    goc moniker list ("@visMoniker Name = list {...}"),
 *		    which has the type of each entry from its moniker
 *		    (one of another file: from <geode>_objs.h,
 *		    __goc_xvt_<moniker>).
 * RETURN:	    the number of entries, 0 if the chunk is something
 *		    else.  With out: "@Name" of each, a comma between;
 *		    with names: the names, each with its 0.
***********************************************************************/
static const char *
MonikerListSkip(const char *p)
{
    for (;;) {
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
	       *p == '|') {
	    p++;
	}
	if (*p != ';') {
	    return p;
	}
	while (*p != '\0' && *p != '\n') {
	    p++;
	}
    }
}

static const char *
MonikerListWord(const char *p, int *len)
{
    const char *w = p;

    while (isalnum((unsigned char)*p) || *p == '_') {
	p++;
    }
    *len = p - w;
    return w;
}

static int
MonikerList(const char *p, Str *out, Str *names)
{
    int	    	n = 0;

    for (;;) {
	const char  *w;
	int 	    wl, depth;

	p = MonikerListSkip(p);
	if (*p == '\0') {
	    return n;
	}
	w = MonikerListWord(p, &wl);
	p = w + wl;
	if (!WordIs(w, wl, "vismonikerlistentrytype")) {
	    return 0;
	}
	p = MonikerListSkip(p);
	if (*p != '<') {
	    return 0;
	}
	for (p++, depth = 1; *p != '\0'; p++) {
	    if (*p == '<') {
		depth++;
	    } else if (*p == '>' && --depth == 0) {
		break;
	    }
	}
	if (*p == '\0') {
	    return 0;
	}
	p = MonikerListSkip(p + 1);
	w = MonikerListWord(p, &wl);
	p = w + wl;
	if (!WordIs(w, wl, "optr") && !WordIs(w, wl, "i_optr") &&
	    !WordIs(w, wl, "un_optr")) {
	    return 0;
	}
	while (*p == ' ' || *p == '\t') {
	    p++;
	}
	w = MonikerListWord(p, &wl);
	p = w + wl;
	if (wl == 0) {
	    return 0;
	}
	if (out != NULL) {
	    int i;

	    StrAdd(out, n > 0 ? ",\n    @" : "    @", n > 0 ? 7 : 5);
	    StrAdd(out, w, wl);
	}
	if (names != NULL) {
	    StrAdd(names, w, wl);
	    StrAdd(names, "", 1);
	}
	n++;
    }
}

/*
 * The monikers of hand-made moniker lists that are in another file
 * (the .ui file has "extern Name;", but the name is not to be found
 * when the file is written: those this file does not define): goc is
 * to know them as such.
 */
static void
OutputMonikerListExterns(void)
{
    Symbol  *resource, *sym;
    Str	    seen = {NULL, 0, 0};

    StrAdd(&seen, "", 0);
    for (resource = firstResource; resource != NullSymbol;
	 resource = resource->data.symResource.nextResource) {
	if (!ResourceWanted(resource)) {
	    continue;
	}
	for (sym = resource->data.symResource.firstObject;
	     sym != NullSymbol; sym = sym->data.symObject.next) {
	    Str	    	names = {NULL, 0, 0};
	    int	    	n, i;
	    const char  *name;

	    if (sym->type != CHUNK_SYM ||
		(sym->flags & (SYM_CHUNK_IS_EMPTY | SYM_CHUNK_IS_STRUCTURE |
			       SYM_CHUNK_IS_TEXT))) {
		continue;
	    }
	    StrAdd(&names, "", 0);
	    n = MonikerList(sym->data.symChunk.data.chunkText, NULL, &names);
	    for (i = 0, name = names.text; i < n; i++, name += strlen(name) + 1) {
		Symbol	*m;
		const char *q;
		int 	found = 0;

		/* (one this file defines is not) */
		m = Symbol_Find((char *)name, TRUE);
		if (m != NullSymbol && (m->flags & SYM_DEFINED) &&
		    !(m->flags & SYM_EXTERNAL)) {
		    continue;
		}
		for (q = seen.text; q < seen.text + seen.len; q += strlen(q) + 1) {
		    if (*q != '\0' && strcmp(q, name) == 0) {
			found = 1;
			break;
		    }
		}
		if (!found) {
		    Output("@extern visMoniker %s;\n", name);
		    StrAdd(&seen, name, strlen(name) + 1);
		}
	    }
	    free(names.text);
	}
    }
    free(seen.text);
}

static void
OutputGocChunk(Symbol *sym)
{
    if (sym->flags & SYM_CHUNK_IS_EMPTY) {
	Output("@chunk byte %s[] = {};\n", sym->name);
    } else if (sym->flags & SYM_CHUNK_IS_STRUCTURE) {
	uicerror(sym, "uic -g: a chunk of a structure is not written as goc "
		 "yet");
    } else if (sym->flags & SYM_CHUNK_IS_TEXT) {
	Output("@chunk TCHAR %s[] = \"%s\";\n", sym->name,
	       sym->data.symChunk.data.chunkText);
    } else if (MonikerList(sym->data.symChunk.data.chunkText, NULL,
			   NULL) > 0) {
	Str l = {NULL, 0, 0};

	StrAdd(&l, "", 0);
	(void) MonikerList(sym->data.symChunk.data.chunkText, &l, NULL);
	Output("@visMoniker %s = list {\n%s\n};\n", sym->name, l.text);
	free(l.text);
    } else {
	int	n;
	char	*c = EspData(sym->data.symChunk.data.chunkText, &n, 1);
	char	*t = c;
	int	tlen = 0, lower = 0;

	if (espDataError != NULL) {
	    uicerror(sym, "%s", espDataError);
	}

	/*
	 * One structure (<Type> <values>, which came out as the macro
	 * GOC_<Type>) is a chunk of that type, as the code will read
	 * it: the macro, beside the type in its header, makes its
	 * initializer of the values.
	 */
	while (*t == ' ' || *t == '\t' || *t == '\n') {
	    t++;
	}
	if (n == 1 && strncmp(t, "GOC_", 4) == 0) {
	    while (isalnum((unsigned char)t[4 + tlen]) ||
		   t[4 + tlen] == '_') {
		lower |= islower((unsigned char)t[4 + tlen]);
		tlen++;
	    }
	}
	if (tlen > 0 && lower && t[4 + tlen] == '(') {
	    Output("@chunk %.*s %s = %s;\n", tlen, t + 4, sym->name, t);
	} else {
	    Output("@chunk byte %s[] = {\n    %s\n};\n", sym->name, c);
	}
	free(c);
    }
}

/***********************************************************************
 *				DoGocOutput
 ***********************************************************************
 * SYNOPSIS:	    Write everything the file defines as goc source.
 * CALLED BY:	    main, for uic -g
 ***********************************************************************/
void
DoGocOutput(void)
{
    Symbol *resource, *sym;
    FILE *externs = NULL;
    int i;

    Output("/* Made by uic -g; do not edit. */\n");
    for (i = 0; i < numIncludes; i++) {
	Output("@include <%s>\n", includes[i]);
    }
    if (externFile != NULL) {
	externs = fopen(externFile, "w");
	if (externs == NULL) {
	    perror(externFile);
	    exit(1);
	}
	fprintf(externs, "/* Made by uic -g; do not edit. */\n");
    }
    OutputMonikerListExterns();
    for (resource = firstResource; resource != NullSymbol;
	 resource = resource->data.symResource.nextResource) {
	if (!ResourceWanted(resource)) {
	    continue;
	}
	curResource = resource;
	Output("\n@start %s", resource->name);
	if (resource->flags & SYM_DATA_RESOURCE) {
	    Output(", data");
	} else if (resource->flags & SYM_NOT_DETACHABLE) {
	    Output(", notDetachable");
	}
	Output(";\n");
	MarkInlineMonikers(resource);
	/* the monikers first: goc wants one defined where it is used */
	for (sym = resource->data.symResource.firstObject;
	     sym != NullSymbol; sym = sym->data.symObject.next) {
	    if (sym->type == VIS_MONIKER_SYM &&
		!(sym->flags & SYM_GOC_INLINE)) {
		OutputGocVisMoniker(sym);
	    }
	}
	for (sym = resource->data.symResource.firstObject;
	     sym != NullSymbol; sym = sym->data.symObject.next) {
	    if (externs != NULL && !(sym->flags & SYM_GOC_INLINE)) {
		switch (sym->type) {
		    case OBJECT_SYM:
			fprintf(externs, "@extern object %s;\n", sym->name);
			break;
		    case VIS_MONIKER_SYM:
			fprintf(externs, "@extern visMoniker %s;\n",
				sym->name);
			break;
		    case CHUNK_SYM:
			fprintf(externs, "@extern chunk %s;\n", sym->name);
			break;
		}
	    }
	    switch (sym->type) {
		case OBJECT_SYM:
		    OutputGocObject(sym);
		    break;
		case VIS_MONIKER_SYM:
		    /* (written already) */
		    break;
		case CHUNK_SYM:
		    OutputGocChunk(sym);
		    break;
		case GCN_LIST_SYM:
		case GCN_LIST_OF_LISTS_SYM:
		    /* (with their object: gcnList(...) = ...) */
		    break;
		case HINT_LIST_SYM:
		    /* (with their object, as variable data) */
		    break;
		default:
		    uicerror(sym, "uic -g: %s is of a kind that is not "
			     "written as goc yet", sym->name);
		    break;
	    }
	}
	Output("@end %s;\n", resource->name);
    }
    if (externs != NULL) {
	fclose(externs);
    }
}
