# Pilot: `floatFloatToAscii.asm` → C (32/64-bit)

A measuring point for how much effort translating PC/GEOS assembler to C
takes. Translated: `Library/Math/Float/floatFloatToAscii.asm` (number → text
formatting, used by GeoCalc, Chart, Parse and others).

The C files live next to the asm in `Library/Math/Float/` (`floatFloatToAscii.c/.h`, `fpstack.c/.h`); the test is `Port32/Tests/test_ffa.c`. Built with CMake from the repository root (out-of-tree build directory):

```sh
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build
cmake -S . -B build32 -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-linux-i686.cmake
cmake --build build32 && ctest --test-dir build32
build/Port32/Tests/test_ffa -v   # all test cases with output
```

## Files

| File | Content |
|---|---|
| `floatFloatToAscii.c` | translation of all 21 routines (incl. `FloatFloatToAsciiFixed`, `ConvertDwordToAscii`, `FloatGetNumDigitsInIntegerPart`) |
| `floatFloatToAscii.h` | `FFA_stackFrame` and constants 1:1 from `CInclude/math.h` |
| `fpstack.c/.h` | shim for the GEOS float stack (~30 routines from `floatLow/High/Fixed/Comp.asm`) |
| `test_ffa.c` | 41 test cases |

## Result

| | Assembler | C |
|---|---|---|
| Total lines | 2,252 | 603 (+309 shim) |
| Code without comments | ~870 | ~470 (+240 shim) |

- 41/41 tests pass on x86-64 **and** i386.
- Code ratio: ~1.9 : 1 (total lines ~3.7 : 1; the asm files are comment-heavy).
- Effort: most of it was **not** translating the file itself but clarifying
  the semantics of the helper routines it calls (rounding in
  `FloatFloatToDwordNoPop`, `FloatEpsilon`, the `FloatTrunc` hack, stack
  effects of `FloatEq0`, register/carry conventions).

## Key findings

- **GEOS-80 = x87 extended** (bias 0x3fff, explicit integer bit, Inf =
  `7fff:8000…0`). The constant table from `Float10ToTheX` is used directly
  as `long double` via `memcpy`.
  → The software float emulator (~15,000 lines of asm) can be mapped to
  x87/`long double` or SoftFloat `extF80`.
  *Caution:* Open Watcom only has a 64-bit `long double`, and so does armhf;
  on aarch64 `long double` is a 128-bit quad. The port therefore moves to
  `double` (see `GHAL-Concept.md` §7).
- **Structure preserved**: functions keep their names, order and the
  Forth-like stack flow, so the code can be reviewed line by line against
  the original.
- **Quirks deliberately preserved (marked QUIRK in the code)**:
  - rounding to `decimalLimit`, then truncation to `totalDigits`
    (`123.456`, 5 digits, 4 decimals → `123.45`), as documented.
  - the fixed → scientific fallback overwrites **all** format flags
    (commas/percent sign are lost).
  - with `FFAF_DONT_USE_SCIENTIFIC` a negative exponent remains.
  - `+inf` → `"overflow"`, `-inf` → `"underflow"`.
  - `FloatFloatToDword` checks the exponent *before* rounding.

## Open points

1. **ABI decision**: `FFA_startNumber`, `FFA_decimalPoint` etc. used to be
   absolute segment offsets (`di`). Now: offsets relative to the buffer.
   `FFA_decimalPoint == 0` still means "no decimal point", but with
   `FFAF_NO_LEAD_ZERO` (".25") the point sits at offset 0 → ambiguous.
   For the real port: check the callers (GeoCalc, Spreadsheet) and switch to
   pointers or offset+1.
2. **No reference comparison yet**: the expected strings come from the
   documentation, not from the 16-bit original. Next step: run the 16-bit
   version (e.g. `Appl/SDK_C/FP2Asc`) with the same test vectors and compare
   bit for bit.
3. `FloatLog` uses `log10l` instead of the GEOS series expansion; it only
   serves the exponent *estimate* (an error of ±1.6 is absorbed by the
   algorithm) but could in theory branch differently at exact powers of ten.
4. Not included: the DBCS variant (mechanical: `char` → `wchar`), the
   date/time branch (`FloatFloatToDateTime`, separate file).

## GHAL

`test_ghal_core.c` (ctest `ghal.core`) runs on every host and uses only
`ghal.h`: memory blocks (plain and paged), clocks (resolution, never
backwards), `ghal_crit`/`ghal_hostlock` nesting, the `GHAL_IRQ_TICK` source
(rate, held off inside `ghal_crit`, detach) and the capability query.

`test_ghal_posix.c` (ctest `ghal.posix`, linux only) covers what needs
fork, threads and signals: enforced read/execute protection, `ghal_crit`
against another thread, and `ghal_fault` (access, divide, breakpoint; the
test catches every fault, so no real crash happens).

For dos32 the tests are DOS programs; ctest starts them headless in
DOSBox-X through `Port32/tools/dosrun.sh`:

```sh
cmake -S . -B build-dos32 -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-dos32-watcom.cmake
cmake --build build-dos32 && ctest --test-dir build-dos32
```
