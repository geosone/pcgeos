/*
 * test_ffa.c -- tests for the C port of FloatFloatToAscii.
 *
 * Expected strings come from the routine's documented behaviour
 * (floatFloatToAscii.asm / math.def headers).  They are NOT yet reference
 * output of the 16-bit original -- see README.md "Next steps".
 *
 * Run with -v to print every case (useful to generate a vector file).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "floatFloatToAscii.h"

static int fails, total, verbose;

static void check(const char *name, double x, word flags, int digits,
                  int decimals, const char *expect)
{
    char buf[FLOAT_TO_ASCII_HUGE_BUF_LEN];
    memset(buf, 'X', sizeof buf);
    FpReset();
    FloatPushValue(x);
    int n = FloatFloatToAscii_StdFormat(flags, (byte)digits, (byte)decimals,
                                        buf, NULL);
    total++;
    int ok = strcmp(buf, expect) == 0 && FpDepth() == 0 &&
             (n == 0 || n == (int)strlen(buf));
    if (!ok)
        fails++;
    if (!ok || verbose)
        printf("%s %-28s %-24.17g flags=%04x d=%2d dec=%2d -> \"%s\" (n=%d, depth=%d)%s%s%s\n",
               ok ? "ok  " : "FAIL", name, x, flags, digits, decimals, buf, n,
               FpDepth(), ok ? "" : "  expected \"", ok ? "" : expect,
               ok ? "" : "\"");
}

static void checkFrame(const char *name, double x, FFA_stackFrame *f,
                       const char *expect)
{
    char buf[FLOAT_TO_ASCII_HUGE_BUF_LEN];
    FpReset();
    FloatPushValue(x);
    int n = FloatFloatToAscii(f, buf, NULL);
    total++;
    int ok = strcmp(buf, expect) == 0 && FpDepth() == 0;
    if (!ok)
        fails++;
    if (!ok || verbose)
        printf("%s %-28s %-24.17g -> \"%s\" (n=%d)%s%s%s\n", ok ? "ok  " : "FAIL",
               name, x, buf, n, ok ? "" : "  expected \"", ok ? "" : expect,
               ok ? "" : "\"");
}

int main(int argc, char **argv)
{
    verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    const word SCI = FFAF_SCIENTIFIC, COM = FFAF_USE_COMMAS,
               NTZ = FFAF_NO_TRAIL_ZEROS, NLZ = FFAF_NO_LEAD_ZERO,
               PCT = FFAF_PERCENT, DUS = FFAF_DONT_USE_SCIENTIFIC;

    /* --- fixed format, fast path --- */
    check("int",            1234,        0,   15, 0,  "1234");
    check("int+dec",        1234,        0,   15, 2,  "1234.00");
    check("round 2",        3.14159,    0,   15, 2,  "3.14");
    check("round away 0",   0.56,       0,   15, 0,  "1");
    check("round away 0 neg", -0.56,    0,   15, 0,  "-1");
    check("round half",     2.5,        0,   15, 0,  "3");
    check("345.678 dec2",   345.678,    0,   15, 2,  "345.68");
    check("zero",           0,           0,   15, 2,  "0.00");
    check("zero no dec",    0,           0,   15, 0,  "0");
    check("small",          0.05,       0,   15, 2,  "0.05");
    check("neg",            -42.5,      0,   15, 1,  "-42.5");
    check("commas",         1234567.891, COM, 15, 2, "1,234,567.89");
    check("commas 3",       123456,      COM, 15, 0,  "123,456");
    check("no trail 0",     2.5,        NTZ, 15, 5,  "2.5");
    check("no trail 0 int", 2.0,        NTZ, 15, 5,  "2");
    check("no lead 0",      0.25,       NLZ, 15, 2,  ".25");
    check("percent",        0.125,      PCT, 15, 1,  "12.5%");
    /* QUIRK (documented): rounds at decimalLimit, then truncates to totalDigits */
    check("totalDigits cut",123.456,    0,   5,  4,  "123.45");
    check("1e9",            1e9L,        0,   15, 0,  "1000000000");
    check("dec 10",         0.1234567891, 0, 15, 10, "0.1234567891");

    /* --- fixed format, general path (> 32 bit integer) --- */
    check("big int",        12345678901234.0, 0,  15, 0, "12345678901234");
    check("big commas",     12345678901234.0, COM, 15, 0, "12,345,678,901,234");
    check("big int+dec",    1234567890123.5,  0,  15, 2, "1234567890123.50");
    check("too big -> sci", 1e20L,       0,   15, 2,  "1.00E+20");
    check("dec > 10",       0.5,        0,   15, 12, "0.500000000000");

    /* --- scientific --- */
    check("sci 1",          12345.678,  SCI, 15, 3,  "1.235E+04");
    check("sci neg exp",    0.000123,   SCI, 15, 2,  "1.23E-04");
    check("sci neg num",    -6.02e23,   SCI, 15, 2,  "-6.02E+23");
    check("sci 1e100",      1e100L,      SCI, 15, 1,  "1.0E+100");
    check("sci 1e-300",     1e-300L,     SCI, 15, 1,  "1.0E-300");
    check("sci zero",       0,           SCI, 15, 2,  "0.00E+00");
    check("sci ntz",        1.5e10,     SCI|NTZ, 15, 6, "1.5E+10");
    check("sci round up",   9.999,      SCI, 15, 2,  "1.00E+01");

    /* --- don't use scientific (huge) --- */
    check("huge fixed",     1e20L,       DUS, 64, 0,  "100000000000000000000");

    /* --- NaN / Inf --- */
    check("+inf",           INFINITY,    0,   15, 2,  "overflow");
    check("-inf",           -INFINITY,   0,   15, 2,  "underflow");
    check("nan",            NAN,         0,   15, 2,  "error");

    /* --- full frame: header, trailer, sign strings, decimal offset --- */
    {
        FFA_stackFrame f;
        memset(&f, 0, sizeof f);
        f.FFA_float.FFA_params.formatFlags = FFAF_HEADER_PRESENT | COM |
                                             FFAF_SIGN_CHAR_TO_FOLLOW_HEADER;
        f.FFA_float.FFA_params.totalDigits = 15;
        f.FFA_float.FFA_params.decimalLimit = 2;
        strcpy(f.FFA_float.FFA_params.header, "$");
        strcpy(f.FFA_float.FFA_params.preNegative, "(");
        strcpy(f.FFA_float.FFA_params.postNegative, ")");
        checkFrame("currency neg", -1234.5, &f, "$(1,234.50)");
        memset(&f, 0, sizeof f);
        f.FFA_float.FFA_params.formatFlags = FFAF_HEADER_PRESENT | COM;
        f.FFA_float.FFA_params.totalDigits = 15;
        f.FFA_float.FFA_params.decimalLimit = 2;
        strcpy(f.FFA_float.FFA_params.header, "$");
        strcpy(f.FFA_float.FFA_params.preNegative, "-");
        checkFrame("sign before header", -5, &f, "-$5.00");
        memset(&f, 0, sizeof f);
        f.FFA_float.FFA_params.formatFlags = FFAF_TRAILER_PRESENT;
        f.FFA_float.FFA_params.totalDigits = 15;
        f.FFA_float.FFA_params.decimalLimit = 1;
        f.FFA_float.FFA_params.decimalOffset = (byte)-6;
        strcpy(f.FFA_float.FFA_params.trailer, " Mio");
        checkFrame("millions", 2500000, &f, "2.5 Mio");
    }

    /* --- FROM_ADDR with GEOS-80 bit pattern (10^10 from the asm table) --- */
    {
        FloatNum n = {0, 0, 0xf900, 0x9502, 0x4020};
        char buf[FLOAT_TO_ASCII_NORMAL_BUF_LEN];
        FpReset();
        FloatFloatToAscii_StdFormat(FFAF_FROM_ADDR | COM, 15, 0, buf, &n);
        total++;
        if (strcmp(buf, "10,000,000,000") || FpDepth()) {
            fails++;
            printf("FAIL from addr -> \"%s\"\n", buf);
        } else if (verbose)
            printf("ok   from addr 10^10 -> \"%s\"\n", buf);
    }

    printf("%d/%d passed\n", total - fails, total);
    return fails != 0;
}
