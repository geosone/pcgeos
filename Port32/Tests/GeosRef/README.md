# GeosRef: reference output of the original GEOS code

`ffaref` is a small GEOS application that runs the test vectors of the C
port of `FloatFloatToAscii` (`../ffa_cases.h`, plus the frame cases of
`../test_ffa.c`) through the **original** math library and writes the results
to `FFAREF.TXT` in the GEOS top directory. Each line is
`name<TAB>result<TAB>character count<TAB>float stack depth afterwards`.
Then it shuts GEOS down.

`ffaref.txt` is that output. It comes from GEOS32 (Loader32, protected mode)
on MS-DOS 7.1 in QEMU (KVM, with an FPU, so the math library uses the
`intx87` coprocessor library). The NC and EC builds give identical output.
`check_ref.py` (ctest `math.floatFloatToAscii.geosref`) checks that the
expected strings of the C tests are exactly these results.

## Regenerating the reference

You need a GEOS target made by `buildbbx.pl` and the DOS base image from
`Port32/tools/mkdosbase.sh` (see `../README.md`). Paths are examples; keep
anything worth keeping in the git-ignored `Local/`.

```sh
# 1. build the application (the directory name must be the geode name)
mkdir -p Local/geosref/ffaref
cp Port32/Tests/GeosRef/ffaref.goc Port32/Tests/GeosRef/ffaref.gp \
   Port32/Tests/ffa_cases.h Local/geosref/ffaref/
cd Local/geosref/ffaref
mkmf && pmake full          # with WATCOM, ROOT_DIR, LOCAL_ROOT, PATH set
cd -

# 2. run it in GEOS on real DOS and fetch the result
DOSRUN_BASE_IMG=$PWD/Local/dosbase.img \
GEOSRUN_VIDEO="VESA Compatible SuperVGA: 640x480 64K-color" \
    Port32/tools/geosrun.sh Local/gbuild/localpc/ensemble FFAREF.TXT \
    Local/geosref/ffaref/ffaref.geo > Port32/Tests/GeosRef/ffaref.txt
```

`geosrun.sh` copies the target to a second disk, adds the application to
`WORLD`, and adapts `geos.ini`: no host file system, the generic mouse
driver, the video device you choose, and `execOnStartup` set to the
application. The whole run takes a few seconds. For an EC target, pass
`ffarefec.geo`. `GEOSRUN_KEEP=DIR` keeps a screenshot and the disk for
diagnosis. Define `FFAREF_DUMP` when building `ffaref` to also log the
`FFA_stackFrame` after every frame case.

## What it took to get GEOS32 running on real DOS

Upstream GEOS32 had only run in DOSBox with its host interface. On a real
DOS with a DPMI host (HDPMI16), several places assumed real mode or
DOSBox. These are fixed on this branch, each under `PROTECTED_MODE` where
the 16-bit build must stay unchanged:

| Where | Problem on a real CPU |
|---|---|
| `Kernel/FSD/fsdInit.asm` | DOSBox probe (`FE 38` opcode) hooked INT 6 the real-mode way; now a GPMI exception handler. `os2` is always the primary FSD (the other DOS FSDs have no protected mode support) |
| `Kernel/Geodes/geodesResource.asm` | relocations were written through the code selector; now through a data alias. Self-segment relocations stored ES (the alias) instead of the block's selector |
| `Kernel/Object/objectClass.asm` | `ObjInstantiate` read `ds:[0]` before checking DS for null |
| `Driver/Font/TrueType` (EC) | a flag was incremented in the code segment |
| `Driver/Video/VGAlike/VGA16` | `MemAlloc` was called with a paragraph count in BX instead of the size in AX (`ALLOC_ZERO` death in EC). The handle pushed for `MemFree` was never popped, so `MemFree` got the interrupt number (KR-09 on every machine with VESA) |
| `Library/Math` | `DetectCoProcessor` wrote its status word to the code segment |
| `Library/CoProcessor` | the same in the coprocessor libraries' entry routine |

One bug was not specific to protected mode. With a coprocessor library
loaded, the math library points its entries to the hardware routines. The
C entry `FLOATFLOATTOASCII` was published later, outside the redirected
range. So C callers with their own `FFA_stackFrame` formatted whatever was
on the unused software stack (`$0.00`) and left the number on the
coprocessor stack. `OverwriteRelocationTable` now also redirects it. In
addition, the coprocessor library's stub returned the caller's AX instead
of the character count.

Open: in plain DOSBox-X (no host interface), the `vga16` driver still
reports no compatible video hardware (UI-18). In QEMU, 640×480 works and
1024×768 does not.
