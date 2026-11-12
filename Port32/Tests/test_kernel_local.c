/*
 * test_kernel_local.c -- the localization code of the C kernel
 * (Library/Kernel/Local: character classes, case, string compare, DOS
 * code pages, quotes, number and currency formats and their settings in
 * geos.ini, numbers and distances as text; the tables and strings made at build time by
 * Port32/tools/localgen.py).
 */
#include "kernelC.h"
#include "ghal.h"
#include "hostfs.h"

#include <localize.h>
#include <char.h>
#include <initfile.h>
#include <stdio.h>
#include <string.h>

#undef LocalIsUpper
#undef LocalIsLower
#undef LocalIsAlpha
#undef LocalIsSpace
#undef LocalIsDigit
extern Boolean LocalIsUpper(wchar ch);
extern Boolean LocalIsLower(wchar ch);
extern Boolean LocalIsAlpha(wchar ch);
extern Boolean LocalIsSpace(wchar ch);
extern Boolean LocalIsDigit(wchar ch);

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static char top[260];

/* the test's top directory, with an empty geos.ini */
static void MakeTop(const char *argv0)
{
    char path[400], *slash;
    ghal_file *f;

    strncpy(top, argv0, sizeof(top) - 8);
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    strcpy(slash ? slash + 1 : top, "LOCTOP");
    sprintf(path, "%s%cgeos.ini", top, HostFSSep());
    ghal_fs_delete(path);
    ghal_fs_rmdir(top);
    CHECK(ghal_fs_mkdir(top) == GHAL_OK);
    CHECK(ghal_fs_open(path, GHAL_FS_WRITE | GHAL_FS_CREATE, &f) == GHAL_OK);
    ghal_fs_close(f);
    FileSetTopHostPath(top);
}

static void RemoveTop(void)
{
    char path[400];

    sprintf(path, "%s%cgeos.ini", top, HostFSSep());
    ghal_fs_delete(path);
    ghal_fs_rmdir(top);
}

static int Sign(sword v)
{
    return v < 0 ? -1 : v > 0;
}

static int Near(WWFixedAsDWord a, WWFixedAsDWord b)
{
    sdword d = (sdword) (a - b);

    return d > -4 && d < 4;
}

static void Numbers(void)
{
    char buf[LOCAL_DISTANCE_BUFFER_SIZE], *end;

    /* --- LocalFixedToAscii ------------------------------------------- */
    LocalFixedToAscii(buf, 0x18000, 3);
    CHECK(!strcmp(buf, "1.5"));
    LocalFixedToAscii(buf, 0x14000, 3);
    CHECK(!strcmp(buf, "1.25"));
    LocalFixedToAscii(buf, 0x5555, 3);
    CHECK(!strcmp(buf, "0.333"));
    LocalFixedToAscii(buf, 0xaaaa, 3);
    CHECK(!strcmp(buf, "0.667"));
    LocalFixedToAscii(buf, 0xffff, 3);		/* rounds up to 1 */
    CHECK(!strcmp(buf, "1"));
    LocalFixedToAscii(buf, (dword) 0 - 7, 3);	/* no "-0" */
    CHECK(!strcmp(buf, "0"));
    LocalFixedToAscii(buf, 0x10000, 3);
    CHECK(!strcmp(buf, "1"));
    LocalFixedToAscii(buf, 0xfffd8000, 3);
    CHECK(!strcmp(buf, "-2.5"));
    LocalFixedToAscii(buf, 0x28000, 0);
    CHECK(!strcmp(buf, "3"));
    LocalFixedToAscii(buf, 0x2a000, 1);	/* 2.625 */
    CHECK(!strcmp(buf, "2.6"));
    LocalFixedToAscii(buf, 0xfff98000, 0);	/* -6.5 */
    CHECK(!strcmp(buf, "-7"));

    /* --- LocalAsciiToFixed ------------------------------------------- */
    CHECK(LocalAsciiToFixed("1.5", &end) == 0x18000 && !*end);
    CHECK(LocalAsciiToFixed("-2.25x", &end) == 0xfffdc000 && *end == 'x');
    CHECK(LocalAsciiToFixed("0.25", NULL) == 0x4000);
    CHECK(LocalAsciiToFixed("0.99999", &end) == 0x10000 && !*end);
    CHECK(LocalAsciiToFixed("+3", NULL) == 0x30000);
    CHECK(LocalAsciiToFixed("99999", NULL) == 0x7fff0000);
    CHECK(Near(LocalAsciiToFixed("0.3333", NULL), 0x5555));

    /* --- distances --------------------------------------------------- */
    CHECK(LocalDistanceToAscii(buf, 72UL << 16, DU_INCHES, MEASURE_US, 0) == 5);
    CHECK(!strcmp(buf, "1 in"));
    LocalDistanceToAscii(buf, 72UL << 16, DU_INCHES, MEASURE_US,
			 LDF_FULL_NAMES | LDF_PRINT_PLURAL_IF_NEEDED);
    CHECK(!strcmp(buf, "1 inch"));
    LocalDistanceToAscii(buf, 144UL << 16, DU_INCHES_OR_CENTIMETERS,
			 MEASURE_US,
			 LDF_FULL_NAMES | LDF_PRINT_PLURAL_IF_NEEDED);
    CHECK(!strcmp(buf, "2 inches"));
    LocalDistanceToAscii(buf, 72UL << 16, DU_INCHES_OR_CENTIMETERS,
			 MEASURE_METRIC, 0);
    CHECK(!strcmp(buf, "2.54 cm"));
    LocalDistanceToAscii(buf, 72UL << 16, DU_CENTIMETERS, MEASURE_US,
			 LDF_PASSING_DECIMAL_PLACES | 1 |
			 LDF_OMIT_UNITS_STRING);
    CHECK(!strcmp(buf, "2.5"));
    LocalDistanceToAscii(buf, 0x18000, DU_POINTS, MEASURE_US, LDF_FULL_NAMES);
    CHECK(!strcmp(buf, "1.5 point"));
    CHECK(LocalDistanceFromAscii("1 in", DU_POINTS, MEASURE_US) ==
	  72UL << 16);
    CHECK(LocalDistanceFromAscii("1\"", DU_POINTS, MEASURE_US) ==
	  72UL << 16);
    /* 2.54 is 2.539993 in a WWFixed: 71.9998 points */
    CHECK(LocalDistanceFromAscii("2.54cm", DU_POINTS, MEASURE_US) ==
	  0x47fff4);
    CHECK(LocalDistanceFromAscii("12", DU_PICAS, MEASURE_US) == 144UL << 16);
    CHECK(LocalDistanceFromAscii("2", DU_INCHES_OR_CENTIMETERS, MEASURE_US) ==
	  144UL << 16);
    CHECK(LocalDistanceFromAscii("-100000 in", DU_POINTS, MEASURE_US) ==
	  (dword) (0 - (455UL * 72 << 16)));
    CHECK(LocalDistanceFromAscii("3 furlongs", DU_POINTS, MEASURE_US) ==
	  3UL << 16);
    CHECK(LocalGetMeasurementType() == MEASURE_US);
    CHECK(LocalIsNumChar('7') && LocalIsNumChar('-') && LocalIsNumChar('.'));
    CHECK(!LocalIsNumChar(',') && !LocalIsNumChar('a'));
}

static void Formats(void)
{
    LocalQuotes q;
    LocalNumericFormat nf;
    LocalCurrencyFormat cf;
    char sym[CURRENCY_SYMBOL_LENGTH + 1], data[16];
    word n;
    Boolean b;

    /* --- the defaults of localStrings.asm ---------------------------- */
    LocalGetQuotes(&q);
    CHECK(q.frontSingle == C_QUOTESNGLEFT && q.endSingle == C_QUOTESNGRIGHT &&
	  q.frontDouble == C_QUOTEDBLLEFT && q.endDouble == C_QUOTEDBLRIGHT);
    LocalGetNumericFormat(&nf);
    CHECK(nf.numberFormatFlags == NFF_LEADING_ZERO && nf.decimalDigits == 2);
    CHECK(nf.thousandsSeparator == ',' && nf.decimalSeparator == '.' &&
	  nf.listSeparator == ',');
    LocalGetCurrencyFormat(&cf, sym);
    CHECK(cf.currencyFormatFlags == (CFF_LEADING_ZERO |
				     CFF_SYMBOL_BEFORE_NUMBER |
				     CFF_NEGATIVE_SIGN_BEFORE_NUMBER |
				     CFF_NEGATIVE_SIGN_BEFORE_SYMBOL));
    CHECK(cf.currencyDigits == 2 && !strcmp(sym, "$"));
    CHECK(cf.decimalSeparator == '.');

    /* --- geos.ini at LocalInit --------------------------------------- */
    InitFileWriteBoolean("localization", "leadingZero", FALSE);
    InitFileWriteBoolean("localization", "spaceAroundSymbol", TRUE);
    InitFileWriteBoolean("localization", "symbolBeforeNumber", FALSE);
    InitFileWriteData("localization", "decimalSeparator", ",", 1);
    InitFileWriteData("localization", "thousandsSeparator", ".", 1);
    InitFileWriteData("localization", "currencySymbol", "DM", 2);
    InitFileWriteString("localization", "decimalDigits", "3");
    InitFileWriteData("localization", "quotes", "\x84\x93\x84\x93", 4);
    LocalInit();
    LocalGetNumericFormat(&nf);
    CHECK(nf.numberFormatFlags == 0 && nf.decimalDigits == 3);
    CHECK(nf.decimalSeparator == ',' && nf.thousandsSeparator == '.');
    LocalGetCurrencyFormat(&cf, sym);
    CHECK(cf.currencyFormatFlags == (CFF_LEADING_ZERO |
				     CFF_SPACE_AROUND_SYMBOL |
				     CFF_NEGATIVE_SIGN_BEFORE_NUMBER |
				     CFF_NEGATIVE_SIGN_BEFORE_SYMBOL));
    CHECK(!strcmp(sym, "DM") && cf.decimalSeparator == ',');
    LocalGetQuotes(&q);
    CHECK(q.frontSingle == 0x84 && q.endDouble == 0x93);

    /* --- the Set routines write geos.ini ----------------------------- */
    q.frontSingle = '<';
    q.endSingle = '>';
    q.frontDouble = '[';
    q.endDouble = ']';
    LocalSetQuotes(&q);
    LocalGetQuotes(&q);
    CHECK(q.frontSingle == '<' && q.endDouble == ']');
    CHECK(!InitFileReadDataBuffer("localization", "quotes", data,
				  sizeof(data), &n));
    CHECK(n == 4 && !memcmp(data, "<>[]", 4));
    nf.numberFormatFlags = NFF_LEADING_ZERO;
    nf.decimalDigits = 1;
    nf.thousandsSeparator = '\'';
    nf.decimalSeparator = '.';
    nf.listSeparator = ';';
    LocalSetNumericFormat(&nf);
    memset(&nf, 0, sizeof(nf));
    LocalGetNumericFormat(&nf);
    CHECK(nf.numberFormatFlags == NFF_LEADING_ZERO && nf.decimalDigits == 1 &&
	  nf.thousandsSeparator == '\'' && nf.listSeparator == ';');
    CHECK(!InitFileReadBoolean("localization", "leadingZero", &b) && b);
    CHECK(!InitFileReadStringBuffer("localization", "decimalDigits", data,
				    sizeof(data), &n) && !strcmp(data, "1"));
    CHECK(!InitFileReadDataBuffer("localization", "listSeparator", data,
				  sizeof(data), &n) && n == 1 && data[0] == ';');
    cf.currencyFormatFlags = CFF_USE_NEGATIVE_SIGN | CFF_SYMBOL_BEFORE_NUMBER;
    cf.currencyDigits = 0;
    LocalSetCurrencyFormat(&cf, "EUR-LONG-SYMBOL");
    LocalGetCurrencyFormat(&cf, sym);
    CHECK(cf.currencyFormatFlags ==
	  (CFF_USE_NEGATIVE_SIGN | CFF_SYMBOL_BEFORE_NUMBER));
    CHECK(cf.currencyDigits == 0 && strlen(sym) == CURRENCY_SYMBOL_LENGTH &&
	  !strncmp(sym, "EUR-LONG-", CURRENCY_SYMBOL_LENGTH));
    CHECK(!InitFileReadBoolean("localization", "useNegativeSign", &b) && b);
    CHECK(!InitFileReadBoolean("localization", "currencyLeadingZero", &b) &&
	  !b);
    CHECK(!InitFileReadStringBuffer("localization", "currencyDigits", data,
				    sizeof(data), &n) && !strcmp(data, "0"));
}

int main(int argc, char **argv)
{
    char buf[64];
    int c;
    word n;

    (void) argc;
    kernelCurrentGeode = 0x2340;
    MakeTop(argv[0]);
    KernelThreadInit(PRIORITY_STANDARD);

    /* --- character classes ------------------------------------------ */
    CHECK(LocalIsAlpha('a') && LocalIsAlpha(C_UA_DIERESIS));
    CHECK(!LocalIsAlpha('1') && !LocalIsAlpha(' '));
    CHECK(LocalIsDigit('5') && !LocalIsDigit('a'));
    CHECK(LocalIsHexDigit('f') && LocalIsHexDigit('F') && !LocalIsHexDigit('g'));
    CHECK(LocalIsSpace(' ') && LocalIsSpace('\t') && !LocalIsSpace('x'));
    CHECK(LocalIsPunctuation('!') && !LocalIsPunctuation('a'));
    CHECK(LocalIsUpper('A') && LocalIsUpper(C_UA_DIERESIS) && !LocalIsUpper('a'));
    CHECK(LocalIsLower('a') && LocalIsLower(C_LA_DIERESIS) && !LocalIsLower('A'));
    CHECK(LocalIsControl(1) && !LocalIsControl('a'));
    CHECK(LocalIsAlphaNumeric('7') && LocalIsAlphaNumeric('q') &&
	  !LocalIsAlphaNumeric('.'));
    CHECK(LocalIsPrintable('a') && !LocalIsPrintable(1));
    CHECK(LocalIsGraphic('a') && !LocalIsGraphic(1));
    CHECK(!LocalIsAlpha(0x1234));
    /* the classes agree with the host's for ASCII */
    for (c = 0x20; c < 0x7f; c++) {
	CHECK(!LocalIsDigit(c) == !(c >= '0' && c <= '9'));
	CHECK(!LocalIsUpper(c) == !(c >= 'A' && c <= 'Z'));
	CHECK(!LocalIsLower(c) == !(c >= 'a' && c <= 'z'));
    }

    /* --- case -------------------------------------------------------- */
    CHECK(LocalUpcaseChar('a') == 'A' && LocalUpcaseChar('A') == 'A');
    CHECK(LocalUpcaseChar(C_LA_DIERESIS) == C_UA_DIERESIS);
    CHECK(LocalDowncaseChar('Z') == 'z' && LocalDowncaseChar('1') == '1');
    CHECK(LocalDowncaseChar(C_UA_DIERESIS) == C_LA_DIERESIS);
    strcpy(buf, "hello \x8a world");
    LocalUpcaseString(buf, 0);
    CHECK(strcmp(buf, "HELLO \x80 WORLD") == 0);
    LocalDowncaseString(buf, 3);
    CHECK(strcmp(buf, "helLO \x80 WORLD") == 0);

    /* --- size -------------------------------------------------------- */
    CHECK(LocalStringLength("abc") == 3 && LocalStringSize("") == 0);

    /* --- compare ----------------------------------------------------- */
    CHECK(LocalCmpStrings("apple", "banana", 0) < 0);
    CHECK(LocalCmpStrings("banana", "apple", 0) > 0);
    CHECK(LocalCmpStrings("apple", "apple", 0) == 0);
    CHECK(LocalCmpStringsNoCase("apple", "APPLE", 0) == 0);
    CHECK(LocalCmpStrings("apple", "APPLE", 0) != 0);
    CHECK(Sign(LocalCmpStrings("apple", "APPLE", 0)) ==
	  -Sign(LocalCmpStrings("APPLE", "apple", 0)));
    /* accents count only after the letters */
    CHECK(LocalCmpStringsNoCase("\x80pfel", "Apfel", 0) == 0);
    CHECK(LocalCmpStrings("\x80pfel", "Apfel", 0) != 0);
    CHECK(LocalCmpStrings("\x80pfel", "Apfelz", 0) < 0);
    CHECK(LocalCmpStrings("abcX", "abcY", 3) == 0);
    CHECK(LocalCmpStrings("abc", "abcd", 0) < 0);
    CHECK(LocalCmpStringsNoSpace("o'leary", "oleary", 0) == 0);
    CHECK(LocalCmpStringsNoSpace("o'leary", "o'", 2) == 0);
    CHECK(LocalCmpStringsNoSpace("a b c", "abd", 0) < 0);
    CHECK(LocalCmpStringsNoSpaceCase("O Leary", "oleary", 0) == 0);
    CHECK(LocalCmpStringsNoSpace("O Leary", "oleary", 0) != 0);
    CHECK(LocalCmpChars('a', 'b') < 0 && LocalCmpChars('b', 'a') > 0);
    CHECK(LocalCmpCharsNoCase('a', 'A') == 0 && LocalCmpChars('a', 'A') != 0);
    CHECK(LocalLexicalValueNoCase('a') == LocalLexicalValueNoCase('A'));
    CHECK(LocalLexicalValue('a') != LocalLexicalValue('A'));
    CHECK(LocalGetLanguage() == SL_ENGLISH);

    /* --- code pages -------------------------------------------------- */
    CHECK(LocalGetCodePage() == CODE_PAGE_US);
    CHECK(LocalIsCodePageSupported(CODE_PAGE_PORTUGUESE));
    CHECK(!LocalIsCodePageSupported(12345));
    strcpy(buf, "\x8e\x84 x");		/* CP437: A and a with dieresis */
    CHECK(!LocalDosToGeos(buf, 0, '_'));
    CHECK(strcmp(buf, "\x80\x8a x") == 0);
    CHECK(!LocalGeosToDos(buf, 0, '_'));
    CHECK(strcmp(buf, "\x8e\x84 x") == 0);
    strcpy(buf, "a\xc2z");		/* not in CP437 */
    CHECK(LocalGeosToDos(buf, 0, '_'));
    CHECK(strcmp(buf, "a_z") == 0);
    strcpy(buf, "\xc2\xc2");
    CHECK(LocalGeosToDos(buf, 1, '_') && strcmp(buf, "_\xc2") == 0);
    CHECK(!LocalIsDosChar(0xc2) && LocalIsDosChar(C_UA_DIERESIS) &&
	  LocalIsDosChar('a'));
    CHECK(LocalDosToGeosChar(0x8e, '_') == C_UA_DIERESIS);
    CHECK(LocalGeosToDosChar(C_UA_DIERESIS, '_') == 0x8e);
    CHECK(LocalGeosToDosChar(0xc2, '_') == '_');
    CHECK(LocalGeosToDosChar('q', '_') == 'q');
    CHECK(LocalCodePageToGeosChar(0x8e, CODE_PAGE_MULTILINGUAL, '_') ==
	  C_UA_DIERESIS);
    CHECK(LocalCmpStringsDosToGeos("\x8epfel", "\x80pfel", 0, '_',
				   LCSDTG_NO_CONVERT_STRING_2) == 0);
    CHECK(LocalCmpStringsDosToGeos("\x8epfel", "\x80pfel", 0, '_', 0) != 0);
    CHECK(LocalSetCodePage(12345) == 0 && LocalGetCodePage() == CODE_PAGE_US);
    CHECK(LocalSetCodePage(CODE_PAGE_MULTILINGUAL) != 0);
    CHECK(LocalGetCodePage() == CODE_PAGE_MULTILINGUAL);
    strcpy(buf, "\xb7");		/* CP850: A with grave */
    CHECK(!LocalDosToGeos(buf, 0, '_') && (byte) buf[0] == C_UA_GRAVE);
    CHECK(LocalIsDosChar(C_UA_GRAVE));
    CHECK(LocalSetCodePage(CODE_PAGE_US) != 0);
    CHECK(!LocalIsDosChar(C_UA_GRAVE));

    Numbers();
    Formats();
    LocalSetMeasurementType(MEASURE_METRIC);
    CHECK(LocalGetMeasurementType() == MEASURE_METRIC);
    CHECK(!InitFileReadStringBuffer("localization", "measurementSystem", buf,
				    sizeof(buf), &n) && !strcmp(buf, "1"));
    InitFileReset();
    RemoveTop();

    if (fails) {
	printf("%d failures\n", fails);
	return 1;
    }
    printf("kernel.local ok\n");
    return 0;
}
