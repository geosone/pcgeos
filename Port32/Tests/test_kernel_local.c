/*
 * test_kernel_local.c -- the localization code of the C kernel
 * (Library/Kernel/Local: character classes, case, string compare, DOS
 * code pages; the tables made at build time by Port32/tools/localgen.py).
 */
#include "kernelC.h"

#include <localize.h>
#include <char.h>
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

static int Sign(sword v)
{
    return v < 0 ? -1 : v > 0;
}

int main(void)
{
    char buf[64];
    int c;

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

    if (fails) {
	printf("%d failures\n", fails);
	return 1;
    }
    printf("kernel.local ok\n");
    return 0;
}
