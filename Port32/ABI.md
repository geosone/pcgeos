# Port32 ABI: GEOS in C on 32/64-bit hosts

Decisions for the C port (plan phases P3/P4). They apply to every Port32
host: `linux`, `win32`/`win64`, `gonware` and `dos32`. The 16-bit build
(pmake, Esp, Open Watcom 16-bit, GEOS32/Loader32) is not affected: shared
headers switch on `GEOS_PORT32`, and that build stays byte-identical
(checked with `Port32/tools/geocmp.py`).

## 1. Compatibility (decided 2026-11-11)

- **No binary compatibility with 16-bit GEOS.** 16-bit geodes do not run on
  the C port; they are rebuilt from source. In memory, the port has its own
  ABI (below).
- **dos32 stays data-compatible with DOS16 GEOS.** On DOS, the port reads and
  writes the same files as 16-bit GEOS: documents and VM files, `geos.ini`,
  fonts, the directory layout of a GEOS installation, and DOS 8.3 names.
  Data is converted at the file boundary (load/store), never by sharing
  in-memory layouts with the file.
- **Other hosts may change formats** (e.g. long names, wider VM block
  handles), as long as dos32 keeps the DOS16 formats.

## 2. Base types (`Port32/include/geosport.h`)

`geos.h` includes `geosport.h` when `GEOS_PORT32` is defined.

| Type | Width | Note |
|---|---|---|
| `byte`/`sbyte`, `word`/`sword`, `dword`/`sdword` | 8/16/32 | fixed on every host (`dword` was `unsigned long`: 64 bit on LP64) |
| `wchar`, DBCS `TCHAR` | 16 | `wchar_t` is 32 bit on Linux, 16 on Windows: never used |
| `Boolean` | 16 | unchanged |
| `Handle` and all `*Handle` types | **32** | decided 2026-11-11 |
| `ChunkHandle` | 16 | offset in an LMem block |
| `optr` | **64** | `handle << 32 \| chunk` (`ConstructOptr`, `OptrToHandle`, `OptrToChunk`) |
| `Message` | 32 | |
| `NULL` | host's | `geos.h` no longer redefines it to `0` (a variadic `0` is no pointer on 64 bit) |

- There are no segments. `_far`, `_near`, `_pascal`, `_cdecl` and `_export`
  are empty. `PtrToSegment`, `SegmentOf` and `PtrToOffset` do not exist, so
  every use fails to compile and has to be rewritten.
- In memory, structures use the host's natural alignment. Packed on-disk
  structures are read only through accessors (`le16`/`le32`, `memcpy`); see
  `GHAL-Concept.md` §7.
- Error checking: the build defines `DO_ERROR_CHECKING` (the GEOS
  convention); `geos.h` derives `ERROR_CHECK` from it.

## 3. Checks

- `test_geosport` (ctest `cinclude.geosport`) checks the widths and the optr
  macros on every host, dos32 included.
- `cinclude_port32` (GCC/Clang hosts) compiles every header listed in
  `Port32/Tests/cinclude_port32.txt` in its own translation unit, after
  `geos.h`, with `-Wall -Wextra -Werror`. At the start, 81 of the 114 `.h`
  files are listed. Most of the others are not self-contained: they need
  other headers included first. A few clash with the host C library: GEOS
  `math.h` against `<math.h>`, and `Ansi/ctype.h` against the built-ins.

## 4. Object resources: `goc -cg`

`goc -cg` is the backend of `goc` for flat hosts (GCC and Clang, C99;
see section 5). The 16-bit backends are unchanged: for every SDK sample, the
`-cw` output is byte-identical to before.

With segments, glue built each LMem resource from three segments: data,
handle table and header. A chunk handle was the address of the chunk's
entry in the handle segment, and `(optr)&Obj` was a far pointer relocated
to the resource handle. Without segments, `goc` does that work itself:

- **Chunk handles are compile-time constants.** Where a chunk is defined,
  `goc` emits `enum { __goc_ch_X = GOCR_CHUNK_HANDLE(<header>, <index>),
  __goc_rs_X = GOCR_RES_<RESOURCE> }`. The handle table follows the block
  header, rounded up to 4 bytes; in an object block, entry 0 is the flags
  chunk. Code (also in other files) gets `const ChunkHandle X` and
  `const word X_resid`.
- **Resource numbers come from the `.gp` file.** `Port32/tools/gpres.py`
  numbers the resources the way glue does: first the `resource` lines in
  order, then any other resource the `.goc` files start. Numbering starts at 2:
  0 is the core block and 1 the fixed data.
- **optrs in resource data are unrelocated:** `GOCR_SOPTR(X)` puts the
  resource number with `GOCR_UNRELOCATED` set in the handle half. Object
  relocation replaces it with the resource handle when the block is loaded,
  as it already does for 16-bit object blocks. `process` as an output is
  `GOCR_PROCESS_OPTR`.
- **In code, `@X` is `GOCR_OPTR(X)`.** This combines the resource handle,
  which the geode loader writes to the geode's hidden
  `__goc_resource_handles[]`, with the chunk handle.
  `OptrToChunk(@X)` of an object in the same file becomes the constant
  `GOCR_SCHUNK(X)`, so static tables of chunk handles still work.
- **Each resource is described by tables:** `GocResource
  __goc_resource_<Name>` gives the number, the header and a `GocChunk` per
  chunk. A chunk can consist of two C objects: a chunk array or GCN list
  header and its elements, or a moniker and its text. Glue relied on the
  segment order to place them next to each other. Now the loader copies
  them one after the other into the LMem block.
- **`ClassStruct` points to its tables** (`Class_messages`,
  `Class_methods`, `Class_htypes`, `Class_relocMethod`). The tables no
  longer have to follow the structure in a class segment, and the reloc
  tables are pointers.
- An `@header` must come before the first chunk of its resource.

Checks: `goc.resources` (`Port32/Tests/Goc`) links a `.goc` file with
objects in two resources, monikers, a GCN list and a `process` destination.
It checks the tables on amd64, i686, arm64 and dos32 (DJGPP). The target `goc_sdk`
compiles 44 of the 55 SDK_C samples (`Port32/Tests/goc_sdk.txt`). In the
other samples, the application code needs porting: `NULL` used as a handle,
`PtrToSegment`, inline asm, and static optr tables in code.

The ANSI headers forward to the host C library where nothing GEOS-specific
is lost: `ctype.h`, and `size_t` in `string.h`. The GEOS `stdio.h` works on
GEOS files, so it stays GEOS (open).

## 5. Sending messages

The 16-bit backends cast `CObjMessage` to a prototype with the message's
parameters, and the kernel takes them from the stack (Pascal convention,
MPD). In C on a flat host, that would be a call through the wrong
function type. Under `goc -cg`:

- **The sender passes the parameters as a structure that is built in
  place:** a C99 compound literal `&(struct {T1 _p0; T2 _p1;}){args}`. goc
  does not have to split the argument list, because the initialiser
  assigns the arguments in order and converts them like a prototype
  would. Array parameters become pointers.
- **The reply comes back through a buffer**, `&(R){0}`. Each kernel
  function returns that buffer, so `@call` stays an expression:
  `(*(R *) CObjMessageP(flags, dest, msg, params, size, reply, size))`.
  `@send` passes no buffer; `@record` gets an `EventHandle` as its reply.
- **Kernel entry points** (`object.h`): `CObjMessageP` for `@send`,
  `@call` and `@record` (and inside `CObjSendToChildren` for
  `@children`), `CObjCallSuperP` and `CMessageDispatchP`. The kernel
  copies `params` into the event when it queues a message, because it
  knows the size.
- **The receiver calls methods through thunks:** for every method of a
  class, goc emits `_goc_t_<Class>_<n>(pself, oself, message, params,
  reply)`. The thunk unpacks the structure and calls the method with its
  real prototype. `Class_methods` points to the thunks (`GocMethodThunk`).
  The method's prototype comes first, so `@extern method` works too.
- `MM_BASED` methods get an ordinary `pself` because there are no
  segments.

Compound literals need C99. Open Watcom 386 lacks them, so **dos32 is
built with DJGPP gcc for the port** (decided 2026-11-11;
`cmake/toolchain-dos32-djgpp.cmake`). Open Watcom remains the compiler of
the 16-bit build. The older Watcom dos32 build
(`cmake/toolchain-dos32-watcom.cmake`) still builds GHAL, but it leaves
out `goc.resources`.

The DJGPP toolchain comes from crossdev (`i586-pc-msdosdjgpp`, gcc 16,
C and C++).
The C library is the DJGPP CVS state after 2.05, and gcc needs two DJGPP
fixes: `float.h` with `#include_next`, and `.bss` section flags. Every
executable gets CWSDPMI bound in (`cwsdstub.exe`,
`Port32/tools/djgpp-bind.sh`), so it runs on plain DOS without other
files.

Checks: `goc.resources` sends messages with scalar, structure and pointer
parameters. It covers `@call` with a reply, `@send`, `@record` +
`@dispatch` and `@callsuper`, through a minimal stand-in kernel in
`Goc/test_gocres.c`. It passes on linux amd64/i686/arm64 and on dos32
(DJGPP, real DOS in QEMU). The 44 SDK samples of `goc_sdk` compile with
the same scheme.

## 6. optrs in static data in code

Code has static tables of optrs, for example GenControl feature lists
(`{OptrToChunk(@X), @XName, 0}`) and `GenAppUsabilityTuple` lists. Glue
relocated them when the geode was loaded. Under `goc -cg`, an optr in
such a table must be a constant, so goc writes it **unrelocated**, the
same way as in resource data (§4): `GOCR_SOPTR(X)` holds the resource
number with `GOCR_UNRELOCATED` set, plus the chunk handle. The value is
resolved where it is used:

- **In the geode that holds the table**, `GOCR_RESOLVE(o)` uses the
  geode's own `__goc_resource_handles[]`. goc applies it to the
  destination of every `@send`/`@call` that is not already relocated, so
  `@send table[i]::MSG` works unchanged. For an optr that is already
  relocated, it changes nothing.
- **In another geode**, for example the UI library reading a controller's
  feature list, `GeodeResolveOptrAt(where, o)` is used (`geode.h`). The
  kernel finds the owning geode from the address `where` that the optr
  was read from.
- Entry 0 of `__goc_resource_handles` is the core block of the geode. For
  an application, this is also its process, which is how `process`
  (`GOCR_PROCESS_RES`) is resolved.

goc decides by context. It follows the C structure of its output: braces,
strings, comments and preprocessor lines. In the initializer of an object
with static storage (file scope or `static`), `@X` becomes
`GOCR_SOPTR(X)`. Everywhere else it remains `GOCR_OPTR(X)`, which is
relocated at once. Automatic variables therefore get relocated optrs.
Only objects and chunks of the same file can be used in static data.
Other objects are a goc error until §7.1 is solved. In the SDK samples,
26 table entries change form, and no other code does.

Checks: `goc.resources` covers a static optr, an optr table at file
scope, a feature-list structure that is `static` in a function, an
automatic optr, `GOCR_RESOLVE` including `process`, `GeodeResolveOptrAt`,
and `@send` to a table entry. The UICtrl and Levels samples no longer
fail on their tables, and Levels now compiles (`goc_sdk`: 45 samples).

## 7. Open

1. **References to objects of other files in resource data.** `@extern`
   objects have no compile-time resource, so they need load-time
   relocation by name.
2. **Geode format**: an ELF shared object plus the `.gp` tables (resources
   as above, exports, protocol numbers); see `GHAL-Concept.md` §4.4.
3. **DOS16 file formats under dos32**: which structures of VM files and
   documents need converters, given 32-bit handles and 64-bit optrs in
   memory.
4. **GEOS `stdio.h` and the DBCS string functions** on top of the host C
   library.
