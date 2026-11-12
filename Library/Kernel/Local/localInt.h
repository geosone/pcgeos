/*
 * localInt.h -- what the C files of the localization code share (SBCS):
 * the tables that Port32/tools/localgen.py makes of the asm at build
 * time (localTables.c), and the code pages.
 */
#ifndef __LOCALINT_H
#define __LOCALINT_H

#include <geos.h>
#include <localize.h>

/* stringCharTables.asm: the CharClass of each character (a word, the
 * first field of the record in the highest bit) */
extern const word localCharClassTable[256];
#define CC_ALPHA	0x8000
#define CC_PUNCTUATION	0x4000
#define CC_SPACE	0x2000
#define CC_LIGATURE	0x1000
#define CC_SYMBOL	0x0800
#define CC_UPPER	0x0400
#define CC_LOWER	0x0200
#define CC_CONTROL	0x0100
#define CC_DIGIT	0x0080
#define CC_HEX		0x0040
#define CC_PRINTABLE	0x0020
#define CC_GRAPHIC	0x0010

/* stringCaseTables.asm: from MIN_UPCASE and MIN_DOWNCASE on */
extern const word LOCAL_MIN_UPCASE, LOCAL_MIN_DOWNCASE;
extern const byte localUpcaseTable[159];
extern const byte localDowncaseTable[191];

/* stringSortTables.asm: LexicalOrder and Lex1stOrder of each character */
extern const byte localLexicalOrderTable[256];
extern const byte localLexical1stOrderTable[256];

/* cmap*.asm: a code page to GEOS and back, from MIN_MAP_CHAR on; zero
 * where a character has no counterpart */
typedef struct {
    DosCodePage		LCP_page;
    const byte		*LCP_toGeos;
    const byte		*LCP_fromGeos;
} LocalCodePage;

/* GetCodePage: the tables of a code page, those of the US one if it is not
 * supported */
const LocalCodePage *LocalFindCodePage(DosCodePage page);
/* GetCurrentCodePage */
const LocalCodePage *LocalCurrentCodePage(void);

/* TextCommon */
#define LocalCharIs(ch, mask) \
    ((ch) < 256 && (localCharClassTable[(ch)] & (mask)) != 0)

#endif
