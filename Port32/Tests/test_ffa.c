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

static void checkRaw(const char *name, word w3, word exp, word flags,
                     int digits, int decimals, const char *expect)
{
    char buf[FLOAT_TO_ASCII_HUGE_BUF_LEN];
    FloatNum n;
    memset(&n, 0, sizeof n);
    n.F_mantissa_wd3 = w3;
    n.F_exponent = exp;
    FpReset();
    FloatPushNumber(&n);
    int r = FloatFloatToAscii_StdFormat(flags, (byte)digits, (byte)decimals,
                                        buf, NULL);
    total++;
    int ok = strcmp(buf, expect) == 0 && FpDepth() == 0;
    if (!ok)
        fails++;
    if (!ok || verbose)
        printf("%s %-28s %04x:%04x -> \"%s\" (n=%d)%s%s%s\n", ok ? "ok  " : "FAIL",
               name, exp, w3, buf, r, ok ? "" : "  expected \"", ok ? "" : expect,
               ok ? "" : "\"");
}

int main(int argc, char **argv)
{
    verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    const word COM = FFAF_USE_COMMAS;

#define FFA_CASE(name, x, flags, digits, dec, expect) \
    check(name, x, flags, digits, dec, expect);
#define FFA_RAW(name, w3, exp, flags, digits, dec, expect) \
    checkRaw(name, w3, exp, flags, digits, dec, expect);
#include "ffa_cases.h"

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
