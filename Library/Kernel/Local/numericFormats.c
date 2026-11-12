/*
 * numericFormats.c -- the number and currency formats (after
 * numericFormats.asm, SBCS).  Each setting is a string of
 * localStrings.asm, a flag ('0' or '1'), a digit or a character; the
 * Set routines write them to geos.ini [localization], and LocalInit reads
 * them from there: the flags as booleans, the separators and the
 * currency symbol as data, the digits and the measurement system as
 * strings.
 */
#include "kernelC.h"
#include "localInt.h"

#include <initfile.h>
#include <string.h>

/* numericFormatKeys: the keys of the strings from symbolBeforeNumber on,
 * in the order of the chunks */
static const char *const numericFormatKeys[] = {
    "symbolBeforeNumber",		/* booleans */
    "spaceAroundSymbol",
    "useNegativeSign",
    "negativeSignBeforeNumber",
    "negativeSignBeforeSymbol",
    "currencyLeadingZero",
    "leadingZero",
    "thousandsSeparator",		/* data */
    "decimalSeparator",
    "listSeparator",
    "currencySymbol",
    "currencyDigits",			/* strings */
    "decimalDigits",
    "measurementSystem"
};
#define NUM_KEYS	(sizeof(numericFormatKeys) / sizeof(numericFormatKeys[0]))
#define LAST_BOOLEAN	(LS_leadingZero - LS_symbolBeforeNumber)
#define LAST_DATA	(LS_currencySymbol - LS_symbolBeforeNumber)

void LocalGetNumericFormat(LocalNumericFormat *buf)
{
    LocalLockStrings();
    buf->numberFormatFlags = LocalStringDigit(LS_leadingZero);
    buf->decimalDigits = LocalStringDigit(LS_decimalDigits);
    buf->thousandsSeparator = LocalStringChar(LS_thousandsSeparator);
    buf->decimalSeparator = LocalStringChar(LS_decimalSeparator);
    buf->listSeparator = LocalStringChar(LS_listSeparator);
    LocalUnlockStrings();
}

/* the string of one character */
static void SetChar(word id, int c)
{
    char s[2];

    s[0] = (char) c;
    s[1] = 0;
    LocalSetString(id, s);
}

/* NumericWriteFormats: the strings first..last to geos.ini */
static void WriteFormats(word first, word last)
{
    char buf[LOCAL_STRING_SIZE];
    word id, k;

    for (id = first; id <= last; id++) {
	k = (word) (id - LS_symbolBeforeNumber);
	LocalGetString(id, buf, sizeof(buf));
	if (k <= LAST_BOOLEAN)
	    InitFileWriteBoolean(localizationCategory, numericFormatKeys[k],
				 buf[0] == '1');
	else if (k <= LAST_DATA)
	    LocalWriteStringAsData(numericFormatKeys[k], id);
	else
	    InitFileWriteString(localizationCategory, numericFormatKeys[k],
				buf);
    }
}

void LocalSetNumericFormat(const LocalNumericFormat *buf)
{
    LocalLockStrings();
    SetChar(LS_leadingZero, '0' + buf->numberFormatFlags);
    SetChar(LS_decimalDigits, '0' + buf->decimalDigits);
    SetChar(LS_thousandsSeparator, buf->thousandsSeparator);
    SetChar(LS_decimalSeparator, buf->decimalSeparator);
    SetChar(LS_listSeparator, buf->listSeparator);
    LocalUnlockStrings();
    WriteFormats(LS_leadingZero, LS_listSeparator);
    WriteFormats(LS_decimalDigits, LS_decimalDigits);
}

/* the flags of CurrencyFormatFlags, from the highest bit down */
static const word currencyFlags[] = {
    LS_currencyLeadingZero,		/* CFF_LEADING_ZERO */
    LS_spaceAroundSymbol,		/* CFF_SPACE_AROUND_SYMBOL */
    LS_useNegativeSign,			/* CFF_USE_NEGATIVE_SIGN */
    LS_symbolBeforeNumber,		/* CFF_SYMBOL_BEFORE_NUMBER */
    LS_negativeSignBeforeNumber,	/* CFF_NEGATIVE_SIGN_BEFORE_NUMBER */
    LS_negativeSignBeforeSymbol		/* CFF_NEGATIVE_SIGN_BEFORE_SYMBOL */
};
#define NUM_CURRENCY_FLAGS (sizeof(currencyFlags) / sizeof(currencyFlags[0]))

/* symbol: CURRENCY_SYMBOL_LENGTH + 1 bytes */
void LocalGetCurrencyFormat(LocalCurrencyFormat *buf, char *symbol)
{
    LocalNumericFormat nf;
    byte flags = 0;
    unsigned i;

    LocalGetNumericFormat(&nf);
    LocalLockStrings();
    for (i = 0; i < NUM_CURRENCY_FLAGS; i++)	/* AddFlagToAL */
	flags = (byte) ((flags << 1) | (LocalStringDigit(currencyFlags[i]) & 1));
    buf->currencyFormatFlags = flags;
    buf->currencyDigits = LocalStringDigit(LS_currencyDigits);
    buf->thousandsSeparator = nf.thousandsSeparator;
    buf->decimalSeparator = nf.decimalSeparator;
    buf->listSeparator = nf.listSeparator;
    LocalGetString(LS_currencySymbol, symbol, CURRENCY_SYMBOL_LENGTH + 1);
    LocalUnlockStrings();
}

void LocalSetCurrencyFormat(const LocalCurrencyFormat *buf,
			    const char *symbol)
{
    char s[CURRENCY_SYMBOL_LENGTH + 1];
    byte flags = buf->currencyFormatFlags;
    unsigned i;

    LocalLockStrings();
    SetChar(LS_currencyDigits, '0' + buf->currencyDigits);
    for (i = NUM_CURRENCY_FLAGS; i-- > 0; flags >>= 1)	/* SetFlagFromAL */
	SetChar(currencyFlags[i], '0' + (flags & 1));
    strncpy(s, symbol, CURRENCY_SYMBOL_LENGTH);
    s[CURRENCY_SYMBOL_LENGTH] = 0;
    LocalSetString(LS_currencySymbol, s);
    LocalUnlockStrings();
    WriteFormats(LS_symbolBeforeNumber, LS_currencyLeadingZero);
    WriteFormats(LS_currencySymbol, LS_currencyDigits);
}

/* NumericInitFormats: what geos.ini has of them */
void LocalNumericInitFormats(void)
{
    char buf[CURRENCY_SYMBOL_LENGTH + 1];
    Boolean b;
    word k, n;

    for (k = NUM_KEYS; k-- > 0; ) {
	word id = (word) (LS_symbolBeforeNumber + k);

	if (k <= LAST_BOOLEAN) {
	    if (!InitFileReadBoolean(localizationCategory,
				     numericFormatKeys[k], &b))
		SetChar(id, b ? '1' : '0');
	} else if (k <= LAST_DATA)
	    (void) LocalReadStringAsData(numericFormatKeys[k], id,
					 CURRENCY_SYMBOL_LENGTH);
	else if (!InitFileReadStringBuffer(localizationCategory,
					   numericFormatKeys[k], buf,
					   sizeof(buf), &n))
	    LocalSetString(id, buf);
    }
}
