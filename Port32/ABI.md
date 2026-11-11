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

`goc -cg` is the backend of `goc` for flat hosts (GCC, Clang and Open
Watcom 386). The 16-bit backends are unchanged: for every SDK sample, the
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
It checks the tables on amd64, i686, arm64 and dos32. The target `goc_sdk`
compiles 44 of the 55 SDK_C samples (`Port32/Tests/goc_sdk.txt`). In the
other samples, the application code needs porting: `NULL` used as a handle,
`PtrToSegment`, inline asm, and static optr tables in code.

The ANSI headers forward to the host C library where nothing GEOS-specific
is lost: `ctype.h`, and `size_t` in `string.h`. The GEOS `stdio.h` works on
GEOS files, so it stays GEOS (open).

## 5. Open

1. **Sending messages.** `goc` still casts `CObjMessage` to a prototype
   with the message's parameters. That only worked because of the 16-bit
   Pascal convention. Plan: `goc` puts the parameters of a message into a
   structure and emits a typed thunk per method. The kernel then always
   sends `(destination, message, &parameters, &return value)`, which also
   suits queued messages.
2. **optrs in static data in code**, e.g. GenControl feature tables
   (`{OptrToChunk(@X), @Name, 0}`). Glue relocated these. Options are
   ELF relocations resolved by the geode loader, or unrelocated optrs
   resolved where they are used.
3. **References to objects of other files in resource data.** `@extern`
   objects have no compile-time resource, so they need load-time
   relocation by name.
4. **Geode format**: an ELF shared object plus the `.gp` tables (resources
   as above, exports, protocol numbers); see `GHAL-Concept.md` §4.4.
5. **DOS16 file formats under dos32**: which structures of VM files and
   documents need converters, given 32-bit handles and 64-bit optrs in
   memory.
6. **GEOS `stdio.h` and the DBCS string functions** on top of the host C
   library.
