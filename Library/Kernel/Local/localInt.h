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

/*
 * The strings of localStrings.asm (localization), LS_* of the generated
 * localStrings.h: a copy of each that the Set routines and the init file
 * change (the chunks of the LocalStrings resource).  LockStringsDS and
 * UnlockStrings guard them.
 */
#include "localStrings.h"
#define LOCAL_STRING_SIZE	DATE_TIME_FORMAT_SIZE
void LocalLockStrings(void);
void LocalUnlockStrings(void);
/* the string; the strings locked */
char *LocalString(word id);
/* StoreResourceString: a copy of the string (size: of the buffer) */
void LocalGetString(word id, char *buf, word size);
/* SetResourceString, cut to LOCAL_STRING_SIZE */
void LocalSetString(word id, const char *str);
/* the first character of the string, and the digit or flag ('0'..'9') */
byte LocalStringChar(word id);
#define LocalStringDigit(id)	((byte) (LocalStringChar(id) - '0'))

/* the category of the localization settings in geos.ini */
extern const char localizationCategory[];
/* LocalGetStringAsData: TRUE if something was read */
Boolean LocalReadStringAsData(const char *key, word id, word size);
/* LocalWriteStringAsData: the string without its null */
void LocalWriteStringAsData(const char *key, word id);

/* the init routines of LocalInit */
void LocalNumericInitFormats(void);
void LocalInitQuotes(void);
void LocalDateTimeInitFormats(void);
void LocalInitTimezone(void);

/*
 * Dates and times (dateTimeFields.c, dateTimeParse.c): the fields as the
 * asm had them in registers, a byte each but the year; 0xff (0xffff)
 * for a field that a parsed format did not have.
 */
typedef struct {
    word	LDT_year;
    byte	LDT_month;	/* 1-12 */
    byte	LDT_day;	/* 1-31 */
    byte	LDT_weekday;	/* 0-6 */
    byte	LDT_hours;	/* 0-23 */
    byte	LDT_minutes;
    byte	LDT_seconds;
} LocalDateTime;

#define TOKEN_DELIMITER	'|'
/* the tokens of a format string ("|LW|" ...), in the order of the asm's
 * tokenTable; -1 for none */
int LocalFindToken(char c1, char c2);
#define LOCAL_TOKEN_TOKEN_DELIMITER	0	/* "DD", the '|' itself */
/* DateTimeFieldFormat: the length without the null */
word LocalDateTimeFieldFormat(char *str, const char *format,
			      const LocalDateTime *dt);
/* IsLegalFormatChar: whether the character is one of the format's own
 * (not of its tokens) */
Boolean LocalIsLegalFormatChar(wchar ch, DateTimeFormat format);
/* CheckCharGroup: whether the character is in the string (the null
 * too) */
Boolean LocalInCharGroup(wchar ch, word id);

#endif
