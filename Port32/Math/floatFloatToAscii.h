/*
 * floatFloatToAscii.h -- types/constants taken 1:1 from CInclude/math.h
 * (SBCS variant), so the frame layout stays source-compatible.
 */
#ifndef FLOATFLOATTOASCII_H
#define FLOATFLOATTOASCII_H

#include "fpstack.h"

#define DECIMAL_PRECISION   15
#define SIGN_STR_LEN        5
#define PAD_STR_LEN         8

#define MAX_DIGITS_FOR_NORMAL_NUMBERS           DECIMAL_PRECISION
#define MAX_CHARS_FOR_COMMAS_IN_NORMAL_NUMBERS  (DECIMAL_PRECISION / 3 + 1)
#define MAX_DIGITS_FOR_HUGE_NUMBERS             64
#define MAX_CHARS_FOR_COMMAS_IN_HUGE_NUMBERS    (MAX_DIGITS_FOR_HUGE_NUMBERS / 3 + 1)
#define MAX_CHARS_FOR_DECIMAL_POINT             1
#define MAX_CHARS_FOR_EXPONENT                  6
#define MAX_CHARS_FOR_PERCENATGE                1
#define MAX_CHARS_FOR_NULL_TERM                 1
#define MAX_CHARS_FOR_FORMAT_STUFF_IN_NORMAL_NUMBERS \
    (SIGN_STR_LEN * 2 + PAD_STR_LEN * 2 + MAX_CHARS_FOR_COMMAS_IN_NORMAL_NUMBERS + \
     MAX_CHARS_FOR_DECIMAL_POINT + MAX_CHARS_FOR_EXPONENT + MAX_CHARS_FOR_PERCENATGE)
#define MAX_CHARS_FOR_FORMAT_STUFF_IN_HUGE_NUMBERS \
    (SIGN_STR_LEN * 2 + PAD_STR_LEN * 2 + MAX_CHARS_FOR_COMMAS_IN_HUGE_NUMBERS + \
     MAX_CHARS_FOR_DECIMAL_POINT + MAX_CHARS_FOR_EXPONENT + MAX_CHARS_FOR_PERCENATGE)
#define MAX_CHARS_FOR_NORMAL_NUMBER \
    (MAX_DIGITS_FOR_NORMAL_NUMBERS + DECIMAL_PRECISION + \
     MAX_CHARS_FOR_FORMAT_STUFF_IN_NORMAL_NUMBERS + MAX_CHARS_FOR_NULL_TERM)
#define MAX_CHARS_FOR_HUGE_NUMBER \
    (MAX_DIGITS_FOR_HUGE_NUMBERS + MAX_CHARS_FOR_FORMAT_STUFF_IN_HUGE_NUMBERS + \
     MAX_CHARS_FOR_NULL_TERM)
#define FLOAT_TO_ASCII_NORMAL_BUF_LEN (((MAX_CHARS_FOR_NORMAL_NUMBER + 1) / 2) * 2)
#define FLOAT_TO_ASCII_HUGE_BUF_LEN   (((MAX_CHARS_FOR_HUGE_NUMBER + 1) / 2) * 2)

#define FFAF_FLOAT_RESERVED                 0x8000
#define FFAF_FROM_ADDR                      0x4000
#define FFAF_DONT_USE_SCIENTIFIC            0x0200
#define FFAF_SCIENTIFIC                     0x0100
#define FFAF_PERCENT                        0x0080
#define FFAF_USE_COMMAS                     0x0040
#define FFAF_NO_TRAIL_ZEROS                 0x0020
#define FFAF_NO_LEAD_ZERO                   0x0010
#define FFAF_HEADER_PRESENT                 0x0008
#define FFAF_TRAILER_PRESENT                0x0004
#define FFAF_SIGN_CHAR_TO_FOLLOW_HEADER     0x0002
#define FFAF_SIGN_CHAR_TO_PRECEDE_TRAILER   0x0001

typedef struct {
    word  formatFlags;
    byte  decimalOffset;
    byte  totalDigits;
    byte  decimalLimit;
    char  preNegative[SIGN_STR_LEN + 1];
    char  postNegative[SIGN_STR_LEN + 1];
    char  prePositive[SIGN_STR_LEN + 1];
    char  postPositive[SIGN_STR_LEN + 1];
    char  header[PAD_STR_LEN + 1];
    char  trailer[PAD_STR_LEN + 1];
    byte  FFTAP_unused;
} FloatFloatToAsciiParams;

typedef struct {
    FloatFloatToAsciiParams FFA_params;
    word  FFA_startNumber;      /* offsets are relative to the result buffer */
    word  FFA_decimalPoint;
    word  FFA_endNumber;
    word  FFA_numChars;
    word  FFA_startExponent;
    word  FFA_bufSize;
    word  FFA_saveDI;
    word  FFA_numSign;
    byte  FFA_startSigCount;
    byte  FFA_sigCount;
    byte  FFA_noMoreSigInfo;
    byte  FFA_startDecCount;
    byte  FFA_decCount;
    word  FFA_decExponent;
    word  FFA_curExponent;
    byte  FFA_useCommas;
    byte  FFA_charsToComma;
    char  FFA_commaChar;
    char  FFA_decimalChar;
} FloatFloatToAsciiData;

typedef union {
    FloatFloatToAsciiData FFA_float;
    /* FloatFloatToDateTimeData FFA_dateTime;  -- not part of the pilot */
} FFA_stackFrame;

/* number: used only with FFAF_FROM_ADDR, else the fp stack top is popped */
int FloatFloatToAscii(FFA_stackFrame *frame, char *resultString,
                      const FloatNum *number);
int FloatFloatToAscii_StdFormat(word formatFlags, byte totalDigits,
                                byte decimalLimit, char *resultString,
                                const FloatNum *number);
int FloatGetNumDigitsInIntegerPart(void);

#endif
