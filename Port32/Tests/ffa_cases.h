/*
 * ffa_cases.h -- test cases for FloatFloatToAscii_StdFormat, shared by the
 * C port test (test_ffa.c) and the reference app that runs the 16-bit
 * original under GEOS (GeosRef/ffaref.goc).  No include guard: the file
 * is an X-macro list, included once per expansion.
 *
 *   FFA_CASE(name, value, flags, totalDigits, decimalLimit, expect)
 *       value is a C double constant
 *   FFA_RAW(name, mantissa_wd3, exponent, flags, totalDigits, decimalLimit, expect)
 *       a GEOS-80 number with mantissa words 2..0 zero (infinity, NaN)
 *
 * The flag names are the FFAF_* record fields of math.def.  The expected
 * strings are the output of the original (see GeosRef/README.md).
 */
#define SCI FFAF_SCIENTIFIC
#define COM FFAF_USE_COMMAS
#define NTZ FFAF_NO_TRAIL_ZEROS
#define NLZ FFAF_NO_LEAD_ZERO
#define PCT FFAF_PERCENT
#define DUS FFAF_DONT_USE_SCIENTIFIC

/* --- fixed format, fast path --- */
FFA_CASE("int",              1234,         0,   15, 0,  "1234")
FFA_CASE("int+dec",          1234,         0,   15, 2,  "1234.00")
FFA_CASE("round 2",          3.14159,      0,   15, 2,  "3.14")
FFA_CASE("round away 0",     0.56,         0,   15, 0,  "1")
FFA_CASE("round away 0 neg", -0.56,        0,   15, 0,  "-1")
FFA_CASE("round half",       2.5,          0,   15, 0,  "3")
FFA_CASE("345.678 dec2",     345.678,      0,   15, 2,  "345.68")
FFA_CASE("zero",             0,            0,   15, 2,  "0.00")
FFA_CASE("zero no dec",      0,            0,   15, 0,  "0")
FFA_CASE("small",            0.05,         0,   15, 2,  "0.05")
FFA_CASE("neg",              -42.5,        0,   15, 1,  "-42.5")
FFA_CASE("commas",           1234567.891,  COM, 15, 2,  "1,234,567.89")
FFA_CASE("commas 3",         123456,       COM, 15, 0,  "123,456")
FFA_CASE("no trail 0",       2.5,          NTZ, 15, 5,  "2.5")
FFA_CASE("no trail 0 int",   2.0,          NTZ, 15, 5,  "2")
FFA_CASE("no lead 0",        0.25,         NLZ, 15, 2,  ".25")
FFA_CASE("percent",          0.125,        PCT, 15, 1,  "12.5%")
/* QUIRK: rounds at decimalLimit, then truncates to totalDigits */
FFA_CASE("totalDigits cut",  123.456,      0,   5,  4,  "123.45")
FFA_CASE("1e9",              1e9,          0,   15, 0,  "1000000000")
FFA_CASE("dec 10",           0.1234567891, 0,   15, 10, "0.1234567891")

/* --- fixed format, general path (> 32 bit integer) --- */
FFA_CASE("big int",          12345678901234.0, 0,   15, 0, "12345678901234")
FFA_CASE("big commas",       12345678901234.0, COM, 15, 0, "12,345,678,901,234")
FFA_CASE("big int+dec",      1234567890123.5,  0,   15, 2, "1234567890123.50")
FFA_CASE("too big -> sci",   1e20,         0,   15, 2,  "1.00E+20")
FFA_CASE("dec > 10",         0.5,          0,   15, 12, "0.500000000000")

/* --- scientific --- */
FFA_CASE("sci 1",            12345.678,    SCI, 15, 3,  "1.235E+04")
FFA_CASE("sci neg exp",      0.000123,     SCI, 15, 2,  "1.23E-04")
FFA_CASE("sci neg num",      -6.02e23,     SCI, 15, 2,  "-6.02E+23")
FFA_CASE("sci 1e100",        1e100,        SCI, 15, 1,  "1.0E+100")
FFA_CASE("sci 1e-300",       1e-300,       SCI, 15, 1,  "1.0E-300")
FFA_CASE("sci zero",         0,            SCI, 15, 2,  "0.00E+00")
FFA_CASE("sci ntz",          1.5e10,       SCI|NTZ, 15, 6, "1.5E+10")
FFA_CASE("sci round up",     9.999,        SCI, 15, 2,  "1.00E+01")

/* --- don't use scientific (huge) --- */
FFA_CASE("huge fixed",       1e20,         DUS, 64, 0,  "100000000000000000000")

/* --- infinity / NaN (GEOS-80 patterns) --- */
FFA_RAW("+inf",              0x8000, 0x7fff, 0, 15, 2, "overflow")
FFA_RAW("-inf",              0x8000, 0xffff, 0, 15, 2, "underflow")
FFA_RAW("nan",               0xc000, 0x7fff, 0, 15, 2, "error")

#undef SCI
#undef COM
#undef NTZ
#undef NLZ
#undef PCT
#undef DUS
#undef FFA_CASE
#undef FFA_RAW
