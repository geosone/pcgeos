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
- **The DOS16 build stays intact.**
  - The C ports lie beside their asm files. The 16-bit build must not
    take them in.
  - `Tools/nmkmf` makes the 16-bit Makefiles and would take every `.c`
    file of a directory. A directory with C ports therefore has a file
    `mkmf.lst` that lists, one name a line, the files the DOS16 build
    takes; mkmf then reads only those.
  - A new asm file in such a directory must be added to its
    `mkmf.lst`.

## 2. Base types (`CInclude/geosport.h`)

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
- **optrs in chunks that are not objects** (moniker lists, GCN list
  elements, chunk data of any C type). Glue relocated these through the
  geode's relocation table. goc does not know the C layout of the data,
  but the compiler does. So goc writes such a chunk a second time, as
  its **shadow** (`__goc_sh_X`, `__goc_sh2_X` for the second object),
  with the same text but with `GOCR_SHADOW_MASK` defined as 0, so that
  the handle half of every unrelocated optr is 0 there.
  - `GocChunk` points to the shadows (`GC_shadow`, `GC_shadow2`).
  - Where data and shadow differ lies a handle to relocate: a run of
    differing bytes ends with the top byte of the handle, which holds
    `GOCR_UNRELOCATED`.
  - The loader relocates these after it has built the resources
    (`ObjRelocateResourceChunks`).
  - Only chunks whose data holds `GOCR_*SOPTR` get a shadow. goc
    captures the output of the chunk (`GccCaptureBegin`/`End`) and
    writes it again with the defined names renamed.
  - Checks: `goc.resources` checks the shadow of a moniker list and that
    chunks without optrs have none. `kernel.geode` checks the
    relocation (section 7.1).
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

A geode uses the C library of GEOS, not the host's: `geos_geode()` puts
`CInclude/Ansi` before the host headers, and the C functions are imports
like any other routine. `Ansi/ctype.h` declares the standard prototypes
under `GEOS_PORT32` (`int isalpha(int)`, ...), so it agrees with the
compiler's built-ins and with a host `<ctype.h>`; `string.h` takes the
host's `size_t`. The GEOS `stdio.h` works on GEOS files, so it stays GEOS
(open).

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
Objects and chunks of other files of the geode (`@extern`) can be used
as well (§6.1). Anything else (`@X` that is not an object or chunk of
the geode) is a goc error in static data. In the SDK samples, 26 table
entries change form, and no other code does.

Checks: `goc.resources` covers a static optr, an optr table at file
scope, a feature-list structure that is `static` in a function, an
automatic optr, `GOCR_RESOLVE` including `process`, `GeodeResolveOptrAt`,
and `@send` to a table entry. The UICtrl and Levels samples no longer
fail on their tables, and Levels now compiles (`goc_sdk`: 45 samples).

### 6.1 Objects of other files (`@extern`)

A geode's objects are spread over several `.goc` files, and one file
refers to another's objects with `@extern object X;` (`chunk`,
`visMoniker`). In code that is `GOCR_OPTR(X)` with the `const` variables
`X` and `X_resid` of the defining file, which works across files at run
time. In resource data and static data in code the value must be a
constant, and C has no constants across translation units. Therefore:

- `goc -cg -x <file>` writes the chunk constants of every chunk the file
  defines as **macros**:
  `#define __goc_xch_X GOCR_CHUNK_HANDLE(<header type>, <n>)` and
  `#define __goc_xrs_X GOCR_RES_<RESOURCE>`. Macros are expanded only
  where they are used, so the header type need not be known when the
  file is read.
- `cmake/GeosGoc.cmake` collects these per geode in `<geode>_objs.h`
  (`cmake/GeosGocObjs.cmake`, rewritten only when it changes). Every C
  file of the geode includes it after `<geode>_res.h`.
- For a name declared with `@extern`, goc writes `GOCR_XSOPTR(X)` /
  `GOCR_XSCHUNK(X)` instead of `GOCR_SOPTR` / `GOCR_SCHUNK`, in instance
  data (links, composite, optr fields), GCN lists, chunk data, resource
  output and static initializers in code.

No load-time relocation by name is needed: resource numbers are fixed per
geode by `gpres.py`, chunk handles by the order of the chunks. The SDK
output does not change (none of its samples uses `@extern` in data). Of
the real multi-file geodes in `Appl` that goc can translate on Linux,
BbxBrow and Webbox use it (12 references), and all resolve. `goc.resources`
is now a geode of two files: `gocres2.goc` uses objects of `gocres.goc` in
an object tree, instance data, chunk data, a static table, `OptrToChunk`
in static data, code and `@send`.

## 7. Geodes as modules

A geode is a module in the host's format (`GHAL-Concept.md` §4.4: DXE3 on
dos32, ELF shared object on linux and gonware), loaded by `ghal_module`.
What glue put into the geode header from the `.gp` file is the exported
object `__geos_module` (`CInclude/geosmod.h`), written by
`Port32/tools/gpgeode.py`:

- permanent name and extension (space padded), long name, token,
  attributes (`type`, computed as glue does: without `single` the geode
  is multi-launchable), protocol from the `.rev` file, `heapspace` (in
  paragraphs, `13k` = 832), `stack`, process class, `appobj` (an
  unrelocated optr, §4), `entry`;
- the libraries (`library x [noload [fixed]]`);
- the exported entries in `.gp` order with their names (`export x as y`,
  `publish`, `skip n`, `skip until n` as empty slots), so entry numbers
  stay those of the `.gp` file;
- the resource table indexed by resource number (`gpres.py`): the `RESF_*`
  flags of the `resource` lines exactly as glue computes them, and for
  LMem resources made by goc a pointer to their `GocResource`;
- `GM_resourceHandles`, the geode's `__goc_resource_handles[]`, which the
  kernel fills as it loads the resources.

`geos_geode(<name> GP <x.gp> GOC <files>)` (`cmake/GeosGeode.cmake`) builds
such a geode. All 45 samples of `goc_sdk` are built this way on every
host; their imports are GEOS routines and classes only (the C library
functions are GEOS's, see §4). ctest `geode.hello` loads the Hello sample
(no kernel yet: every import gets a dummy address) and checks its
`__geos_module` against `hello.gp`, down to the class pointer of the
application object, which must be the import `GenApplicationClass`.

### 7.1 GeodeLoad in the C kernel (`Library/Kernel/Geodes`, P4)

`geodesLoad.c`, `geodesLibrary.c` and `geodesAccess.c` load geodes
from their modules:

- **Geode files.** `GeodeLoad` reads the geode file of the given name
  from the current path, whole, and hands it to `ghal_module`; the file
  is closed again (DOS16 kept it open to load resources from it). A
  geode file is a native file (no GEOS header): what the header held is
  in `__geos_module`.
- **Libraries.** A library not loaded yet is the file of its permanent
  name, up to the first blank, plus `.geo`, in `SP_SYSTEM` (and so at
  every place of `SP_SYSTEM`, §13a), as `TryOpenLib` did. There are no
  `ec.geo` files to try first.
- **Images.** A program may also register images by name
  (`GeodeRegisterImage`; tests embed their geodes this way). A
  registered image comes before the file; a library's image is looked
  up under its permanent name.
- **Libraries first.** A module describes itself only once it is
  loaded. So `GeodeLoad` loads it once with dummy imports, reads the
  library list from `__geos_module` and unloads it again. Then it uses
  the libraries (found among the loaded geodes, or loaded) and loads
  the module for good.
- **Imports by name.** An import comes from one of three places:
  - the kernel's export table: every symbol of the kernel's objects
    that `Library/Kernel/geos.gp` exports, case ignored
    (`Port32/tools/kexports.py`, generated at build time from `nm`);
  - a few host names: `raise` for libgcc on ARM, and libgcc's frame
    registration for a DXE3;
  - a library's exports: an "export ... as" name, or a module symbol
    that is one of its exported entries.
- **Protocols.** A library's protocol is checked as
  `TestProtocolNumbers` did: the importer's major number must be equal,
  and its minor number must not be newer. 0.0 accepts any protocol,
  which is what gpgeode writes for now.
- **Core block.** The core block is a fixed block that owns itself; its
  handle is the geode handle. It holds the kernel's data on the geode
  (`GeodeCore` in `kernelC.h`): the name, the attributes, the
  reference and thread counts, and the libraries in use. The GEOS data
  stays in the module.
- **Resources.**
  - Each LMem resource made by goc becomes a block owned by the geode
    (`LMemCreateResourceBlock`), with heap flags from its `RESF_*`.
  - The handles go to `GM_resourceHandles`; entry 0 is the core block.
  - The optrs in chunks that are not objects are relocated through
    their shadows (section 4).
  - Object blocks are relocated once their exec thread is known.
  - Code and data resources are part of the module and have no handle.
- **Library entries.** `LCT_NEW_CLIENT` goes to each library of a new
  geode, and `LCT_ATTACH` to a new library's own entry (a TRUE return
  fails the load). `LCT_CLIENT_EXIT` and `LCT_DETACH` go out when the
  geode is freed. `GeodeUseLibrary` and `GeodeFreeLibrary` also call
  the entry for the running geode.
- **Processes.**
  1. The loader allocates the event queue (owned by the geode) and
     queues `MSG_META_ATTACH`, with the appInfo high and low words as
     `value1` and `value2`.
  2. It creates the process thread with the process class, attached to
     the queue but stopped (`KernelEventThreadCreate`).
  3. The core block and the object blocks get that thread as their
     exec thread, and only then does the thread start.
  4. When the last thread of a process exits, it frees the geode. The
     geode's blocks and queues go, the module is unloaded, and the
     references to its libraries are dropped.
- **Multiple launches.** A loaded library is shared. A process that is
  not multi-launchable fails a second time with
  `GLE_NOT_MULTI_LAUNCHABLE`.
- **A thread that ends inside a handler.** A handler that ends its own
  thread (`MSG_PROCESS_EXIT` calls `ThreadDestroy`) never returns, so
  the dispatcher cannot free the event or wake an `MF_CALL` caller.
  `CMessageDispatchP` keeps a chain of the events a thread is
  dispatching, and `ThreadDestroy` frees them. In the asm kernel the
  event had been freed already, because its parameters were in
  registers.
- **Other routines:** `GeodeGetInfo` (attributes, protocol, token,
  permanent name, reference count; no release number), `GeodeFind`,
  `GeodeGetProcessHandle`, `GeodeGetAppObject` and
  `ProcGetLibraryEntry` (entries in `.gp` order).

The Open Watcom build of dos32 has no module loader
(`ghal_module_none.c`), so it loads no geodes.

ctest `kernel.geode` loads two geodes built with `goc -cg` (sources in
`Port32/Tests/Geode`): the library `kglib` (protocol 2.3; a routine, a
class and an entry that counts its calls) and the process `kgapp`, which
uses `kglib` and holds an object of `kglib`'s class in its object
resource. The test checks:

- the load errors: no such geode, wrong attributes, the three protocol
  errors, no such library, and a second launch;
- the library entry calls, `GeodeGetInfo`, `GeodeFind` and
  `ProcGetLibraryEntry`;
- `MSG_META_ATTACH` on the process thread: it calls into the library
  and sends messages to the relocated object, and `GeodeGetAppObject`
  works;
- the optrs in an LMem data resource (an optr chunk, a structure with
  an optr behind a word, a chunk array of optrs) and in a static GCN
  list are relocated (and the test fails without the shadows);
- `ObjDuplicateResource` of a template, which holds references within
  itself, to another resource and in chunk data: with the owner and
  thread given, and taken from the source; also the saved-block list
  and `ObjFreeDuplicate` (the test fails without `OLMBH_resource`);
- `MSG_META_DETACH`: the process frees itself, then `GeodeFreeLibrary`
  frees the library;
- at the end, not a single handle is left.

It runs on amd64, i686, arm64, armhf and DJGPP dos32. At its end it
loads `kgapp.geo` from `WORLD` and `kglib.geo` from `SYSTEM` as files,
and checks that a missing library file is `GLE_LIBRARY_NOT_FOUND`.

Not yet:

- `noload` libraries;
- resources loaded on demand (`RESF_DISCARDED`);
- heapspace, the UI thread, `LCT_NEW_CLIENT_THREAD`;
- freeing a geode's semaphores and timers;
- `GeodeResolveOptrAt`.

### 7.2 Drivers (`Geodes/geodesDriver.c`, P4)

**Strategy routine.** A DOS16 driver had a strategy routine. It was
called with the function number in `di` and the arguments in the
registers its `.def` file describes. The carry flag returned an error.
In Port32 the strategy routine is a C function
(`CInclude/geosdriver.h`):

```c
typedef struct {
    dword DR_ax, DR_bx, DR_cx, DR_dx, DR_si, DR_bp;
    void  *DR_dsSi, *DR_esDi;   /* segment:offset pairs as pointers */
    dword  DR_di;               /* di as a value */
    void  *DR_esSi;             /* es:si as a pointer */
} DriverRegs;
typedef Boolean DriverStrategy(word function, DriverRegs *regs); /* carry */
```

- The registers are dwords because handles are 32 bits wide (§2).
- A ported driver keeps the register use of its `.def` file.
- The file system drivers have their own table of C functions instead
  (§13f).

**Driver table.** A driver defines its `DriverInfoStruct` as the
variable `DriverTable`, as glue required. `gpgeode.py` puts its address
into `GM_driverTable` (`GEOS_MODULE_VERSION` 2). `DRIVER_STRATEGY(f)`
stores a strategy function in it, and `DriverCall(dis, fn, &regs)`
calls it.

**Load and free.**
- Once loaded, a driver gets `DR_INIT` with its geode handle in `bx`
  and the appInfo words in `cx` and `dx`.
- A carry fails the load with `GLE_DRIVER_INIT_ERROR`, and the driver
  gets no `DR_EXIT`.
- It gets `DR_EXIT` before it is freed.
- A driver of type `system` joins the system driver list.
  `GeodeExitSystemDrivers` gives the drivers on it `DR_EXIT`, the last
  first (`ExitSystemDrivers`, for the shutdown).

**Using geodes.** As in `UseLibraryDriverCommon`:
- `GeodeUseDriver` and `GeodeUseLibrary` take a file name. They load
  the geode, or use it if it is loaded already. A missing file is
  `GLE_FILE_NOT_FOUND`.
- `GeodeUseDriverPermName` and `GeodeUseLibraryPermName` only use a
  loaded geode; otherwise the error is `GLE_LIBRARY_NOT_FOUND`.
- Up to now, `GeodeUseLibrary` had taken a permanent name.

**Other routines.** `GeodeFreeDriver`, `GeodeInfoDriver`,
`GeodeGetDefaultDriver` and `GeodeSetDefaultDriver`. The default video,
keyboard and task drivers do not yet get their strategy shortcuts.

ctest `kernel.geode` loads the test driver `kgdrv` from `SYSTEM`. It
checks:
- a refused `DR_INIT`;
- `DR_INIT` with the geode handle;
- a call of the strategy routine, and the carry for an unknown call;
- the reference counts of both use routines;
- the default driver;
- `DR_EXIT` on the last free and from `GeodeExitSystemDrivers`, and no
  second one.

**What a geode may import.** A geode has no host C library (§7). The
kernel exports, besides its routines:
- the GHAL host services that drivers need (`kexports.py`
  `GHAL_EXPORTS`): `ghal_crit_*`, `ghal_log`, `ghal_time_monotonic_ns`,
  `ghal_irq_*`, `ghal_input_*` and `ghal_display_*`;
- `memcpy`, `memmove`, `memset` and `memcmp` (`COMPILER_EXPORTS`). gcc
  and clang may call these on their own even in a freestanding geode,
  for structure copies and zeroing.

These entries have no asm counterpart. Their entry numbers are
`0x8000` plus their position in the list of extras.

**Protocol.** A driver's protocol is the protocol of its driver class.
glue took it from the `.ldf` file of that protocol (`kbd.ldf` gives
`KEYBOARD_PROTO`, 4.1), not from the `.rev` file. `geos_geode(...
PROTOCOL 4.1)` passes it to `gpgeode.py --protocol`.

### 7.3 The keyboard driver (`Driver/Keyboard`, P4)

The C keyboard driver keeps the design of the asm driver
(`keyboard.h`, `keyboardProcess.c`, `keyboardInput.c`,
`keyboardInit.c`, beside the asm files).

**The interrupt.**
- The handler of `GHAL_IRQ_INPUT` takes over from the IRQ 1 handler.
- It reads the keyboard's HID events (`ghal_input_read(GHAL_INPUT_KEYBOARD)`).
- It turns them into the set-1 scan codes the 8042 gave. Keys of the
  extended keyboard get `0xe0`. `<Pause>` becomes
  `e1 1d 45 e1 9d c5` and sends nothing on release.
- It sends each scan code to the output as `MSG_IM_KBD_SCAN`, with
  `cx` = `extension << 8 | code` and `0x80` set on a release. The
  message is queued (`MF_FORCE_QUEUE`), as from the interrupt.
- `IM_FIRST_MESSAGE` is 8704 (`CInclude/Internal/im.h`): `IMClass` is a
  subclass of `ProcessClass`. ctest `kernel.obj` checks this number
  against goc.

**`DR_KBD_XLATE_SCAN`.** This is a port of `ProcessKeyElement`,
`ConvertExtCodes`, `ProcessScanCode`, `HandleExtendedDef`,
`HandleNormalDef`, `ProcessStateRelease` and `AccentTranslation`
(SBCS).
- It works on the tables of the keyboard map. `Port32/tools/kmapgen.py`
  makes C of a `kmap*.def` file (`KeyboardMap`,
  `CInclude/Internal/kbdMap.h`). It handles all 28 SBCS maps.
- It returns `di` = `MSG_META_KBD_CHAR` (119) and `al` = 1 for a
  character.
- It returns `cx` = the character, `dl` = `CharFlags`,
  `dh` = `ShiftState`, `bp` = `ToggleState` with the scan code above.

**Options.** These `[keyboard]` keys are read: `keyboardTypematic`,
`keyboardDoesLEDs`, `keyboardAltGr`, `keyboardShiftRelease` and
`keyboardSwapCtrl`. The toggles start off.

**Not in the C driver:**
- the BIOS shift and toggle bytes;
- the LEDs and the typematic rate of the 8042 (the states are kept; the
  host repeats keys itself);
- hotkeys, `DR_KBD_MAP_KEY` and `DR_KBD_CHECK_SHORTCUT` (not yet);
- DBCS.

**Output.** As in DOS16, `DR_INIT` adds the driver as a monitor of the
input manager at `ML_DRIVER` (`KbdMonRoutine`, §13j). `ImAddMonitor`
returns the input manager's thread, which becomes the output.
`KbdMonRoutine` translates the scan codes in the chain.
`DR_KBD_CHANGE_OUTPUT` and `DR_KBD_SET_KBD_STATE` can still change the
output; without one, the driver sends nothing. `DR_EXIT` removes the
monitor.

**Geodes.** There is one geode per map: `kbd.geo` (US, `kbd.gp`) and
`kbde_gr.geo` (German extended, `German/Extended/kbde_gr.gp`).
`Driver/Keyboard/mkmf.lst` keeps the C files out of the DOS16 build.

**Boot.** `KernelBoot` loads `[keyboard] driver` from
`SP_KEYBOARD_DRIVERS`; if that fails it falls back to `kbd.geo`, and if
that fails too it raises `CANNOT_LOAD_KEYBOARD_DRIVER`. The driver
becomes the default `GDDT_KEYBOARD` driver (`LoadKeyboardDriver`).
Without the key, no keyboard driver is loaded.

ctest `driver.keyboard` boots with `kbd.geo` and loads `kbde_gr.geo`.
It posts HID events and translates the scan codes it gets from a queue
output. It checks:
- letters, digits, `<Shift>`, `<Caps Lock>` and `<Ctrl>`;
- the cursor keys and `<Del>`;
- the keypad with and without `<Num Lock>`;
- the `<Pause>` sequence;
- `DR_KBD_GET_KBD_STATE`;
- no output;
- the way through the input manager to its output (`ImGrabInput`);
- the German map: y and z, the dead acute, and `<Alt Gr>`-q giving @.

### 7.4 The mouse drivers (`Driver/Mouse`, P4)

As in DOS16, a mouse driver is the common part plus a device part.
- `mouseCommon.c` is the C port of `mouseCommon.asm`, beside it. It has
  the strategy routine, init/exit, acceleration, the combine mode and
  `DR_MOUSE_CHANGE_OUTPUT`.
- `MouseSendEvents(buttons, x, y, absolute)` sends `MSG_IM_PTR_CHANGE`
  and `MSG_IM_BUTTON_CHANGE` to the input manager. `buttons` are
  `MouseButtonBits`, 1 meaning up.
  - Relative motion is accelerated as before (`[input]`
    `mouseAccelThreshold` and `mouseAccelMultiplier`).
  - Absolute positions go out with `PI_absX`/`PI_absY` and no
    acceleration. This was compile-time `MOUSE_USES_ABSOLUTE_DELTAS`;
    here it is decided per call, because a host has both kinds.
- `MouseSendWheel` sends `MSG_IM_MOUSE_WHEEL_VERTICAL`
  (`mouseNaturalScroll`).
- The driver table is the `MouseDriverInfo` of `mouseDr.def`, including
  `MDI_flags` (`CInclude/Internal/mouseDr.h`; the public `mouseDr.h`
  stays as it was). The function numbers follow `DRE_SET_DEVICE`:
  `DR_MOUSE_SET_RATE` is 12. The protocol is 3.2, as in DOS16.

**Combining.** DOS16 folded pointer changes into the last one in the
input manager's queue (`MouseCombineEvent`, `MF_CUSTOM`). The C kernel
has no custom combining, so the changes go out as they come. The
combine mode is only stored; the input manager combines what it sends
on (§13j).

**Not yet:** `ImSetPtrFlags`, calibration, the coordinate buffer
escapes, wheel keys, and rates (`MOUSE_CANT_SET_RATE`).

**The host pointer** is `Driver/Mouse/Host` (`hostmse.geo`). It has no
asm original: DOS16 had one driver per kind of hardware, and on a host
the pointer of the window system or console comes from `ghal_input`.
- The handler of `GHAL_IRQ_INPUT` reads the pointer's events.
- X and Y are a position (`GHAL_INPUT_ABSOLUTE`) or a change.
- HID buttons 1, 2, 3 and 4 become GEOS buttons 0, 2, 1 and 3 (primary,
  right, middle, fourth).
- The wheel turned away from the user is up (< 0).
- Motion goes out before a button change that follows it in the same
  batch, so a press lands where the pointer was. Pan is dropped.

The mouse driver is loaded by the UI, not the kernel (as in DOS16).

ctest `driver.mouse` loads `hostmse.geo` from `SP_MOUSE_DRIVERS` and
reads the input manager's output. It checks:
- position and motion;
- the three buttons, and a press after motion in one batch;
- the wheel and acceleration;
- the combine mode and the rate;
- `DRE_TEST_DEVICE` and the driver table;
- `DR_MOUSE_CHANGE_OUTPUT` (the raw `MSG_IM_*`);
- no events after `DR_EXIT`.

### 7.5 The video drivers (`Driver/Video`, P4)

As in DOS16, a video driver is the common part plus a device part.

**The common part** is `VidCom/*.c`, the C port beside `vidcom*.asm`:
- `vidcomEntry.c`: the strategy routine, init/exit, the device, the
  exclusive and the escapes;
- `vidcomOutput.c`: `DR_VID_RECT`;
- `vidcomPalette.c`: the palette.

**The device part** draws into its frame buffer and owns the pointer.

**Registers.** `CInclude/Internal/videoDr.h` is the C form of
`videoDr.def`:
- The function numbers follow `DRE_SET_DEVICE` (`DR_VID_INFO` is 12),
  and the escapes start at `0x8100`.
- `VideoDriverInfo` is the driver table. The protocol is 5.3, as in
  DOS16.
- The `DriverRegs` registers carry what the `.def` passes. Segments
  become pointers:
  - `ds` (the GState) is `DR_dsSi` and `si` is `DR_si`, the offset of
    the `CommonAttr` in it;
  - `es` (the Window) is `DR_esDi`;
  - a far pointer in `dx:si` (`DR_VID_INFO`, the palette, a
    `PointerDef`, a device name) is `DR_dsSi`.
  - `ss:bp` (the `PutBitsArgs` of `DR_VID_PUTBITS` and
    `DR_VID_PUTLINE`, the parameters of `DR_VID_REGION`) and the
    `ds:bp` of `DR_VID_GETBITS` are `DR_bpPtr`.
  - The far pointer in two other registers is `DR_farPtr`: `bx:dx` of
    `POLYGON` and the dash functions, `bx:si` of `POLYLINE`, `dx:cx` of
    `REGION`.
  - `geosdriver.h` gained both fields at the end of `DriverRegs`.

**Window and GState.** The driver reads the Window and the GState as
the graphics system will keep them:
- `CInclude/Internal/window.h`, `gstate.h` and `tmatrix.h` are the C
  forms of `window.def`, `gstate.def` and `tmatrix.def`, with 32-bit
  handles.
- The `GOpt` fields of the GState were opcodes the asm patched into its
  code; here they are flags.
- A window's regions are chunks of its block (`WinRegionOf`). A region
  is lines of words: the bottom y of a band, its on/off pairs, then
  `EOREGREC`; one more `EOREGREC` ends it. The mask region has no
  bounds words; `W_maskRect` holds its bounds.

**As in DOS16:**
- Functions up to `DR_VID_END_EXCLUSIVE` run without the driver's
  semaphore; the rest run with it.
- While another GState holds the exclusive, a drawing function is not
  done. Its bounds, clipped to the mask, are collected and returned by
  `DR_VID_END_EXCLUSIVE`; the caller invalidates them.
- The video functions fail until `DRE_SET_DEVICE`.
- `DR_VID_RECT` clips to `W_maskRect`. A rectangular mask
  (`WGF_MASK_SIMPLE`) is filled at once. Any other mask is filled line
  by line through the clip line: the band that holds the line, from the
  kernel's `WinValClipLine` (`Library/Kernel/Win/winIndividual.c`, the
  first piece of the window system). The pointer is hidden while it is
  in the way.

**Colours and mix modes.** `DR_VID_RECT` draws with the `CommonAttr`'s
RGB value (the kernel maps an index to it), the draw mask (unless
`AO_MASK_1`), and the GState's mix mode (`MM_COPY` for
`AO_MASK_1_COPY`).

**Lines and bits** (`vidcomLine.c`, `vidcomRaster.c`):
- `DR_VID_LINE` is Bresenham's line in runs, each run a rectangle
  through the mask (`VidFillClipped`). Both ends are drawn. Where the
  error ties, a pixel may differ from the asm's DDA.
- `DR_VID_GETBITS` writes a simple 24-bit `Bitmap`: the header, then as
  many scan lines as fit.
- `DR_VID_PUTBITS` draws mono, 4-bit, 8-bit and 24-bit scan lines:
  - with their mask (`BMT_MASK`, ahead of each line) and the GState's
    mix mode;
  - indices go through the bitmap's palette or the driver's. The
    driver's palette starts with the 16 standard colors until
    `DR_VID_SET_PALETTE`.
  - with `PBF_FILL_MASK`, the 1s are drawn in the area color with its
    draw mask.
- `DR_VID_MOVBITS` copies where the destination is visible, bottom up
  when it moves down.
- All three walk the visible spans of each line (`VidForSpans`).

**Lines from runs.** Lines and everything built on them step through
`VidLineRuns`: Bresenham's line in horizontal or vertical runs, with
the run's first pixel index along the line.
- `DR_VID_POLYLINE`: the separator format (`SEPARATOR` between
  polylines, two at the end).
  - It draws with a brush (`al` wide, `ah` high, at least 1x1) in the
    line attributes.
  - It leaves out the first pixel of each line after the first.
- `DR_VID_DASH_LINE` steps the on/off pattern (`WWFixed` pixels) along
  the major axis.
  - It starts at `DI_pattIdx` and leaves it where the line stopped.
  - With `al` 1 the first pixel is left out and not counted.
  - With no pattern the line is solid.
  - Where a dash ends may differ from the asm's fixed-point stepping by
    a pixel.
- `DR_VID_DASH_FILL` fills each dash together with its copy moved by
  `ax`, `cx`, as a four-point polygon.
- `DR_VID_POLYGON` (`vidcomPolygon.c`): a convex polygon including its
  outline.
  - Each edge is stepped as a line; each row is filled from its
    leftmost to its rightmost edge pixel.
  - With `al` 1 it is drawn only when its points go clockwise (positive
    area, y down).
- `DR_VID_PUTLINE` (`vidcomPutLine.c`): pixel k of the line takes pixel
  k of the bitmap's first scan line, as far as the bitmap reaches.

**Regions.** `DR_VID_REGION` (`vidcomRegion.c`) draws a region (its
bounds, then bands) moved by `ax`, `bx`:
- each pair of a band is a rectangle through the mask;
- coordinates from `0x4000` to `0xbfff` take `PARAM_0`...`PARAM_3` from
  `DR_bpPtr`, as `TransRegCoord` did.

The exclusive collects the bounds of all these functions.

**The XOR region** (`vidcomXOR.c`) is the frame drawn while a window is
moved or resized.
- `DR_VID_SET_XOR` takes:
  - the position (`ax`, `bx`);
  - the region as a handle and an offset (`dx:cx`), locked for as long
    as the region is set;
  - the `VisXORFlags` (`si`);
  - the `VisXORParams` (`DR_bpPtr`).
- It is inverted over everything and clipped to the screen only.
- It is taken away around drawing that touches it.
- On `DR_VID_MOVEPTR` the position and parameters that the flags name
  move with the pointer. The asm moved them only while the pointer was
  shown.
- `DR_VID_CLEAR_XOR` returns the parameters and the position as they
  are at that moment.

**No save under areas**, as on the 24-bit VGA (`vga24Under.asm`):
`DR_VID_SAVE_UNDER` says it is not possible.

**Not yet:**
- the escapes (all unsupported, `di` 0).

**`PUTSTRING`** (`vidcomChars.c`): the position (WBFixed, device) in
`ax.dl` and `bx.dh` (`dl`, `dh` the low and high byte of `DR_dx`), the
string, its length and the locked `FontBuf` in a `VidPutStringArgs`
(`videoDr.h`) at `DR_bpPtr`. The position after the text comes back as
it came in (`DR_ax`, `DR_bx`, `DR_dx`). Bitmap characters only: what the font driver would build,
region, greyscale and rotated characters, control characters and
`GS_textDrawOffset` are not done yet.

**The host display** is `Driver/Video/Host` (`hostvid.geo`). It has no
asm original: on a host the screen is the `ghal_display` surface, 32
bits per pixel, whether that is a window, the whole screen or a frame
buffer.
- The device name gives the size, either `"<anything>: WxH"` or
  `"WxH"`; the default is 640x480. Full screen takes the screen's size.
- The driver flushes what changed at the end of each function.
  `DR_VID_SCREEN_OFF` keeps drawing but shows nothing until the screen
  is on again.
- To GEOS it is a 24-bit device (`BMF_24BIT`, `DC_CF_RGB`).
- The pointer (`hostPointer.c`, after `vga24Pointer.asm`) starts hidden
  with the default arrow (`pBasic`). It can be up to 32x32; DOS16 took
  16x16.

**The memory video driver** is `Driver/Video/Dumb/VidMem`
(`vidmem.geo`; `Main/mainMain.c` and `vidmemUtils.c` beside the asm,
with the C vidcom). It draws into the bitmaps of `GrCreateBitmap`
(§13m), the default driver `GDDT_MEMORY_VIDEO`.
- The device part learns at the start of each function what it draws
  on (`VidDevBegin`, the window at `es:di`): the window's `W_bitmap`
  names the huge array (an optr, file << 32 | block, as handles are 32
  bits; DOS16 had file:block in a dword). It locks the directory block
  itself unless the kernel did (`W_bmSegment`), and the scan lines of
  one data block at a time.
- `DR_VID_INFO` (`VidDevInfo`) describes the bitmap: its size and
  format, `BMF_MONO` while the mask is edited.
- Formats: monochrome (1 = black), 4 and 8 bits (palette indices: a
  color is drawn as the nearest entry of the bitmap's palette or the
  default one, the mix modes work on the indices), 24 bits; the mask
  with `BM_EDIT_MASK`. A monochrome bitmap gets black where a color is
  dark; DOS16 dithered.
- Not yet: CMYK, dithering, color transfer, the palette functions.
  There is no device to set, no pointer, no XOR region.

The host display got `VidDevInfo` and `VidDevBegin` too; they do
nothing there.

Who tells the input manager the screen's size is the window system's
business, as `ImSetPtrWin` did in DOS16. Until there is one, it is
`ImSetScreenLimits` (§13j).

ctest `driver.video` loads `hostvid.geo` from `SP_VIDEO_DRIVERS` and
draws with a Window and GState of its own. It reads the pixels back
from the display, under Xvfb where the display is X11. It checks:
- the device and `DR_VID_INFO`;
- rectangles in a rectangular mask, and the clipping;
- a mask region with a hole;
- a draw mask with `MM_XOR`;
- the exclusive and its bounds;
- the pointer: shown, moved, nested hide, kept over drawing, and a
  pointer of the test's own;
- lines, including through the mask region;
- `GETBITS`;
- `PUTBITS`: mono, filled, 4 and 24 bits, and with a mask;
- `MOVBITS` over itself, in both directions;
- polygons, also clockwise-only, and the bounds of an excluded one;
- regions, also with a parameter;
- polylines with a brush;
- dashed lines with their pattern index, without the first pixel, and
  solid;
- fat dashes;
- a scan line along a line;
- the XOR region: over drawing, following the pointer, and cleared;
- `DR_VID_GETPIXEL`, the palette, an escape, and the screen off and
  on.

## 8. Kernel heap (`Library/Kernel/Heap`, P4)

The C kernel starts with the handle table and the memory block API of
`heap.h` (`heapHandle.c`, `heapHigh.c`; fatal errors in
`Sys/sysError.c`, internal interfaces in `Library/Kernel/kernelC.h`).

- **Handles** are the index of their entry times 16: 0 stays the null
  handle and handles keep the alignment they had as offsets into the
  16-bit handle table. The table grows in pages of 1024 entries (up to
  16M handles); entries never move. Memory handles have type `HT_MEM`,
  the other handle types keep the signatures of `heapInt.def`.
- **Blocks** are one GHAL block each (`ghal_mem_alloc`, as
  `GPMI_CALL_ALLOCATE_BLOCK` in the PM kernel). A block does not move,
  except through `MemReAlloc`. There is no compaction and no swapping;
  discarding frees the data and keeps the handle, and `MemLock` on a
  discarded block returns `NULL`.
- **Sizes stay words** (64K per block): LMem chunk handles are 16-bit
  offsets, and big data lives in HugeArrays and VM files.
- **`MemGetInfo` returns a `dword`** under `GEOS_PORT32`, so the owner
  (a 32-bit handle) fits. `MGIT_ADDRESS` is a fatal error (no segments;
  use `MemDeref`).
- **Fatal errors** keep the names of `kernelErrors.def`, not their
  numbers; the C kernel reports them by name (`KernelFatal`).
- `HandleP`/`HandleV`, `MemPLock`/`MemUnlockV` and `MemThreadGrab`
  use a semaphore in every handle entry (section 9). Not yet: the
  shared/exclusive locks.
- **`MemModifyOtherInfo` takes a `dword`** under `GEOS_PORT32`: the
  other info may hold a thread handle (exec thread, grabbing thread).

ctest `kernel.heap` covers allocation, locking, resizing (zero fill),
discarding, fixed blocks, reference counts, owners, 5000 handles and the
error checks (caught with `kernelFatalHook`) on all hosts, dos32 with
both DJGPP and Open Watcom.

## 9. Threads and semaphores (`Library/Kernel/Thread`, P4)

GEOS keeps its own scheduler (GHAL-Concept §4.1); the GHAL runs the
contexts (`ghal_ctx`).

- **`ghal_ctx`**: exactly one context runs; `ghal_ctx_switch` stops the
  caller and runs the target. The `ghal_crit` nesting belongs to the
  context. linux: one host thread per context and a baton (no
  `pthread_exit`; an ended context returns from its thread function).
  dos32: own stacks and one small stack switch routine (DJGPP and Open
  Watcom); Watcom's stack check limit `_STACKLOW` is switched with the
  context, so stack overflow checks keep working per thread.
  `ghal_idle` waits for the next interrupt when no thread can run;
  `ghal_irq_active` tells the kernel it must not switch.
- **Threads** are `SIG_THREAD` handles with a `KThread`. The run queue is
  ordered by priority (lower number first, FIFO within a priority). A
  thread switches when it blocks, ends, yields, or when a thread of
  better priority becomes ready outside interrupt code. Not yet: time
  slices and CPU usage (with the timer), the `ThreadDestroy`
  acknowledgement (with messages), event queues, exceptions.
- **Stacks**: the 16-bit stack sizes are too small for 32/64-bit code;
  every context gets at least the GHAL minimum (dos32 64K, linux 256K).
- **Semaphores** (`ThreadAllocSem`, `ThreadPSem`, `ThreadVSem`,
  `ThreadPTimedSem`) and **thread locks** (`ThreadGrabThreadLock`, nested
  per thread) are `SIG_SEMAPHORE` handles; the waiting threads are queued
  by priority. Timeouts are in ticks (1/60 s) of the GHAL clock;
  timeout 0 does not block.
- **`ThreadGetInfo` returns a `dword`** under `GEOS_PORT32` (thread and
  queue handles are 32 bits).

ctest `kernel.thread` covers turn taking, priorities and preemption,
yield and `ThreadModify`, release of ended threads, nested thread locks
and `MemThreadGrab` between threads, `MemThreadGrabNB`, timed P and the
error checks; `ghal.core` covers `ghal_ctx` on every host.

## 10. LMem (`Library/Kernel/LMem`, P4)

`LMem/lmemCode.c` keeps the block layout of the asm kernel: header,
handle table at `LMBH_offset` (a chunk handle is the offset of its
entry; 0 is a free handle, `0xffff` a chunk without data), then the
chunks, each with a size word (data size + 2) right before its data.
That is the layout `goc -cg` describes (section 4), and
`LMemCreateResourceBlock` builds a resource's block from its
`GocResource`: header, handle table, chunks in handle order, the two
parts of a chunk one after the other.

- **Chunk data is aligned to 8** (objects hold pointers and 64-bit
  optrs): every chunk occupies a multiple of 8 bytes including its size
  word, and the heap starts where the first chunk's data is 8-aligned.
- Free chunks are linked through their first word, sorted by address and
  merged with free neighbours; a free chunk's size word is its whole
  slot. Allocation is first fit, then compaction, then growing the block
  (with some padding). Blocks stay below 64K.
- Chunks move (compaction) and the block may move when it grows, as in
  the asm kernel: dereference again after `LMemAlloc`, `LMemReAlloc`,
  `LMemInsertAt`. `LMemReAlloc`/`LMemInsertAt` return TRUE on failure
  (with `LMF_RETURN_ERRORS`; otherwise `LMEM_CANNOT_ALLOCATE` is fatal).
- Object blocks (`LMF_HAS_FLAGS`): handle 0 is the flags chunk, one
  `ObjChunkFlags` byte per handle; it grows with the handle table.
  `MemAllocLMem(LMEM_TYPE_OBJ_BLOCK)` makes the running thread the exec
  thread, as in the asm kernel.
- `LMemDeref` of a chunk without data returns `NULL`.
- Not yet: `LMF_NO_HANDLES` (DB items), `LMemSplitBlock`, GCN lists
  (they send messages). Object relocation: section 13.

**Chunk, element and name arrays** (`lmemChunkArray.c`,
`lmemElementArray.c`, `lmemNameArray.c`) keep the asm format: header
(`CAH_offset` bytes), fixed-size elements right after it, or for
variable-sized ones a table of `CAH_count` word offsets (from the chunk
start) and the elements. Element arrays start each element with a
`RefElementHeader` (24-bit reference count) and keep freed elements on a
free list (`EAH_freePtr`, `WAAH_high` = `0xff`), so tokens stay valid.
Name arrays are variable-sized element arrays of reference count, data
(`NAH_dataSize`) and name (no terminator).

- `RefElementHeader` is 4 bytes in Port32 (`WordAndAHalf` gets host
  alignment), 3 in 16-bit GEOS: name and element arrays in DOS16 files
  need the converter (section 14).
- `ElementArrayAddElement` has no size for variable-sized elements (the
  asm C stub passed none either): it serves fixed-size arrays; the kernel
  uses `ElementArrayAddElementSized` (for name arrays).
- `ChunkArrayEnum` callbacks may delete or insert elements;
  `CAH_curOffset` holds the running enumeration's position.
- An element of size 0 has the offset of the next one: a pointer cannot
  tell them apart (`ChunkArrayPtrToElement`, `ChunkArrayInsertAt`), as
  in the asm kernel.
- **`goc -cg` writes the element count** of `@chunkArray` and
  `@elementArray` (the elements come first, the header counts them with
  `sizeof`); glue did that for the 16-bit backends, whose output is
  unchanged.

ctest `kernel.carray`: fixed-size arrays (insert, delete, range, enum
with deletion, sort), a random sequence on a variable-sized array
against shadows, element arrays (references, free list reuse, merging
in `ElementArrayElementChanged`, used index/token), name arrays and
`ArrayQuickSort`. `goc.resources` reads a goc `@chunkArray` through the
C kernel.

ctest `kernel.lmem` runs a random sequence of allocations, frees,
resizes, inserts and deletes against shadow copies and validates the
block after every step (`LMemValidate`), then object blocks,
contraction, the 64K limit and the error checks. `goc.resources` builds
the blocks of its goc resources with `LMemCreateResourceBlock` and finds
every chunk through its goc chunk handle.

## 11. Timer (`Library/Kernel/Timer`, P4)

`Timer/timerList.c` and `timerInt.c`: `TimerStart`, `TimerStop`,
`TimerSleep`, `TimerGetCount` and the timer interrupt.

- **Ticks** are 1/60 s of the GHAL clock (`KernelTicks`); the interrupt
  is `GHAL_IRQ_TICK` at 60 Hz (PIT/IRQ0 on dos32, a host thread on
  linux). `TimerGetCount` and the timeouts of `ThreadPTimedSem` use the
  same clock.
- **Routine timers** (`TIMER_ROUTINE_*`, `TIMER_MS_ROUTINE_ONE_SHOT`)
  pass the routine as `destObject` and its data as `msg` under
  `GEOS_PORT32` (`TimerRoutineOptr(fn)`, `TimerRoutine(word data, dword
  tickCount)` in `timer.h`); the routine runs in interrupt context, as
  in the asm kernel. Millisecond timers are rounded up to ticks.
- **Event timers** send their message from the interrupt (section 12):
  parameters `(dword tickCount, word id)`; for continual timers the id is
  the interval, as in the asm kernel.
- **Real-time timers** (`TIMER_*_REAL_TIME`) are not there yet
  (`BAD_TIMER_TYPE`).
- Interrupt code does not free memory (DJGPP's `malloc` under a hardware
  interrupt); ended timers are freed from normal code (`TimerReap`).

**Scheduling** now follows `timerInt.asm`: every tick the running thread's
CPU usage and current priority go up; after a time slice (6 ticks) or
when a better thread became ready, the interrupt sets
`kernelReschedulePending`; every second the CPU usage of all threads is
halved and the priorities recomputed. The switch itself happens at the
next kernel entry (`KernelPreemptPoint`: `MemLock`, semaphore P,
`TimerGetCount`, ...) or at once when every thread waits (idle).
GHAL-Concept §4.1: soft preemption first; hard preemption of threads
that never enter the kernel is not needed so far.

ctest `kernel.timer`: sleep, one-shot and continual routine timers,
`TimerStop` (found, ended, wrong id), millisecond timers, an interrupt
waking a blocked thread, event timers through the hook, and two
computing threads of the same priority sharing the CPU by time slices
(with rising priority) while a better thread sleeps and comes back.

## 12. Events, queues and messages (`Geodes/geodesEvent.c`, `Object/objectClass.c`, P4)

**Events** are `SIG_EVENT_REG` handles (`SIG_EVENT_STACK` when the
parameter structure is longer than the 32 bytes kept in the event) with
destination, message and a copy of the parameter structure of section 5.
**Queues** are `SIG_QUEUE` handles: events linked through the handle
table, a semaphore counting them, the attached thread.

**Routing** (`CObjMessageP`, as `ObjMessage`): the destination's handle
names the thread that runs it: a thread; a queue (its attached thread);
a memory handle: `HM_otherInfo` (the burden thread of an object block,
the first thread of a process core block). Messages for the running
thread are direct calls unless `MF_FORCE_QUEUE`; others become events.
`MF_CALL` to another thread queues the event and blocks until the
receiver has dispatched it; the receiver writes the reply straight into
the caller's buffer. A message to optr 0 is ignored.

- Supported flags: `MF_CALL`, `MF_FORCE_QUEUE`, `MF_RECORD`,
  `MF_INSERT_AT_FRONT`, `MF_CHECK_DUPLICATE` with `MF_CHECK_LAST_ONLY`,
  `MF_REPLACE` and `MF_DISCARD_IF_NO_MATCH` (a duplicate has the same
  message and destination), `MF_DISPATCH_DONT_FREE`. `MF_CUSTOM` (a
  compare routine passed on the stack) is not supported yet. `MF_STACK`
  and `MF_FIXUP_*` mean nothing here (parameters are always a structure,
  there are no segments).
- Interrupt code may send with `MF_FORCE_QUEUE` (event timers): events
  come from a reserve that normal code refills, handles only from the
  free list; when both are empty the event is dropped and counted.
- `ThreadAttachToQueue(queue, class)` makes the running thread dispatch
  its queue for good; messages to the thread, its queue or its process
  (chunk 0) go to `class`.
- `ThreadDestroy` sends `MSG_META_ACK(exitCode, thread:ackData)` to its
  ack object, as in the asm kernel.
- API: `GeodeAllocQueue`, `GeodeFreeQueue`, `GeodeInfoQueue`,
  `GeodeFlushQueue`, `QueuePostMessage`, `QueueGetMessage`,
  `CMessageDispatchP`, `ObjDispatchMessage`, `ObjFreeMessage`,
  `ObjGetMessageInfo`, `MessageSetDestination`, `ObjDuplicateMessage`,
  `ObjSetEventInfo`. `ObjGetMessageData` has no registers to return.

**Method calls** (`ObjCallMethodTable`): from the object's class up the
superclasses, through the thunks of section 5. `pself` is the chunk, or
for a class with a master level the master part
(`Class_masterOffset`). As in the asm kernel:

- a master part that is not built yet is built when a class of its
  level is reached (`ObjInitializeMaster`: `ObjResizeMaster` grows it
  to `Class_instanceSize`, then `MSG_META_INITIALIZE` to that class);
- `MSG_META_INITIALIZE` is not inherited across a master class;
- above a variant master class, the superclass is the class pointer at
  the start of its master part. While it is 0, the object gets
  `MSG_META_RESOLVE_VARIANT_SUPERCLASS(masterOffset)` and the answer is
  stored there.

Also `ObjDeref`, `ObjDeref1`, `ObjDeref2`, `ObjIsClassADescendant`,
`ObjResizeMaster`, `ObjInitializeMaster`.

**The base structure of an object** (class pointer, one word per master
level) has no padding: `goc -cg` declares the `<Class>Base` types
`GOCR_PACKED`, and `Class_masterOffset` is `sizeof(ClassStruct *) +
2 * (level - 1)` (8, 10, ... on 64-bit hosts; 4, 6, ... on 32-bit ones,
as in 16-bit GEOS). Before this, the C compiler padded the nested base
structures on 64-bit hosts, and goc wrote the 16-bit offsets 4, 6, ...

`MetaClass`, `ProcessClass`, `ObjInstantiate`, variable data and
relocation, composites: section 13.

ctest `kernel.msg` (with `Goc/kmsg.goc`, classes and objects from
`goc -cg`):

- direct calls to an object block of the running thread;
- a variant superclass resolved by the object, with the master part
  above it built and initialized once, and an override that calls its
  superclass;
- a process thread on its own queue, with order and
  `MF_INSERT_AT_FRONT`, duplicate checking with replace, and
  `@record`/`@dispatchcall` with a duplicated event;
- remote calls to an object block run by the other thread;
- an event timer sent from the timer interrupt;
- `GeodeFlushQueue` of one destination;
- `MSG_META_ACK` from `ThreadDestroy`.

`goc.resources` keeps its stand-in message kernel (fake handles show where
goc sends to), under its own names.

## 13. Objects (`Library/Kernel/Object`, P4)

**Layout of an object chunk.** Optrs are 8 bytes and pointers are 8 bytes
on 64-bit hosts, and armhf traps on some misaligned 64-bit accesses. So
everything in an object that holds them is aligned to 8. The chunk data
is 8-aligned (section 10). Then:

```
base structure   class pointer, one word per master level (packed, sec. 12)
master parts     each at an offset aligned to 8, occupying its
                 Class_instanceSize rounded up to 8, in level order
variable data    from the next offset aligned to 8 to the end of the chunk
```

- **`goc -cg`** marks the master parts and the variable data area of a
  resource object `GOCR_PART` (`__attribute__((aligned(8)))`), so its
  static objects have this layout. goc builds every master part that
  lies below an unresolved variant; the parts above one stay 0 and are
  built later.
- **`ObjResizeMaster`** builds a part where the next built part starts
  (parts are built from the last level up), or else after the base
  structure. It grows or shrinks by multiples of 8, so the later parts
  and the variable data stay aligned.
- Without master levels, the instance is the class's
  `Class_instanceSize` bytes from the chunk start (class pointer
  included), and variable data follows at the next offset aligned to 8.

**Variable data.** Each entry is a `VarDataEntry` and its data:

```
VDE_reserved[2]   4 bytes, 0
VDE_dataType      type | VarDataFlags
VDE_entrySize     8 + size of the data (always present)
data              8-aligned
```

The next entry starts at the next offset aligned to 8. Type and size
come right before the data, as in 16-bit GEOS (`VEDP_dataType` -4,
`VEDP_entrySize` -2), so the `VarData*Ptr` macros keep working.
`VarDataSizePtr` returns the size of the data under Port32. The 16-bit
macro returned the entry size, header included, and the callers in the
tree use it as the data size. goc writes each entry as an aligned
structure `{0, type|flags, sizeof(VarDataEntry)+sizeof(data), data}`.
Pointers to variable data point to the data, as in the asm kernel.
Where the C API passes such a pointer as a word (`ObjVarDeleteDataAt`,
the reply of `MSG_META_INITIALIZE_VAR_DATA`), the word is its offset
from the start of the block, like a 16-bit near pointer.

API: `ObjVarAddData` (an existing entry of another size moves to the
end, the data is cleared), `ObjVarFindData`, `ObjVarDerefData` (asks
the object with `MSG_META_INITIALIZE_VAR_DATA`), `ObjVarDeleteData`
(TRUE if deleted), `ObjVarDeleteDataAt`, `ObjVarScanData`,
`ObjVarDeleteDataRange`, `ObjVarCopyDataRange`.

**Relocation** (`objectReloc.c`). In an unrelocated optr or hptr, the
handle holds a resource number of the block's owner with
`GOCR_UNRELOCATED` set (section 4). Relocated, it holds that resource's
handle. `GOCR_PROCESS_RES` stands for the process.
`ObjRelocateObjBlock`/`ObjUnRelocateObjBlock` do the block output and
every object (`OCF_IS_OBJECT`) through its classes, as the asm kernel
does:

- `Class_relocTable` entries give an offset in the class's master part
  (skipped while the part is not built). goc makes them for `optr`,
  link and composite instance variables, and for `@reloc`.
- `Class_vdRelocTable` entries give a vardata tag and an offset in its
  data. They apply only to objects marked `OCF_VARDATA_RELOC`.
- A class with a relocation method (`@method XClass, _reloc`,
  `CLASSF_HAS_RELOC`) gets the call instead of its tables. Its
  `@callsuper()` is `ObjRelocOrUnRelocSuper`, which does the class's
  tables and goes on up. The method's "frame" word carries the
  `VMRelocType` and whether it unrelocates.

Table entries point to the handle: under `goc -cg`, the handle half of
an optr (`+GOCR_OPTR_HANDLE_OFS`, 4 on the little-endian hosts) or the
hptr itself. The 16-bit backends keep `+2`. Pointers
(`RELOC_RELOC_ENTRY_POINT`: classes, routines) need nothing at load
time: the module loader made them absolute. The relocation types of
resources are `VMRT_RELOCATE_FROM_RESOURCE` and
`VMRT_UNRELOCATE_FROM_RESOURCE`; the other `VMRelocType`s unrelocate
for a file (section 13d). goc casts a relocation
method to `MessageMethod *` through `void (*)(void)`, because its
prototype has `word` parameters. The kernel calls it with that
prototype (`ObjRelocMethod`).

Until `GeodeLoad` exists, the code that builds a geode's resources
registers their handles with `GeodeSetResourceHandles`
(`Geodes/geodesResource.c`). Entry 0 is the process, as in
`__goc_resource_handles`.

**MetaClass and ProcessClass** are written by hand in C
(`objectMeta.c`, `objectProcess.c`): a message table and thunks that
take the parameter structures of section 5. Their message numbers
(`KMSG_*` in `kernelC.h`) are checked against goc's by `kernel.obj`.

- `MetaClass`: `GET_CLASS`, `IS_OBJECT_IN_CLASS`, `GET_OPTR`,
  `BLOCK_FREE`, `OBJ_FREE`/`FINAL_OBJ_FREE`, `DEC_BLOCK_REF_COUNT`,
  `SET_FLAGS`/`GET_FLAGS`, `DISPATCH_EVENT`, `SEND_CLASSED_EVENT`
  (self if of the event's class, block output, process, or dropped),
  `ADD_VAR_DATA`, `DELETE_VAR_DATA` (TRUE if not found, as the carry
  was), `INITIALIZE_VAR_DATA`, `GET_VAR_DATA` (`0xffff`: not found),
  `SET_OBJ_BLOCK_OUTPUT`/`GET_OBJ_BLOCK_OUTPUT`.
- `ProcessClass`: `MSG_PROCESS_INSTANTIATE` (`{MemHandle, ClassStruct
  *}`, replies with the chunk), `MSG_PROCESS_INSTANTIATE_FOR_THREAD`,
  `MSG_PROCESS_FINAL_BLOCK_FREE`.
- `ObjInstantiate` allocates only the base structure for a class with
  master levels; parts are built on demand. Without master levels it
  allocates the whole instance and sends `MSG_META_INITIALIZE`. A block
  run by another thread is passed to that thread with
  `MSG_PROCESS_INSTANTIATE`. `ObjInstantiateForThread` keeps the
  thread's blocks of up to 4K (`T_dynamicObjBlock`).
- Freeing (`ObjFreeChunk`, `MSG_META_OBJ_FREE`, `ObjFreeObjBlock`)
  works as in the asm kernel. An object of a resource keeps its handle
  with no data and is marked dirty. A block in use (`OLMBH_inUseCount`)
  is only marked `LMF_AUTO_FREE`. The final message goes through the
  queue of the thread that runs the object. Without the input manager
  there is no flush of the input queues yet.
- Also: `ObjGetFlags`, `ObjSetFlags`, `ObjMarkDirty`,
  `ObjInitializePart`, the in-use and interactible counts (the latter
  notify the block output), `ObjBlockSetOutput`/`GetOutput`,
  `ObjLockObjBlock`, `ObjTestIfObjBlockRunByCurThread`,
  `ObjIsObjectInClass` (follows resolved variants).
- `MemAllocLMem` zeroes its block, so the rest of an
  `ObjLMemBlockHeader` (counts, output) starts at 0.

**Composites** (`objectComposite.c`, `objectLinkable.c`) work as in
the asm kernel. A composite's `CompPart` holds its first child. Each
child's `LinkPart` holds the next sibling, and the last child holds the
parent with `LP_IS_PARENT` set in the chunk. goc writes these links the
same way, unrelocated, and the class relocation tables cover them.

- The routines take the C API's offsets: the master offset, then the
  offsets of the parts in that master part.
- Children may lie in other blocks.
- `ObjCompAddChild` builds the master part of the composite and of the
  child if they are not built yet.
- `ObjCompProcessChildren` takes the next sibling before the callback,
  so a callback may remove its child. Without a callback, only
  `OCCT_COUNT_CHILDREN` exists in C (the count is added to
  `*(word *) cbData`). The other call types send a message in
  registers, and C uses `CObjSendToChildren` (`@send @children`)
  instead.
- `ObjLinkFindParent` is `@parent`.
- The register routines `ObjLinkCallParent`, `ObjLinkCallNextSibling`
  and `ObjSwapLockParent` have no C form.

**GCN lists** (`LMem/lmemGCNList.c`) work as in the asm kernel. This
covers the system lists, lists in a block, single lists, status
events, relocation, and `MetaClass`'s `MSG_META_GCN_LIST_*` with
`TEMP_META_GCN`.

- The Port32 `GCNListHeader` (`metaC.goh`) holds the status event as a
  32-bit `EventHandle`. Reserved words align the elements (optrs) to 8,
  so the header is 24 bytes.
- The objects on a list are taken from it before the first one gets the
  event, so a handler may change the list. The system lists are
  unlocked by then.
- Each object gets one reference to the data block, which
  `MSG_META_NOTIFY_WITH_DATA_BLOCK` gives back. The sender's reference
  is given up at the end.
- A status that is the same as the last one is not sent again: the same
  message and parameters, or a data block with the same bytes.
- Not yet: handing the events and blocks of system lists to the kernel
  geode (comes with geode exit).

`ObjSetFlags` and `ObjMarkDirty` do nothing in a block without chunk
flags, as in the asm kernel. GCN lists also live in plain LMem blocks.

**Detach and quit** (`objectShutdown.c`) work as in the asm kernel:

- `ObjInitDetach`, `ObjIncDetach` and `ObjEnableDetach` keep
  `DETACH_DATA`. `MSG_META_ACK` and `SHUTDOWN_ACK` count down, and at 0
  the object gets `MSG_META_DETACH_COMPLETE` (or `SHUTDOWN_COMPLETE`).
  `MetaClass` then acks the asker through the queue.
- An object that never called `ObjInitDetach` acks when
  `MSG_META_DETACH` or `APP_SHUTDOWN` reaches `MetaClass`. An ack to an
  object that is not detaching (a thread's exit, for instance) is
  ignored.
- Under Port32 the complete messages carry `(word callerID, optr
  ackOD)` as parameters, because the asm kernel passed them in
  registers. `metaC.goh` declares them with `@ifdef GEOS_PORT32`, which
  `goc -cg` predefines.
- Quit: `MSG_META_QUIT` (`MSG_META_QUIT_OBJECT`: the optr to ack) is
  acked with `MSG_META_QUIT_ACK`. An object with an active list
  (`MGCNLT_ACTIVE_LIST`) sends `MSG_META_QUIT` to each object on it in
  turn, keeps the ones it has asked in `TEMP_META_QUIT_LIST`, and ends
  with `MSG_META_FINISH_QUIT`. An abort ends the sequence.
- `ProcessClass`: `MSG_META_DETACH` queues `MSG_PROCESS_EXIT` with the
  same parameters, and that calls `ThreadDestroy(callerID, ackOD, 0)`.
  The ack data the asm kernel passed in SI is 0.

Aliases (`@alias`) have parameters of their own, and the method gets
the parameters of whichever alias the sender used. A method handles
the alias it expects (`@method X, MSG_META_QUIT_OBJECT`). `@callsuper()`
passes the parameters of the message the method was declared for.

ctest `kernel.obj` (with `Goc/kobj.goc`) covers:

- the layout goc made: aligned parts and variable data, unrelocated
  optrs, string data, data of 4 bytes and entries without data;
- relocation across two resources and to the process, through instance
  and vardata tables, an hptr and a relocation method;
- unrelocation back to the bytes goc made;
- variable data through the API and the `MetaClass` messages;
- `ObjInstantiate` of both kinds of class, with parts built after
  variable data was added;
- the `MetaClass` messages and classed events;
- remote `ObjInstantiate` and `ObjInstantiateForThread`;
- `MSG_META_OBJ_FREE`, and `ObjFreeObjBlock` with and without an in-use
  count;
- a static tree of composites across two resources: links as goc made
  them, `@children`, `@parent`, finding, counting, callbacks that stop
  or start at a child, removing, adding at every position, moving, and
  new objects whose parts are built when they are added;
- GCN lists: adding, sending and removing on the system lists (also a
  block's objects at once); data block references; status events kept,
  sent to new objects, not sent again when unchanged, and replaced; an
  object's lists through the `MetaClass` messages; unrelocating and
  relocating them, with and without `GCNLTF_SAVE_TO_STATE`;
- detach with acks from other objects, an object that acks at once, app
  shutdown, quit over an active list, a quit that one object refuses,
  and detaching a process (its thread exits and acks);
- goc's numbers for the new messages and vardata tags, and the sizes of
  the GCN and detach structures.

**Duplicating resources** (`objectDup.c`).

- `ObjDuplicateResource` builds the copy from the module data of the
  resource: the `GocResource` of its owner, as the asm kernel read the
  geode file (`GeodeDuplicateResource` does the same without
  relocating). The source block itself is not used, so it may be in
  memory.
- The copy is relocated like a resource when it is loaded (chunk
  shadows, then the objects). An object block knows the resource it was
  built from: `OLMBH_resource` is a Port32 field of
  `ObjLMemBlockHeader` that goc fills in. An optr to that resource is
  the block itself. This makes references within a template lead to
  the copy, as glue's `ORS_CURRENT_BLOCK` did.
- Other resources are taken from the template's owner, unless the new
  owner runs the same geode (it has the same permanent name). In that
  case they are taken from the new owner.
- The owner and the thread can be given, or taken from the running
  thread (0) or from the source (-1). A source run by its process's
  first thread gives the new owner's first thread.
- The copy gets `LMF_DUPLICATED` and loses `LMF_IN_RESOURCE`.
- **The saved-block list.** A copy of a detachable resource, and a block
  given to `ObjSaveBlock`, goes onto its owner's saved-block list. Each
  entry is a `SIG_SAVED_BLOCK` handle: `HE_otherInfo` is the block,
  `HE_size` the VM ID and `HE_next` the next entry. The list starts at
  `GeodeCore.GC_savedBlocks`.
- `ObjFreeDuplicate` takes the block off the list and frees it. When a
  geode is freed, its entries are freed as well.

Not yet:

- Saving to state (`objectState.asm`, with VMem): the VM blocks of the
  saved blocks, and unrelocating the optrs in chunks that are not
  objects. Those optrs are relocated when a resource is loaded or
  duplicated (shadows, section 4).

## 13a. Files (`Library/Kernel/File`, P4)

The file routines of the kernel resolve a path to a disk and the path
on it. They call the file system driver of that disk for everything
that touches the medium (section 13f). The GEOS top directory is drive
`C` of the HostFS driver: a directory on the host. The kernel is told
where it is with `FileSetTopHostPath`.

- **Paths** (`filePath.c`).
  - A standard path is a directory below the top, named as in 16-bit
    GEOS (`WORLD`, `DOCUMENT`, `SYSTEM\FS`, `PRIVDATA\STATE`, ...,
    after `stdPathUpwardTree`). A DOS16 installation keeps its layout.
  - A path that starts with `\` is relative to the root of the current
    disk; for a standard path that is the top. A path that starts with
    a drive name (`C:`) is relative to the root of that drive's disk.
    Any other path is relative to the current path. `..` may leave a
    standard path; the current path is then the top plus the
    directories below it. Going above the root is
    `ERROR_PATH_NOT_FOUND`. An unknown drive or disk is
    `ERROR_INVALID_VOLUME`.
  - Every thread has a current path: a `StandardPath` or a disk
    handle, and a tail.
    `FileGetCurrentPath` returns them. A new thread starts with its
    creator's path, the first thread at `SP_TOP`.
    `FilePushDir`/`FilePopDir` keep a stack; a pop without a push is
    fatal.
  - A standard path need not exist on the host. A directory below it
    must exist.
- **Standard paths over several directories** (`filePath.c`, after
  `GetPaths` of `Loader/path.asm` and `Int_SetDirOnPath`).
  - `[paths]` in the init files gives a standard path further
    directories. The key is made of the names from the top down, in
    lower case, as the loader's: `document`, `systemfs`,
    `userdatafont`, and `top` for SP_TOP. The values of all init files
    count. The directories are separated by blanks or line ends (a blob
    for several lines).
  - Each directory is a GEOS path with a drive name (`D:\DOCS`). DOS16
    used DOS paths; here a drive must exist (`FSDInitDrive`). An entry
    on an unknown drive is skipped.
  - The places of a path in standard path S, in order:
    1. the directory below the top;
    2. the `[paths]` directories of S;
    3. those of the standard paths above S, each with the names down to
       S (`top = D:\NET` gives `D:\NET\DOCUMENT` to DOCUMENT).
  - An existing file or directory is found at the first place that has
    it (`FileLocate`). This holds for open, delete, rename (the new name
    goes to the same directory), attributes, extended attributes,
    `VMOpen`, and the source of `FileCopy`/`FileMove`.
  - A new file or directory goes to the first place whose directory
    exists (`FileOpOnPath`, after `FileOpOnPathLow`). If no place has
    it, the error is `ERROR_FILE_NOT_FOUND` when some place had the
    directory, else `ERROR_PATH_NOT_FOUND`.
  - A current path below a standard path must exist at one of its
    places.
  - `FileEnum` enumerates every place. A name found at an earlier place
    hides the same name at later ones. `FEA_PATH_INFO` has
    `DPI_EXISTS_LOCALLY` for the first place, and the place number in
    `DPI_ENTRY_NUMBER_IN_PATH` for the others.
  - `FileInitStdPaths` reads `[paths]`; the boot will call it after the
    init file and the drives.
- **Open files** (`fileOpenClose.c`, `fileIO.c`).
  - A `FileHandle` is a `SIG_FILE` handle owned by the process of the
    opening thread. Its `HE_addr` is a `KFile` holding the driver's
    open file (`FSDFile`). `FileGetDiskHandle` returns its disk.
  - `FileDuplicateHandle` gives a second handle to the same `KFile`,
    so the position is shared, as in DOS.
  - Files a geode still has open are closed when it is freed.
  - A new file created read-only can still be written through its
    handle; its attributes are set when it is closed (DOS semantics).
    `FileSetDateAndTime` works on the open file.
  - A short read or write sets `ERROR_SHORT_READ_WRITE`. With
    `noErrorFlag`, other errors are fatal.
- **By name** (`fileFile.c`): delete, rename (the new name stays in the
  old directory unless it has a path; it never replaces a file),
  attributes, and create/delete directories (the current directory
  cannot be deleted).
- **Copy and move** (`fileFile.c`, after `FileCopyCommon`,
  `FileMoveCommon`). A source or destination disk of 0 means the
  current path. With a disk, the path is relative to its root; a
  standard path given as the disk means the path is relative to that
  standard path.
  - `FileCopy` copies the data through both drivers, and then the
    header attributes of a GEOS file (all but the name), the date and
    the DOS attributes. It replaces an existing file; a copy onto
    itself does nothing.
  - `FileMove` refuses an existing destination (`ERROR_FILE_EXISTS`).
    Within a disk, the driver moves the file (`FSD_rename`). Between
    disks, it copies and deletes, and a directory gets
    `ERROR_DIFFERENT_DEVICE`. `FileRename` between disks gives the
    same error.
  - `FileCopyLocal` and `FileMoveLocal` take the source only from the
    directory below the top, not from the other places of a standard
    path.
- **GEOS files**, extended attributes and host names: section 13c.
- **Errors.** Every routine sets the thread's error (`ThreadGetError`).
  A missing file is `ERROR_FILE_NOT_FOUND`. If its directory is
  missing too, the error is `ERROR_PATH_NOT_FOUND`.

Not yet:

- deny modes and record locks;
- links, temporary files, and change notification;
- `SPA_DOES_NOT_EXIST_LOCALLY` (every standard path has its place
  below the top).

## 13b. VM files (`Library/Kernel/VMem`, P4)

**Format: DOS16 on every host (decided 2026-11-11).** VM files use the
format of 16-bit GEOS, on every host:

- the `VMFileHeader` at position 0 of the file data;
- the blocks, each rounded up to 16 bytes;
- the VM header block. Its table holds 12-byte entries from offset
  0x20, and a `VMBlockHandle` is the offset of its entry.

So block handles inside documents stay valid, and files move freely
between hosts. The header is kept in memory as it is in the file
(little endian, `vmemInt.h`). The memory of each block is kept beside it
(`KVMFile.VF_mem`), because the 16-bit `VMBH_memHandle` cannot hold a
32-bit handle; that field is 0 in the file. A VM file is a GEOS file
of type `GFT_VM` (section 13c): the VM data follow the 256-byte
`GeosFileHeader`, and the VM positions count from there, as on DOS16.

- **Block table** (`vmemBlkManip.c`), as in `vmemBlkManip.asm`:
  - used entries;
  - assigned free entries: free space, sorted by position, with
    neighbours merged;
  - unassigned entries, cleared;
  - the table grows by 4 entries when needed.
  
  A block is written in place if it still fits; any rest becomes free
  space. Otherwise it goes to the first free space that is large
  enough, then the free space at the end of the file, then the end of
  the file.
- **Header** (`vmemHeader.c`): the header is the block of the first
  entry. The `VMFileHeader` repeats that entry (signature, size,
  position). On open, the used size is computed again and the old
  memory handles are cleared.
- **API** (`vmemHigh.c`, `vmemChain.c`):
  - **Handles.** A `VMFileHandle` is a `SIG_VM` handle owned by the
    opening process; it owns the memory of its blocks. `MemOwner` of a
    block returns the file's owner, as in `heapHigh.asm`. Opening an
    open file returns the same handle again.
  - **Blocks** are read when first locked. `VMUpdate`/`VMClose` write
    the dirty blocks, then the header.
  - Implemented: `VMOpen` (open, create, create-only, truncate,
    temporary file, read-only, with the `VM_OPEN_OK_*`/`VM_CREATE_OK`
    status), `VMClose`, `VMUpdate`, `VMAlloc`, `VMFree`, `VMLock`,
    `VMUnlock`, `VMDirty`, `VMFind`, `VMInfo`, `VMModifyUserID`, map
    block, attributes, `VMGetDirtyState`, `VMMemBlockToVMBlock`,
    `VMVMBlockToMemBlock`, `VMAttach`, `VMDetach` (the file keeps the
    contents), `VMPreserveBlocksHandle`, `VMCopyVMBlock`, and the
    chains and trees of blocks (copy, free, compare, info).
  - VM files a geode still has open are closed when it is freed.
- **Checks:**
  - `Port32/tools/vmcheck.py` reads the format independently. It finds
    all 823 `.sym` files of the 16-bit tools sound.
  - ctest `kernel.vm.format` runs it in strict mode on the files the
    kernel writes.
  - `kernel.vm` reads a tools-written file (`vm_reference.h`) on every
    host, DOS included, and changes it.

- **LMem blocks** (`VMAllocLMem`, or an LMem block attached) are
  written in the **DOS16 LMem layout** and converted back on reading
  (`LMemToDOS16`/`LMemFromDOS16` in `lmemCode.c`; decided 2026-11-11:
  the Port32 `LMemBlockHeader` stays 20 bytes in memory):

  | DOS16 in the file | Port32 in memory |
  |---|---|
  | `LMemBlockHeader`, 16 bytes (`LMBH_handle` 0) | 20 bytes |
  | rest of a larger header (e.g. `ObjLMemBlockHeader`) as it is | after the 20 bytes |
  | handle table at `LMBH_offset` | at `LMBH_offset + 4` |
  | chunks in handle order right after the table, size word = data + 2, slots rounded to 4, no free chunk | chunk data aligned to 8, one free chunk at the end |

  The entry gets `VMBF_LMEM`. On reading, the block gets
  `LMBH_handle`, `LMF_IS_VM` and `HF_LMEM`; `VMAttach` sets
  `LMF_IS_VM`, `VMDetach` clears it. Blocks with `LMF_NO_HANDLES` are
  written as they are. The chunk handles of a file are 4 lower than in
  memory: chunk handles stored inside chunk data (DB items, element
  array references) and the rest of larger headers whose layout
  differs are left to the converters of section 14.2.
- **Relocation** (`CallReloc` in `vmemBlkManip.c`, as `CallVMReloc`):
  before a block is written (`VMRT_UNRELOCATE_BEFORE_WRITE`), after
  it was written (`VMRT_RELOCATE_AFTER_WRITE`) and after it was read
  (`VMRT_RELOCATE_AFTER_READ`). An object block of a file with
  `VMA_OBJECT_RELOC` is relocated by the kernel (section 13d; after
  reading, the in-use and interactible counts are cleared and the
  block gets the thread of `VMSetExecThread`); any other block goes to
  the routine of `VMSetReloc`.

- **Huge arrays** (`vmemHugeArray.c` beside `vmemHugeArray.asm`): the
  DOS16 structure in the file. The directory block (user ID
  `SVMID_HA_DIR_ID`) has `HugeArrayDirectory` (`HAD_data`, `HAD_dir`,
  `HAD_xdir`, `HAD_self`, `HAD_size`), the user's part of the header,
  and a chunk array of `HugeArrayDirEntry` {`HADE_last`, `HADE_size`,
  `HADE_handle`} after a first entry with `HADE_last` -1. The data
  blocks (`SVMID_HA_BLOCK_ID`) have `HAB_next`, `HAB_prev`, `HAB_dir`
  after the LMem header and one chunk array of the elements (element
  size `HAD_size`, 0: variable). In the file `LMBH_handle` of these
  blocks is the chain link (`HAD_data`, `HAB_next`), as the asm left
  it on unlocking, and `HAD_dir` a file chunk handle (4 lower); the
  write and read of the block convert. `HugeArrayDirectory` is 32
  bytes in C (2 bytes padding after `HAD_size`), 26 in DOS16: the
  padding is a gap of the header that is not written
  (`LMemToDOS16`/`LMemFromDOS16` take it), so the user's part of the
  header (`EB_bm` of a bitmap) is at 26 in the file as in DOS16; the
  chunk handles of directory blocks are 6 lower in the file. The chain routines
  (`vmemChain.c`) take the link from `HAD_data`/`HAB_next`; a copied
  array gets its own handles (`FixupHugeArrayChain`), arrays are
  compared element by element. Blocks: an insertion that keeps a
  block within 6000 bytes goes into it, a larger one splits the block
  and adds blocks of about 4000 bytes (fixed size) or one element
  each (variable size); neighbours that fit within 6000 bytes are
  combined after every change, not only around the place of the
  change as in the asm, so blocks may be cut differently (the format
  allows that). `HADE_size` is the block's DOS16 size.
  `ECCheckHugeArray` checks nothing yet; `vmcheck.py` checks the
  structure in files.

Not yet:

- backup mode (`VMSave`, `VMRevert`, `VMSaveAs`);
- sharing between processes, `VMGrabExclusive`;
- discarding clean blocks;
- DB items, compression.

## 13d. Object blocks in files (`Object/objectReloc.c`, P4)

An object block written to a VM file is unrelocated completely, as the
asm kernel does (`VMObjRelocOrUnReloc`, `UnRelocateObjBlock`). A
reference becomes `GOCR_UNRELOCATED | source << 28 | index`
(`KUR_*` in `kernelC.h`):

| Source | Index | 16-bit counterpart |
|---|---|---|
| `KUR_RESOURCE` 0 | resource number of the owner (what `goc -cg` writes) | `ORS_OWNING_GEODE` |
| `KUR_SAVED` 1 | VM ID of a saved block (saved-block list) | `ORS_VM_HANDLE` |
| `KUR_VM` 2 | VM block handle in the same file | `ORS_NON_STATE_VM` |
| `KUR_UNKNOWN` 3 | handle >> 4 | `ORS_UNKNOWN_BLOCK` |
| `KUR_ENTRY_OWNER` 4 | export number of the owner | `ORS_OWNING_GEODE_ENTRY_POINT` |
| `KUR_ENTRY_LIBRARY` 5 | library number << 16, export number | `ORS_LIBRARY` |
| `KUR_ENTRY_KERNEL` 6 | entry number in `geos.gp` | `ORS_KERNEL` |

Handles are optrs, hptrs and the block output. Pointers are the class
of every object (relocated first, unrelocated last, as
`RelocOrUnRelocAndLoadClass`), the superclass of a variant master
part, and `RELOC_RELOC_ENTRY_POINT` entries: they must be **exported
entries** of the owner, one of its libraries or the kernel
(`GeodeEntryToReloc`/`GeodeRelocToEntry` in `geodesLoad.c`), so a
class whose objects are saved is exported, as in 16-bit GEOS. The
numbers do not change when a geode or the kernel grows: the export
number of a geode is its `.gp` order, and each kernel export carries
its entry number in `geos.gp` (`kexports.py` counts exports and `skip`
as glue does). Without a kernel export table (Open Watcom) the kernel
knows the numbers of `MetaClass` and `ProcessClass`. Relocation methods see the same
references through `ObjDoRelocation`/`ObjDoUnRelocation` (a per-thread
flag, `KThread.T_fileReloc`).

The instance data keep the Port32 layout of the host ABI (64-bit optrs,
8-byte pointers on 64-bit hosts), so such object blocks are not
portable between hosts with different pointer sizes; portable
documents do not store objects (section 14.2).

ctest `kernel.geode`: the process `kgapp` saves objects of its own
class and of `kglib`'s class with references in the same block, to
another VM block and to a resource, closes the file, opens it again,
and sends them messages.

## 13e. The state file (`Object/objectFile.c`, P4)

As `objectFile.asm`. The routines are asm-only in 16-bit GEOS (the UI
calls them); in C:

```c
Boolean ObjAssocVMFile(VMFileHandle file, MemHandle *extra); /* TRUE: protocol error */
void ObjDisassocVMFile(void);
void ObjCloseVMFile(void);
void ObjSaveExtraStateBlock(MemHandle mh);
VMBlockHandle ObjMapSavedToState(MemHandle mh);
MemHandle ObjMapStateToSaved(VMBlockHandle vmbh, GeodeHandle gh);
```

The process keeps its state file in `GeodeCore.GC_vmHandle`
(`PH_vmHandle`); the blocks to save are its detachable object
resources and its saved blocks (`GC_savedBlocks`: duplicated detachable
resources and `ObjSaveBlock`). The file is a VM file in the DOS16
format (section 13b) with the asm kernel's user IDs: the resource
number for a resource, `VM_SAVED_UI`/`VM_SAVED_APP` for saved blocks
(who runs them), `VM_EXTRA_DATA` for the extra block. A new file gets
`FEA_PROTOCOL`/`FEA_RELEASE` of the process and a map block with the
protocols of its libraries; an old one must match them.

- **Saving** (`ObjDisassocVMFile`): each block is written as a copy,
  compacted as `CompactObjBlock` does (clean resource chunks become
  empty, chunks neither dirty nor from the resource go, master parts of
  `CLASSF_DISCARD_ON_SAVE` classes above a variant go, variable data
  without `VDF_SAVE_TO_STATE` goes) and unrelocated for a file (section
  13d). The blocks stay in memory: the process is about to exit.
- **Restoring** (`ObjAssocVMFile`): the asm kernel did this when a
  block was first locked (`FullObjLock`), because its resources came
  from the geode file then. Port32 builds resources when the geode is
  loaded, so it is done at once: the saved blocks come back as blocks
  of the process (VM ID = their VM block handle); then each resource
  with saved chunks is unrelocated, the chunks are merged in with the
  same chunk handles (`MergeObjBlock`, `LMemAllocAt`), and the resource
  is relocated again.
- A saved block made while a state file is open gets a VM block at
  once (`AllocEmptyVMBlock`); one made before gets an odd ID, replaced
  by a VM block in `ObjAssocVMFile`.

The instance data keep the layout of the host ABI, as in section 13d:
a state file belongs to one host ABI.

ctest `kernel.geode`: `kgapp` attaches to a new state file, changes an
object of a resource, adds an object to it and saves on
`MSG_META_DETACH` with an extra block; loaded again, it finds the
changed and the added object, the unchanged chunks of the resource,
its static GCN list and the duplicated block with its references.

## 13c. GEOS files and host names (`Driver/IFS/HostFS`, the HostFS driver, P4)

**The header is in the file on every host (decided 2026-11-11).** A
GEOS file (created without `FCF_NATIVE`) starts with the 256-byte
`GeosFileHeader` of DOS16 GEOS (`Include/Internal/fileStr.def`): the
signature, the GEOS name, type, flags, release, protocol, token,
creator, user notes, notice, creation date, password and desktop info.
The same file therefore works on DOS16, dos32 and Linux, copied either
way. Host extended attributes (xattrs) are not used: they would be lost
on the way to a FAT disk.

- **Data.** A handle of a GEOS file reads and writes the data after the
  header (`KFile.KF_base`). `FilePos`, `FileSize` and `FileTruncate`
  count from there; the position never goes into the header. Truncating
  keeps the header. `FileOpen` takes any file that starts with the
  signature as a GEOS file. `FileCreate` of an existing file of the
  other kind is `ERROR_FILE_FORMAT_MISMATCH`.
- **Extended attributes** (`File/fileEA.c` over the driver, kept by HostFS): `FileGet/SetPathExtAttributes`,
  `FileGet/SetHandleExtAttributes`, one attribute or `FEA_MULTIPLE`
  (the first error is returned, the others are still done).
  - The header fields are read and set as they are in the file (little
    endian). Strings may be shorter than their field.
  - `FEA_MODIFICATION`, `FEA_FILE_ATTR`, `FEA_SIZE` (the data),
    `FEA_FILE_ID` and `FEA_DOS_NAME` (the host name) come from the host.
  - A native file has no header attributes: getting one is
    `ERROR_ATTR_NOT_FOUND`, setting one `ERROR_ATTR_NOT_SUPPORTED`. Its
    `FEA_FILE_TYPE` is `GFT_NOT_GEOS_FILE`, its `FEA_NAME` the host name.
  - `FEA_NAME` is changed by `FileRename` only
    (`ERROR_ATTR_CANNOT_BE_SET`). `VMOpen` sets `GFT_VM` and the flags,
    as `VMCreateFileHeader` did.
- **Host names** (`Driver/IFS/HostFS`). Three layouts:
  - *DOS names* (dos32; `HostFSSetDosNames`): as the DOS file system
    driver (`dosVirtual.asm`, `dosPath.asm`). A GEOS file gets a
    generated 8.3 name: the first eight characters, illegal ones as `_`,
    upper case, then `.000`, `.001` and so on. A directory whose GEOS name
    is not exactly a DOS name gets one too, plus a `@DIRNAME.000` with
    its GEOS name (type `GFT_DIRECTORY`). A native file needs a DOS name
    (`ERROR_INVALID_LONGNAME`). Renaming within the directory changes
    only the name in the header.
  - *DOS names with long file names* (dos32 when DOS has the LFN API:
    Windows 9x, DOSLFN; detected at the first use), as the MSLF driver
    of DOS16 GEOS. GEOS files still get generated 8.3 names. Native
    files and directories may keep a long DOS name (up to 32
    characters, none of `" * / : < > ? \ |`). A directory with such a
    name needs no `@DIRNAME.000`.
  - *Long names* (the other hosts): the GEOS name is the host name.
  - **Lookup**, on every host, for each part of a path:
    1. the name itself (with DOS names without LFN only if it is a DOS
       name, since DOS would cut it to 8.3);
    2. the name in any case;
    3. the GEOS name in the headers of the files with likely
       extensions (`.000`–`.999`, `.GEO`, `.VM`, `.DB`, `.STA`, `.BIT`)
       and in the `@DIRNAME.000` of directories.

    So either layout can be read on any host. A DOS16 installation
    works on Linux as it is.
- **Moving trees.** `Port32/tools/geostree.py todos [--lfn]|tolong SRC
  DST` copies a tree into the other layout (ctest `tools.geostree`).
- **dos32** uses long file names where DOS has them (DJGPP `_use_lfn`;
  Open Watcom's C library has no LFN calls, so it uses 8.3 names) and
  keeps the case of 8.3 names (`_crt0_startup_flags`). DOS updates the directory entry of
  an open file only on a commit, so a new GEOS file is committed after
  its header is written; otherwise it could not be found by its GEOS
  name while open.
- **Choosing the layout.** As in DOS16 GEOS, `[system] primaryFSD` in
  the init file chooses it (`FSDLoadPrimary`, `FSD/fsdInit.c`):
  - `mslf.geo` selects DOS names with LFN. On a DOS without LFN it falls
    back to DOS names.
  - `ms3`, `ms4`, `ms7`, `dri`, `os2` and `ntfat` select DOS 8.3 names.
  - `hostfs.geo` selects long names.
  - Without the key, the host's layout is used.
  - A Linux tree can therefore be kept in the DOS16 layout, and be used
    by DOS16 GEOS at the same time.
- ctest `kernel.file.geos` covers all of this on every host, DOS
  included (DOS names, with and without LFN, are tested on Linux too;
  the QEMU DOS has no LFN).

Not yet: links in `@DIRNAME.000`; the DOS code page for characters
above 0x7f in names; `FEA_GEODE_ATTR`, `FEA_CUSTOM` and the
network attributes; `FileGetHandleAllExtAttributes`;
`FileCopyPathExtAttributes`.

## 13f. File system drivers, drives and disks (`Library/Kernel/FSD`, `Drive`, `Disk`, `Driver/IFS/HostFS/hostfsDriver.c`, P4)

The asm kernel talks to a file system driver through `DR_FS_*` calls of
its strategy routine (`Include/Internal/fsDriver.def`). Port32 keeps
the same split but uses C. A driver is an `FSDriver`: a table of C
functions (`CInclude/Internal/fsDriver.h`). Each function returns 0 or a
`FileError`.

| `FSDriver` | asm |
|---|---|
| `FSD_diskID`, `FSD_diskInit`, `FSD_diskInfo` | `DR_FS_DISK_ID`, `DISK_INIT`, `DISK_INFO` |
| `FSD_curPathSet` | `DR_FS_CUR_PATH_SET` |
| `FSD_open`, `FSD_create` | `DR_FS_ALLOC_OP` `FSAOF_OPEN`, `FSAOF_CREATE` |
| `FSD_read` ... `FSD_close`, `FSD_handleExtAttrs` | `DR_FS_HANDLE_OP` `FSHOF_*` |
| `FSD_createDir` ... `FSD_pathExtAttrs` | `DR_FS_PATH_OP` `FSPOF_*` |
| `FSD_fileEnum`, `FSD_enumAttr` | `DR_FS_FILE_ENUM` |

- **Paths** passed to a driver are absolute on its disk. They are
  `\`-separated, with `.` and `..` already resolved by the kernel, and
  use the names GEOS knows (long names). How the names map to the
  medium is up to the driver.
- **Positions and sizes** are those of the data. For a GEOS file this
  is the data after the `GeosFileHeader`.
- **Drives** (`FSD/fsdDrive.c`, `Drive/driveHigh.c`). A driver makes a
  drive with `FSDInitDrive(number, name, status, media, fsd, private)`.
  The drive API (`DriveGetStatus`, `DriveGetExtStatus`,
  `DriveGetDefaultMedia`, `DriveTestMediaSupport`, `DriveGetName`)
  reads this table.
- **Disks** (`Disk/diskHigh.c`). `DiskRegisterDisk` asks the driver for
  the ID of the medium in the drive. It then returns the disk with that
  ID, or registers a new one (`FSD_diskInit`).
  - A `DiskHandle` is an even number, so it never collides with a
    `StandardPath`, which is odd (`DISK_IS_STD_PATH_MASK`).
  - A standard path used as a disk stands for the disk of the top
    directory.
  - Also available: `DiskGetDrive`, `DiskGetVolumeName`,
    `DiskGetVolumeInfo`, `DiskGetVolumeFreeSpace` (from
    `ghal_fs_space`), `DiskCheckWritable`, `DiskCheckInUse` (open
    files), `DiskCheckUnnamed`, `DiskForEach` and `DiskFind`.
- **HostFS** (`Driver/IFS/HostFS/hostfsDriver.c`). A drive is a host
  directory (`HostFSDriveData`). The driver contains what section 13c
  describes: host names, the `GeosFileHeader`, `@DIRNAME.000` and
  extended attributes.
  - Its disk ID is a hash of the directory. The disk has no volume
    name.
  - `FEA_FILE_ID` is a hash of the host path, the same for every name
    the file is reached by. `VMOpen` uses the disk and this ID to find
    a VM file that is already open.
  - HostFS serves Linux, GonWare and, for now, dos32 too.
- **Driver binding.** Until drivers are geodes, HostFS is linked into
  the kernel and the kernel makes drive `C`.

Not yet:

- drivers as geodes, and their registration (`FSDRegister`);
- the DOS file system driver (IFS) for dos32;
- drive locks, aliases and removing drives;
- removable media, `DiskSave`/`DiskRestore`, formatting, copying and
  renaming disks, and volume names;
- a current directory per drive (`C:NAME` is relative to the root);
- links.

## 13g. FileEnum (`File/fileEnum.c`, P4)

`FileEnum` works as in the asm kernel (`fileEnum.asm`):

- For each entry the driver reports (`FSD_fileEnum`), the kernel
  collects the attributes named in `FEP_returnAttrs`, `FEP_matchAttrs`
  and `FEP_callbackAttrs`, plus `FEA_FILE_ATTR`, `FEA_FILE_TYPE` and
  `FEA_NAME`.
  - They go into the `FileEnumCallbackData`, an array of
    `FileExtAttrDesc`.
  - An attribute the entry does not have has a null `FEAD_value`.
  - The kernel fills in `FEA_PATH_INFO`, `FEA_DISK` and
    `FEA_DRIVE_STATUS` itself.
- **Directories** are taken if `FESF_DIRS` is set.
- **Other entries** must pass two checks:
  - They must match `FEP_matchAttrs`:
    - Bit attributes (`FEA_FILE_ATTR`, `FEA_FLAGS`, `FEA_GEODE_ATTR`,
      `FEA_PATH_INFO`, `FEA_DRIVE_STATUS`): `FEAD_value` is not a
      pointer. Its low 16 bits are the bits that must be set, the next
      16 bits the bits that must be clear (the asm offset and segment).
    - Strings must be equal.
    - Anything else must be byte-for-byte the same.
  - They must have one of the file types in `FEP_searchFlags`.
- **Then the callback** (`FESF_CALLBACK`) decides. It accepts an entry
  by returning non-zero, as documented.
  - `FileEnumWildcard` returns TRUE for a match, as documented (the asm
    C stub returned the opposite).
  - `FEP_callback` NULL (`FESC_WILDCARD`) means the standard wildcard
    callback: `*`, `?`, `[..]`, `[^..]`, `\` escapes. The pattern is
    in `FEP_cbData1`; ignore case if the low byte of `FEP_cbData2` is
    non-zero.
- **Return types.** `FEP_returnAttrs` may be a
  `FileEnumStandardReturnType` cast to a pointer. In a return array,
  `FEAD_value` is the offset within the entry. Attributes the entry
  does not have are zero.
- **Buffer limits.** `FEP_bufSize`, `FEP_skipCount`,
  `FESF_LEAVE_HEADER` and `FESRT_COUNT_ONLY` work as before. The
  matches that did not fit go to `*numNoFit`. If there are no entries,
  there is no buffer, unless a header was asked for.
- **Standard subdirectories.** In a standard path itself (not below
  it), its standard subdirectories are entries even if they do not
  exist on the disk (`FEEnumSpecials`). A standard path that does not
  exist enumerates as empty.
- **Return value.** The number of entries, or -1 with
  `ThreadGetError`.

Port32 differences:

- `FEP_cbData1` and `FEP_cbData2` are `uintptr_t`, so they can hold a
  pointer (`CInclude/fileEnum.h`).
- The callback is called with frame 0. `FileEnumWildcard` finds its
  `FileEnum` through the calling thread.
- With `FESF_REAL_SKIP`, `FEP_skipCount` is set to the real skip count
  for the next call (the DI of the asm routine): the number of entries
  passed before the first one that did not fit.

Standard paths spread over several directories: section 13a.

Not yet: `FEA_CUSTOM`,
and the order of entries (it is the host's directory order).

## 13h. The init file (`Library/Kernel/Initfile`, P4)

The init file routines of `CInclude/initfile.h` work on the same files
as the asm kernel: geos.ini and the files it chains to.

**Format** (as before, `initfileLow.asm`, `initfileConstruct.asm`):

- `[category]` lines, then `key = value` lines. `;` starts a comment.
- Categories and keys compare without case and without spaces and tabs
  (`"My Cat"` is `mycat`).
- A value ends at the comment or the line end; spaces and tabs before
  them do not count.
- A value of several lines is a blob, `{` to `}`.
  - In a blob, `\` escapes `{`, `[`, `}` and `\`; `\n` and `\r` are a
    lone LF and CR.
  - Outside a blob only `\{` and `\[` are escapes.
- What `InitFileWriteString` writes:
  - A string becomes a blob if it starts or ends with a blank, or holds a
    line end or a `;`.
  - The empty string is `{}`.
  - Lines end with CR/LF.
- Integers are decimal; booleans are `true` and `false`.
- Data are two hex digits per byte, 20 bytes per line, and in a blob when
  the entry is longer than 40 columns.
- String sections are lines: runs of characters from the space up,
  separated by control characters.

**Files** (`Loader/ini.asm`):

- The init files are read on first use. The first is `geos.ini` in the
  top directory (`GEOS.INI` is also tried).
- `[paths] ini` names further files, separated by spaces, up to four in
  all.
- Reads search the files in order:
  - `IFRF_FIRST_ONLY`: the first file only.
  - `IFRF_READ_ALL`: every file; the values are joined by CR/LF.
- Writes and deletions change the first file, in memory.
  - `InitFileCommit` writes it back.
  - A file that cannot be written (a read-only geos.ini) is left alone.
- `InitFileSave` and `InitFileRevert` copy the first file to and from
  `ini.bak` in PRIVDATA.
- `InitFileReset` (kernel only) forgets what was read, e.g. after
  `FileSetTopHostPath`.

**Return values** follow the C stubs of `initfileC.asm`:

- Reading routines return TRUE on error. A string read into a buffer is
  cut to the buffer.
- `InitFileEnumStringSection` returns TRUE only if the callback stopped
  the enumeration.
- `InitFileReadAllInteger` returns TRUE if the callback stopped it, or if
  no file has the key.

Port32 differences:

- **Lenient reading.** These cases are read the way an editor of the
  file means them; the asm kernel misread them:
  - A line without `=` is skipped by itself.
  - `key =` with nothing after it is empty, unless a blob starts on the
    next line.
  - A blob may start with `{` and a lone LF (a file edited on Linux).
  - A boolean may end at the end of the file.
  - A section must fit into the buffer together with its null.
  - `IFRF_READ_ALL` also reads into a buffer.
- **Files.**
  - The files of `[paths] ini` are GEOS paths (a drive name, or relative
    to the top directory), not host paths.
  - Without a geos.ini the first file starts empty, and `InitFileCommit`
    creates it.
  - The file is not kept open between commits.
- **Grab.** `InitFileGrab` copies the passed block. What is written
  while grabbed goes back into the block (with a `C_CTRL_Z` at the end)
  at `InitFileRelease`; until then other threads wait.

Not yet: the loader's `/P` switch, geosec.ini, the hash table of the
categories, `InitFileBackupLanguage`/`InitFileSwitchLanguages`, and
`InitFilePushPrimaryFile`/`InitFilePopPrimaryFile`.

## 13i. Starting the kernel (`Library/Kernel/Boot`, `Loader`, P4)

`Loader/main.c` is the program that starts GEOS on the host (`geos`,
`geos.exe` on dos32), after `LoadGeos` of `Loader/main.asm`.

- **Top and command tail.** As the DOS loader, it takes the current
  directory as the GEOS top. Its arguments, joined by blanks, are the
  command tail.
- **`KernelBoot`** (`bootInit.c`, after `InitGeos`) does, in order:
  1. the first thread and the timer;
  2. the top as drive C, and the initial path `SP_TOP`;
  3. the primary file system driver (`FSDLoadPrimary`): the HostFS name
     layout (§13c) and its drives;
  4. the standard paths of `[paths]` (§13a);
  5. the UI (`LoadUI`);
  6. the geode the command tail names (`ProcessCommandLine`).
- **Drives.** DOS16's file system driver made a drive for each DOS
  drive. HostFS serves the top as drive C. `[hostfs] drives` gives it
  more: `D=/srv/geos E=/mnt/cd`, a letter, `=`, a host directory,
  separated by blanks or line ends. A host path with a blank cannot be
  given. The key is new in Port32.
- **The UI.** `[ui] generic` names the UI geode, loaded from
  `SP_SYSTEM`. If it fails, `ui.geo` is tried, and if that fails too,
  `CANNOT_LOAD_UI` is fatal. Without the key no UI is loaded: there is
  no C UI library yet to be the default. `kernelUIHandle` holds it.
- **The command tail.** The switches (`/x...`, up to a blank) are
  skipped. The rest is loaded as a process from `SP_SYSTEM`, the path
  `LoadUI` left, as DOS16 did. `KernelBoot` returns that process.
- **Running.** DOS16 turned the boot thread into the scheduler's idle
  loop. Here the host schedules the threads. `KernelRun` keeps the boot
  thread waiting until no process is left or GEOS ends, and returns the
  exit flags. `geos` then prints the reason, if there is one, and exits
  with 1 after a dirty end.

**Shutdown** (`Sys/sysMisc.c` after `SysShutdown` of `sysMisc.asm` and
`sysC.asm`):

- The arguments follow `system.h`:
  - `SST_CLEAN`, `SST_SUSPEND`, `SST_CLEAN_REBOOT`, `SST_POWER_OFF`:
    `optr ackOD, Message ackMsg`;
  - `SST_DIRTY`, `SST_FINAL`: `const char *reason` (NULL for none);
  - `SST_CONFIRM_END`: `Boolean allow`.
- **A clean shutdown or suspend** sends `MSG_META_CONFIRM_SHUTDOWN
  (word confirmed, GCNShutdownControlType type)` to the objects on
  `GCNSLT_SHUTDOWN_CONTROL`.
  - Each answers with `SST_CONFIRM_START` and then `SST_CONFIRM_END`.
    `SST_CONFIRM_START` gives the right to ask the user, one at a time;
    it returns TRUE when someone has refused already.
  - After the last answer, `ackOD` gets `ackMsg` with `{ word confirmed }`.
    Without an `ackOD`, the UI gets `MSG_META_DETACH` if all agreed.
  - Only one clean shutdown or suspend runs at a time; another call
    returns TRUE.
- **`SST_CLEAN_FORCED`** detaches the UI, which comes back with
  `SST_FINAL`. Without a UI it ends GEOS at once.
- **`SST_RESTART`** returns TRUE: the C kernel cannot reload itself.
  DOS16 returned the same when it could not find `loader.exe`.

**EndGeos** (`Boot/bootBoot.c`), reached through `SST_DIRTY`,
`SST_FINAL`, `SST_PANIC` and `SST_REBOOT`:

1. clears the file change list;
2. gives `DR_EXIT` to the drivers that are not system drivers;
3. closes the files still open;
4. gives `DR_EXIT` to the system drivers;
5. lets `KernelRun` return `KEF_DIRTY`, `KEF_RESET` or `KEF_POWER_OFF`,
   and the reason (`KernelExitMessage`).

On every thread but the boot thread, EndGeos does not return, as in
DOS16: the thread waits until the program ends the process. On the boot
thread it returns.

**`GEOS_ACT.IVE`** (after `InitSys` and `EndGeos`):
- `KernelBoot` creates it in the top. If it is there already, the last
  run did not end cleanly, and `SysGetConfig` reports `SCF_CRASHED`.
- A clean end deletes it; a dirty end leaves it.

**`SysGetConfig`** returns:
- `SCF_RTC | SCF_COPROC`, because every host has a clock and floating
  point, plus `SCF_CRASHED` when it applies;
- processor `SPT_80486`, the newest the type knows;
- machine `SMT_UNKNOWN`.

ctest `kernel.shutdown` stands queues in for the objects on the list
and for `ackOD`. It checks:

- a confirmed shutdown, and a second one refused while it runs;
- a suspend refused by one of two objects;
- `SST_RESTART`;
- `SST_DIRTY` from another thread: the reason, the files closed, the
  thread kept;
- `SCF_CRASHED` after a left-over `GEOS_ACT.IVE`, which the dirty end
  keeps.

`kernel.boot` ends cleanly with `SST_FINAL` on the boot thread and
checks that `GEOS_ACT.IVE` is gone.

It needs no geodes, so it also runs on Open Watcom.

ctest `kernel.boot` checks the chain on every host that loads geodes
(not Open Watcom):

- a geos.ini gives drive D;
- `[paths] system` puts `D:\GEODES` into `SP_SYSTEM`;
- `[ui] generic` names `kglib.geo`;
- the tail `/a /bc kgapp.geo` loads the process. Both geodes exist only
  on drive D.

Not yet:

- the drivers (memory, power, keyboard, mouse, video, font);
- the input manager, fonts, graphics and windows;
- patches, the log file, `documentIsTop`, `FSDInitComplete`;
- the loader's switches (`/p`, `/sp`, memory limits);
- `ExitFSD`, the task driver, the power-off message, and the
  switches `/s` and `/log` (`SCF_UNDER_SWAT`, `SCF_LOGGING`).

## 13j. The input manager (`Library/Kernel/IM`, P4)

The input manager is an event thread of the kernel with `IMClass`, a
subclass of `ProcessClass` (`IM/imInit.c`). `KernelBoot` makes it,
stopped, before the drivers load (`InitIM`). The drivers add their
monitors and send to it. It starts when the drivers are loaded. Its
`MSG_META_ATTACH` finds the default keyboard driver and reads
`[input]`: `leftHanded`, `doubleClickTime`, `rebootOnReset` and
`quickShutdownOnReset`.

**Messages.** `IM_FIRST_MESSAGE` is 8704 (`CInclude/Internal/im.h`).
- The drivers send `MSG_IM_PTR_CHANGE`, `MSG_IM_BUTTON_CHANGE`,
  `MSG_IM_KBD_SCAN` and `MSG_IM_MOUSE_WHEEL_VERTICAL` (8727).
- Their parameters are `IMParams`: `cx`, `dx` and `bp` as words, then
  `si`, the driver handle, as a `Handle`.
- What goes out are `MSG_META_KBD_CHAR` (119), `MSG_META_MOUSE_BUTTON`
  (117), `_PTR` (118), `_DRAG` (123), `_WHEEL_VERTICAL` (124) and
  `MSG_META_LEAVE_CONSTRAIN` (22). Their parameters are goc's three
  words `cx`, `dx` and `bp`. ctest `kernel.obj` checks all these
  numbers against goc.

**The monitor chain** (`IM/imMonitors.c`). Every event goes through the
monitors in the order of their levels:

| Level | Name | What it does |
|---|---|---|
| 20 | `ML_DRIVER` | the keyboard driver |
| 40 | `ML_COMBINE` | pointer changes summed into a position; button state, double press, drag (by distance and by time), the lost drag on a release with the release held back (`MF_MORE_TO_DO`), the wheel, the shift state after a state key, `<Ctrl><Alt><Del>` ending GEOS (`SST_CLEAN_FORCED`, the second time `SST_DIRTY`, or `SST_REBOOT`) |
| 60 | `ML_PTR_PERTURB` | the rectangle of `ImConstrainMouse` |
| 100 | `ML_OUTPUT` | the screen limits; the events go to the output |

- A monitor is a C function on an `ImEvent` (message, `cx`, `dx`, `bp`,
  `si`). It returns `MonitorFlags`. Message 0 asks a monitor that said
  `MF_MORE_TO_DO` for its next event.
- `ImAddMonitor(mon, level, routine)` returns the input manager's
  thread.
- `ImRemoveMonitor` waits on a semaphore of the monitor while the chain
  is inside it.

**Pointer events.** DOS16 folded a pointer event into the last one in
the output queue when it had the same button info (`CombineMouseEvent`,
`MF_CUSTOM`). The C kernel has no custom combining. Instead, a pointer
event with the button info of the last one sent replaces the last event
in the queue (`MF_CHECK_DUPLICATE`, `MF_REPLACE`, `MF_CHECK_LAST_ONLY`).

**Routines** (`IM/imMisc.c`, exported as in `geos.gp`):
- input: `ImGrabInput`, `ImReleaseInput`, `ImInfoInputProcess`;
- buttons: `ImSetDoubleClick`, `ImInfoDoubleClick`, `ImGetButtonState`;
- pointer: `ImGetMousePos`, `ImPtrJump`, `ImForcePtrMethod`,
  `ImConstrainMouse`, `ImUnconstrainMouse`.

There are no windows yet, so positions are screen positions. Instead of
`ImSetPtrWin`, the video driver will set the pointer's limits with
`ImSetScreenLimits`; until then there are none.

**Not yet:**
- the screen saver;
- the pointer image (`CallMovePtr`) and the window under the pointer;
- the move/resize xor;
- pen and ink;
- `ImSetPtrImage`/`Flags`, `ImBumpMouse`.

DOS16 built without `QUIET_PTR_SUPPORT` and `INPUT_MESSAGE_RECEIPT`, and
so does the C kernel.

ctest `kernel.im` sends what pointer drivers send and reads the output.
It checks:
- relative and absolute moves;
- presses, releases and the button state;
- the double press and the time limit;
- the drag, and the lost drag;
- left-handed buttons and the wheel;
- keys;
- monitors of its own, with `MF_MORE_TO_DO`, eaten events and
  `ImRemoveMonitor`;
- `ImConstrainMouse` with `MSG_META_LEAVE_CONSTRAIN`;
- the screen limits and the grab.

ctest `driver.keyboard` covers the way from the HID event to
`MSG_META_KBD_CHAR`.

## 13k. Graphics (`Library/Kernel/Graphics`, P6)

**Regions** (`graphicsRegion.c` beside `graphicsRegion.asm`) are the
DOS16 format without bounds words:
- lines of the bottom y of a band, its on/off x pairs (inclusive) and
  `EOREGREC`;
- one more `EOREGREC` ends the region;
- the first band reaches up to the top of the coordinate space, and
  below the last one the region is empty.

The routines:
- `GrPtrRegOp` and `GrChunkRegOp` (`CInclude/Internal/grWinInt.h`)
  compute AND, OR and NOT. NOT works within `MIN_COORD` ..
  `MAX_COORD - 1`, as in the asm.
- One walk over the bands of both regions does all three operations.
  It merges equal neighbouring bands and touching pairs, and leaves out
  an empty last band. A result may therefore be shorter than the asm's
  for the same area.
- When the result does not fit, `GrPtrRegOp` says so and returns the
  size to try again with. This holds for NOT too (the asm did not check
  there).
- `GrChunkRegOp` takes optrs. It resizes the result chunk and
  dereferences after each resize.
- `GrMoveReg`, `GrGetPtrRegBounds`, `GrTestPointInReg` and
  `GrTestRectInReg` are as in `graphics.h`.

ctest `kernel.region` checks AND, OR and NOT of random unions of
rectangles point by point.

**Graphics states** (`graphicsState.c`, `graphicsGetState.c` and
`graphicsColor.c`, beside their asm files):
- A GState is an LMem block of type `LMEM_TYPE_GSTATE`, with the
  `GState` structure (`CInclude/Internal/gstate.h`, 32-bit handles) as
  its header.
- `GrCreateState` fills in the defaults of `GState <>`. There is no
  cache of free GStates.
- `GrSaveState` copies the whole block into a block of its own, linked
  by `GS_saveStateLink`. `GrRestoreState` copies it back and clears the
  window's cached flags if the window's `W_curState` is this GState.
- A color is kept as both a palette index and an RGB value. RGB maps to
  the closest entry by the asm's distance (`MapRGBtoIndex`): on the
  default palette, the first 32 entries and then one entry of the color
  cube. The palette is the window's (`W_palette`) or the default.
- Draw masks come from the system table (`graphicsTables.c`, taken from
  `graphicsTables.asm`) and are lined up with `GS_xShift` and
  `GS_yShift`, as in `GrShiftMask`.
- The `AttrOpts` flags follow the asm (`CalcRectOpts`, `SetMask`).
- The setters and getters have the C API of `graphics.h`.

Not yet:
- graphics strings and paths: a setter only changes the GState;
- the fonts: see §13n;
- devices with 16 colors or fewer.

ctest `kernel.gstate` checks:
- the defaults;
- index, RGB, color cube, grey and CMY colors;
- a window's palette;
- masks with their `AttrOpts` and alignment;
- line, area and text attributes;
- nested save and restore;
- destroying a GState whose window holds it in `W_curState`.

**Fixed point math** (`graphicsMath.c` beside `graphicsMath.asm` and
`graphicsMathObscure.asm`) gives the asm's results bit for bit:
- products of the magnitudes, truncated (`GrMulWWFixed`,
  `GrMulDWFixed`); quotients truncated, `GrSDivDWFbyWWF` rounded; an
  overflowing quotient is 0;
- `GrReciprocal32` is the truncated `1.0 / v`;
- `GrSqrRootWWFixed` takes two Newton steps as the asm;
- sine and cosine interpolate the asm's tables (taken into
  `graphicsTables.c`), with its step in the last degree; the arc sine
  searches the table and mirrors only the integer part in the other
  quadrants, as the asm.

**Transformations** (`graphicsTransformUtils.c`, `graphicsTransform.c`,
`graphicsUtils.c`):
- a point is a row vector, `x' = 11 x + 21 y + 31`,
  `y' = 12 x + 22 y + 32`; the 2x2 part is `WWFixed`, the translation
  `DWFixed`;
- the asm multiplied only the elements that are not 0 or 1; the C code
  multiplies them all with the same truncating products, which gives
  the same results;
- `SetTMatrixFlags`: `x.0001` and `x.ffff` become whole numbers first;
- translation, scale and rotation go before the matrix
  (`GS_TMatrix = op * GS_TMatrix`), as does `GrApplyTransform`; all but
  the translation drop the font;
- `GrSaveTransform` keeps a stack of chunks in the GState (`next`,
  matrix, default chunk and its matrix); `GrInitDefaultTransform` and
  `GrSetDefaultTransform` use `GS_defTMatrix`;
- with a window the device matrix is `W_curTMatrix`: `GS_TMatrix *
  W_TMatrix`, moved by the window's left and top (`GrComposeMatrix`).
  A change of the GState's matrix clears `WGF_XFORM_VALID` and
  `WGRF_PATH_VALID` when the window holds the GState;
- the inverse (`CalcInverse`) is `1/11`, `1/22` or, rotated,
  `1 / det` in 32.32, its low word dropped as in the asm;
- `GrTransform`, `GrUntransform` and `GrGetCurPos` round to words and
  return `0x8000, 0x8000` on overflow;
- the pen position `GS_penPos` is kept in page coordinates; a relative
  move goes through the 2x2 part only.

ctest `kernel.transform` checks the math (products, quotients, square
roots, sine, cosine, tangent, arc sine, `DWFixed`), every transformation
and its flags, there and back in words, `WWFixed`, `DWFixed` and
dwords, the pen, saved and default matrices, and a window's matrix and
position.

## 13l. Windows (`Library/Kernel/Win`, P6)

The window system in C lies beside the asm: `winCreDest.c`,
`winWindows.c`, `winIndividual.c`, `winState.c`, `winTrans.c`,
`winUtils.c`, with `winInt.h` for the kernel.

**Windows and their regions.**
- A window is an LMem block of type `LMEM_TYPE_WINDOW` that starts
  with `Window` (`CInclude/Internal/window.h`).
- Its regions are chunks of the block, in screen coordinates, without
  bounds words.
- The asm's `WHOLE_REG` is not used. Where the asm made the invalid
  region WHOLE, it is the visible region here.

**The tree.**
- The first child is in front.
- A window goes behind every sibling with a smaller layer priority
  value. Within its layer (`W_layerID`) it goes behind those with a
  smaller window priority.
- On equal values it goes in front, or behind with
  `WPF_PLACE_BEHIND` (`WPF_PLACE_LAYER_BEHIND` for a new layer).

**Validation.**
- `W_univReg` is `W_winReg` in the parent's universe, without the
  siblings in front of it.
- `W_visReg` is the universe without the children's regions.
- The asm recalculated only the windows within the change bounds. The
  C code recalculates the whole tree of the changed window, which gives
  the same regions.
- What becomes visible is added to `W_invalReg`. It is washed out with
  the background through the driver's `DR_VID_RECT` (not for
  `WCF_TRANSPARENT`) and exposed with `MSG_META_EXPOSED` (69).
- There is one exposure at a time (`WRF_EXPOSE_PENDING`), none while
  the window is suspended, and none for `WCF_PLAIN`.
- Moving or resizing exposes the whole branch, as in the asm (no
  blit).

**Updates and the drawing mask.**
- `GrBeginUpdate` makes the invalid region the update region.
- `GrEndUpdate` and `WinAckUpdate` end the update and send
  `MSG_META_WIN_UPDATE_COMPLETE` (71) to the input object.
- The mask for a GState (`WinValWinStruct`) is the visible region
  without the invalid one, and for the update GState also within the
  update region.

**Locking.** The asm took the tree semaphore and P-locked each window.
The C code has one recursive tree lock for all of it (`WinTreeP`,
`WinTreeV`).

**Not yet:**
- save-under areas (the C drivers refuse them, so none ever exists);
- pointer enter/leave notification (`winNotification.asm`);
- `WinScroll`;
- the GState's clip paths in the mask;
- `WCF_MASKED`;
- flushing the input and exposure objects' queues before a window is
  freed. `WinClose` frees the window at once, so a queued
  `MSG_META_EXPOSED` for a closed window must not be answered;
- `WinForEach`, the geode window variables (`winGeode.asm`),
  `WinRealizePalette`.

**`WinSetInfo` in Port32** takes 64 bits (`optr`):
- an optr for `WIT_INPUT_OBJ` and `WIT_EXPOSURE_OBJ`;
- for `WIT_PRIVATE_DATA`, a pointer to the `WinPrivateData`, as an
  integer;
- for `WIT_STRATEGY`, the routine, as an integer;
- for `WIT_COLOR`, `red | flags << 8 | green << 16 | blue << 24`.

`WinGetInfo` writes the C stub's layout with Port32's sizes (see
`winState.c`).

**Tests.** ctest `kernel.win` runs on a fake driver. It checks:
- opening, washing and exposure;
- updates, the mask, suspending and `WinInvalReg`;
- overlapping windows and priority;
- moving and resizing, child windows, closing;
- information and transformations;
- `WinLocatePointC`.

## 13m. Drawing (`Graphics/graphicsOutput.c`, `graphicsLine.c`, P6)

Every drawing routine starts like `EnterGraphics` (`GrEnterGraphics`,
`graphicsUtils.c`):
- the GState is locked;
- if it has a window, the tree lock is taken and the window locked;
- `WinValWinStruct` runs when the window was last used by another
  GState or its mask or matrix is not valid.

`GrExitGraphics` undoes this. The pen moves as in the asm, also when
nothing is drawn. Nothing is drawn (`TrivialReject`) without a window,
with a null mask, or with a matrix that maps everything to a line.

Coordinates go through `W_curTMatrix` (GState and window matrices plus
the window's position) and are rounded to words; on overflow nothing is
drawn. The driver is called with `DR_dsSi` the GState, `DR_si` the
offset of `GS_areaAttr` or `GS_lineAttr`, `DR_esDi` the window.

**Rectangles** (`GrFillRect`, `GrFillRectTo`, `GrDrawPoint`,
`GrDrawPointAtCP`):
- the 2.0 imaging conventions: the right and bottom pixels are not
  filled, unless they equal the left and top;
- `DR_VID_RECT`, clipped to `W_maskRect`;
- rotated, the four corners as `DR_VID_POLYGON`.

**Frames and lines** (`GrDrawRect`, `GrDrawRectTo`, `GrDrawLine`,
`GrDrawLineTo`, `GrDrawRelLineTo`, `GrDrawHLine`, `GrDrawHLineTo`,
`GrDrawVLine`, `GrDrawVLineTo`):
- a frame is four lines with square ends;
- `LE_BUTTCAP` leaves the last pixel out (`AdjustLineEnds`);
- horizontal and vertical lines go as `DR_VID_RECT`, others as
  `DR_VID_LINE`;
- lines are rejected on the mask's bounds.

**Polygons** (`graphicsPolygon.c`: `GrFillPolygon`,
`GrTestPointInPolygon`) are scan converted into a region in device
coordinates, as the asm did, and drawn with `DR_VID_REGION`. The polygon
covers its outline's pixels (as a polyline would draw them) and what is
inside by the even-odd or the winding rule. The asm's edge table,
active edge list and Bresenham steps are kept. The tables and the region
are in the C heap rather than a 64K block, and the region grows as it is
written. `GrTestPointInPolygon` transforms the point by the window's
matrix only, as the asm did.

**Polylines** (`graphicsPolyline.c`: `GrDrawPolyline`, `GrDrawPolygon`,
`GrBrushPolyline`) use the separator format of `DR_VID_POLYLINE`.
- A point past `MAX_COORD` draws nothing.
- The line width is scaled by the window's matrix (`ScaleScalar`).
- `GrBrushPolyline` takes the brush width as its fourth argument, as the
  C stub read it.

**Fat lines** (`graphicsFatLine.c`): each segment of a polyline wider
than a pixel is a six-point polygon.
- Ends: butt, square or round.
- Joins: beveled, mitered (beveled under the miter limit) or round.
- The arithmetic is the asm's WWFixed arithmetic.
- A round end or join is a filled circle (`GrFillEllipseDev`).

**Dashes** (`graphicsLineStyles.c`):
- A single thin line goes to the driver as `DR_VID_DASH_LINE`, with
  lengths scaled along its major axis.
- A polyline is cut into dashes along its Bresenham pixels. Each dash
  is a polyline of its own and goes round corners.
- Unlike the asm, the dashes of every polyline of the format are kept,
  and a skip distance past the pattern length is taken modulo that
  length.

**Ellipses** (`graphicsEllipse.c`, `graphicsArcLow.c`,
`graphicsCalcEllipse.c`, `graphicsCalcConic.c`):
- `CalcEllipse` transforms the bounding box and doubles the
  coordinates, so a center between pixels is a whole number.
- The ellipse is its center and two conjugate radii (with rotation, to
  the midpoints of the transformed box's sides). From them come the
  coefficients of `Ax^2 + Bxy + Cy^2 + Dx + Ey + F = 0`, with the origin
  on the curve.
- `CalcConic` steps along it octant by octant with the midpoint
  algorithm, in 64 bits. The halves and quarters the asm carried in `cl`
  and `ch` are carried the same way, so the pixels are the same.
- The points form one polyline in the separator format. Rows outside
  the mask (widened by the line width) are left out.
- An outline is that polyline drawn closed in the line attributes. A
  filled ellipse goes to the driver as a convex `DR_VID_POLYGON`.
- An ellipse at most two pixels thick is its four extreme points.

**Arcs** (`graphicsArc.c`, `graphicsArcLow.c`):
- A bounded arc runs counterclockwise from the point of the ellipse at
  the start angle to the one at the end angle (degrees from 3 o'clock).
  Its ends are swapped when the matrix mirrors (`TestFlip`).
- `CalcConic` steps round the ellipse twice and keeps the part between
  the ends.
- A chord closes from the end to the start, a pie through the center.
  An outline is drawn as a polyline. A filled arc is a polygon filled by
  the odd-even rule.
- A 3-point arc has its center where the perpendicular bisectors of
  points 1-2 and 2-3 meet. Points 1 and 3 are swapped so that the arc
  runs counterclockwise through point 2.
- Three points on a line draw a line from point 1 to point 3. The asm
  drew it untransformed; it is transformed here.
- The pen ends at the end of an open bounded arc and at the start of a
  closed one. For a 3-point arc it ends at point 1 if open and at point 3
  if closed. The "to" forms start at the pen and leave it at point 3.

**Rounded rectangles** (`graphicsRoundedRect.c`):
- Four quarter arcs (`CET_BOUNDED_ARC_RR`) joined into one closed
  polyline.
- The radius is at most half the width and half the height.
- It is filled as a convex polygon.

**Regions and the window** (`graphicsRegion.c`, `graphicsWin.c`):
- `GrDrawRegion` passes the region, with its bounds, to the driver as
  `DR_VID_REGION` at the pen. The four parameters of the call are
  `PARAM_0` to `PARAM_3` (`DR_bpPtr`).
- `GrInvalRect` invalidates in window coordinates. When rotated, it
  invalidates the region of the four corners.
- The window's and the mask's bounds go back to document coordinates:
  - without scaling or rotation, as the device rectangle less the
    translation (the asm's rounding is kept);
  - otherwise, as the box round the four corners untransformed.
- Without a window:
  - the bounds are the whole coordinate space;
  - `GrTestRectInMask` gives `TRRT_OUT`.

**Bitmaps** (`graphicsRaster.c`):
- A bitmap goes to the driver as `DR_VID_PUTBITS` at its device
  position, unchanged, when it can: uncompacted, with no scaling or
  rotation, at 72 dpi, and in a format the device takes.
- A complex bitmap comes in slices. The C callback returns the next
  slice; NULL means there is no more.
- A PackBits bitmap is unpacked slice by slice, the scan lines going
  on down until the mask's bottom.
- `GrFillBitmap` draws the 1s in the area color (`PBF_FILL_MASK`).
- A palette goes as RGB to a 24-bit device; other devices get their
  own indices for its colors.
- Scaled (by the matrix, or a complex bitmap not at 72 dpi), the
  scan lines and pixels are picked from the source in steps of the
  inverse factor, as `InitBitmapScale`/`ScaleSlice` do. The scaled
  size takes the fraction of the device position into account.
- A negative factor mirrors: to the left of the position in x, and
  upwards from it in y. The asm hands the driver a negative height for
  the latter; the C kernel flips the lines itself, so drivers only see
  positive heights.
- Rotated, the bitmap is scaled to the length of its rotated top edge
  and to the number of steps down its left edge. Each scan line goes
  along a line parallel to the top edge (`DR_VID_PUTLINE`), and the
  lines step down the left edge. Where the left edge also steps
  sideways, the line is put once more first, so that no holes remain.
  This also happens after the last line, as in the asm.
  - The asm's `BumpCoords` compared the step with the old position
    (`cmp di, ax`) instead of testing it. The C kernel tests the
    step.
- `DR_VID_PUTLINE` starts the scan line at the left end of a mostly
  horizontal line, and at the top end of a mostly vertical one, as
  the asm drivers do (`PutLineSetup`). The kernel mirrors the line
  when it needs it the other way round. The C driver had started at
  point 1; it is fixed.
- Huge bitmaps (`GrDrawHugeBitmap`, `GrFillHugeBitmap`, the `AtCP`
  forms; `graphicsBitmapHuge.asm`): the bitmap is the `CBitmap` after
  the `HugeArrayDirectory` (`EB_bm` of `EditableBitmap`), one scan line
  per element; drawn like a complex bitmap with each block of the huge
  array a slice (the directory stays locked, the data blocks are
  locked in turn), so scaling, rotation and PackBits work as for the
  others.
- Bitmaps to draw into (`graphicsBitmapCreate.c`): `GrCreateBitmap`,
  `GrCreateBitmapRaw`, `GrEditBitmap`, `GrDestroyBitmap`,
  `GrSet`/`GetBitmapMode`, `GrClearBitmap`, `GrGetPoint`,
  `GrSet`/`GetBitmapRes`, `GrGetBitmapSize`, `GrGetHugeBitmapSize`;
  `GrGetPalette` (`graphicsColor.c`). The bitmap is a huge array whose
  directory block is an `EditableBitmap` (`CInclude/Internal/videoDr.h`)
  with the DOS16 layout after the `HugeArrayDirectory`: the `CBitmap`,
  `EB_flags`, `EB_color` (a word: no color transfer yet), `EB_device`
  (the DOS16 `VideoDriverInfo`, 32 bytes, `CB_devInfo` 24), the
  scratch buffer the asm reserved, then the palette of a `BMT_PALETTE`
  bitmap. The window is a root window on the memory video driver (see
  §7.5). The kernel does not lock the directory block into
  `W_bmSegment` around drawing as `EnterGraphics` did; the driver
  locks it. `GrGetBitmap` returns what `DR_VID_GETBITS` gives, 24
  bits on the C drivers (DOS16: the device's format).
  `GrCompactBitmap`/`GrUncompactBitmap` pack and unpack each scan line
  (PackBits, runs of three and more repeated, at most 128); where a run
  meets those limits the bytes may differ from the asm's.
- Bitmaps of more colors than the device (`InitFormatConversion`,
  `ChangeBitmapFormat`) are converted scan line by scan line on their
  way to the driver, so slices, scaling, rotation, PackBits and huge
  bitmaps all get it. To monochrome: the luminance (`CalcLuminance`,
  bit for bit) picks one of 17 grey system draw masks, whose bit at x
  and at the line's number from the first converted line makes the
  pixel white; `MM_SET`, `MM_CLEAR` and `MM_INVERT` take black. To 4 or
  8 bits: the nearest of the first 16 or 256 entries of the window's
  palette. The mask is kept. The asm kept the line number across a
  draw too; it converted slices, the C code converts what goes to the
  driver, so the dither rows of scaled bitmaps follow the drawn lines.
- `GrDrawImage` and `GrDrawHugeImage` (graphicsImage.asm) draw a bitmap
  in the device's pixels: only the position is transformed, each pixel
  is 1, 2, 4, 8 or 16 device pixels square. At size 1 it is the bitmap
  unscaled. Larger, each run of one color is a rectangle in the area
  attributes (dithered), `IF_BORDER` gives the last row and column of
  each pixel to a grid in the line attributes, and masked pixels are
  left out unless `IF_IGNORE_MASK`. A 24-bit pixel is R, G, B together;
  the asm read the three a width apart.
- LZG compacted bitmaps (`BMC_LZG`): the data of a slice is LZG streams
  one after another, unpacked like PackBits. LZG itself
  (`Library/Kernel/Heap/heapCompress.c`) writes the asm's format and,
  except at the very end of the data (where the asm compared bytes past
  it), the same bytes. The compress stack is just the block of the hash
  tables. Compressed VM blocks (`COMPRESSED_VM`) and compressed XIP
  resources were off in the DOS16 kernels and are not supported.

**GrBitBlt** (`graphicsRaster.c`, `GrBltCommon` for `WinScroll` too):
- Only what is visible and valid at the source is copied, with
  `DR_VID_MOVBITS` within that region (it is the mask meanwhile).
- The rest of the destination becomes invalid.
- The source left behind becomes invalid with `BLTM_MOVE` and is
  washed out with `BLTM_CLEAR`.
- Under a scaled matrix, the size is scaled by `TM_11`/`TM_22`.

**WinScroll** (`winWindows.c`):
- The move is rounded to whole device pixels and applied to
  `W_TMatrix` as a translation.
- What is on the screen moves along with it through `GrBltCommon`
  (`BLTM_MOVE`), so what comes into view becomes invalid.
- The region of an update in progress moves too.
- The translation applied is returned in document units. With no move
  at all (less than half a pixel), the asm returned the move in device
  units, and so does the C port.

**Not yet:**
- graphics strings and paths (drawing to them does nothing);
- area patterns (the asm filled them through a path);
- splines; text is §13n.

**Tests.** ctest `kernel.draw` runs on the host video driver and reads
the pixels back. It checks:
- the windows' backgrounds;
- rectangles with the imaging conventions, clipping to the window and to
  windows in front;
- translated, scaled and rotated GStates;
- frames, butt and square line ends, 45 degree lines, lines from the
  pen, points;
- filled polygons by both rules, `GrTestPointInPolygon`, polylines,
  polygon outlines, a brushed polyline;
- fat lines with butt and square ends and mitered joins;
- dashes on a line and along a polyline;
- filled and outlined ellipses, a rotated one, a fat outline and round
  line ends;
- arcs (pie, chord, open, 3-point, from the pen, through a mirroring
  matrix) and rounded rectangles;
- regions, with a parameter; window and mask bounds,
  `GrTestRectInMask`, `GrInvalRect` (rotated too), `GrInvalRectDWord`;
- bitmaps: monochrome drawn and filled, 24 bits, PackBits, a complex
  one with a palette in two slices; scaled twice, half, mirrored in x
  and y, at 144 dpi; rotated by 90 degrees, and by 45 degrees without
  holes;
- `GrBitBlt`: copy, move, clear, and from a source partly hidden;
- `WinScroll`: the screen and the document move, and the rest is
  washed out;
- drawing during an update;
- exposure after moving and closing windows;
- text (§13n).

## 13n. Fonts and text (`Graphics/graphicsInit.c`, `graphicsChars.c`, `graphicsFont.c`, `graphicsTextMetrics.c`, `graphicsText.c`, P6)

`CInclude/Internal/fontDr.h` has the font structures of `fontDr.def`
and `graphicsConstant.def` in the host layout.

**Font files** stay the DOS16 files (`*.fnt` in `SP_FONT`). At the boot
(`GrInitFonts`, after the standard paths) the kernel reads each header:
`FontFileInfo` (`"BSWF"`, major version up to 1, the header size), then
the `FontInfo` from `FI_fontID` on, packed (33 bytes), and its tables
(`PointSizeEntry` 10 bytes, `OutlineDataEntry` 26 bytes) at offsets
that count the 2-byte `FI_fileHandle`. Each becomes a `FontInfo` chunk
in the host layout with its offsets recomputed; old outline weights
are adjusted (`AdjustFontWeight`). The font info block is an LMem block
with the four chunks of the asm at their fixed handles; the
modification data of the asm (for its font cache file) is not kept,
and that chunk is the list of open font files (`[system]
maxfontfiles`).

**A bitmap face** in the file is a DOS16 `FontBuf` image: 71 bytes of
header, `CharTableEntry` of 8 bytes, the kerning tables and the
characters, all at offsets from its start. Loading converts the header
and the table to the host layout and moves every offset by the
difference (`GrConvertBitmapFont`); the `CharData` stays as it is.

**The default font** is `[system] fontid` (a name, without case) at
`fontsize`, the nearest size there is; else Berkeley 10, else the first
font with bitmaps. It is loaded at the boot into a block that stays and
is in use from the start. Without font files the kernel runs without
fonts, and text draws nothing; DOS16 stopped there.

**The font of a GState** (`FindFont`): a font in use with the same
attributes (and the same matrix, if it is complex); else the font if it
has the bitmap of the size and style (plain matrix, normal weight and
width, no super- or subscript) or outlines; else the nearest
(`LibFindBestFace`: the nearest bitmap size, else the outlines, else
the default font, `FBF_MAPPED_FONT`). A new font in use is a discarded
block, filled when first locked: from the file for a bitmap, by the
font driver (`DR_FONT_GEN_WIDTHS`) for outlines. The GState keeps the
block and the entry; the entry counts the GStates, `GrInvalidateFont`
lets go. Locking sets the GState's font values (`UpdateFontOpts`).

**Widths** (`GrTextWidth`, `GrCharWidth`): the character widths, the
default character for one not there, the space padding, an optional
hyphen at the end only, and with kerning the track value and the pair
value, added to a width only while it stays positive. `GrFontMetrics`
has the table of the asm.

**Drawing** (`GrDrawText`, `GrDrawChar` and their `AtCP` forms): the
position is the top of the font's box, or the baseline, accent or
bottom (`TM_DRAW_*`). It goes through the window's matrix to
`DR_VID_PUTSTRING`. The kernel draws the underline and the
strike-through as a bar from the start to the end of the text. The pen
ends after the text.

**Port32 differences:** names compare as bytes (`FEF_DOWNCASE`,
`FEF_ALPHABETIZE` on ASCII), not with the localization's collation. No
greyscale text, no deleting of fonts, no DBCS.

**Not yet:** `GrCharMetrics`, text in graphics strings and paths, the
bars of rotated text, right to left text, `GrDrawTextField`.

**Tests.** ctest `kernel.draw` boots with `FontData/Berkeley.fnt` in
`USERDATA/FONT`. It checks:
- `GrEnumFonts`, `GrCheckFontAvailID`/`Name`, `GrGetFontName`,
  `GrFindNearestPointsize`, `GrGetDefFontID`;
- `GrFontMetrics`, `GrCharWidth`, `GrTextWidth` with space padding;
- the sizes of the file and the nearest one;
- the pixels of drawn text, the pen after it, text at the pen,
  underlined text, text at its baseline.

## 13o. Font drivers (`Graphics/graphicsFontDriver.c`, `Driver/Font/TrueType`, P6)

**Loading.** At the boot, after the font files, the kernel loads the
drivers named in `[system] font` (a list; `truetype.geo` without the
key) from `SP_FONT_DRIVERS` (`SYSTEM/FONT`) with `FONT_PROTO` 3.0. Each
one that loads (`GrInitFontDriver`) goes into the `FONT_DRIVERS` chunk
(`DriversAvailEntry`: maker, geode, strategy) and gets
`DR_FONT_INIT_FONTS` to add its fonts to the font info block. Then the
default font is chosen, so it may be an outline font.

**Calls.** The kernel calls a driver by its maker (`GrCallFontDriverID`)
while it holds the font info block locked, as the asm did with ds. The
registers, as `DriverRegs`:

| Function | In | Out |
|---|---|---|
| `DR_FONT_INIT_FONTS` | `DR_bx` the font info block | |
| `DR_FONT_GEN_WIDTHS` | `DR_bx` the `FontBuf` block (discarded), `DR_esDi` the `GState`, `DR_bpPtr` the matrix (`TMatrix`), `DR_dsSi` the window's `W_TMatrix` or NULL | `DR_bx`, the block locked |
| `DR_FONT_GEN_CHAR` | `DR_dx` the character, `DR_esDi` the locked `FontBuf`, `DR_bpPtr` the `GState` | `DR_esDi`, the block may have moved |
| `DR_FONT_CHAR_METRICS` | `DR_dx` the character, `DR_cx` `GCM_info`, `DR_esDi` the `GState` | `DR_dx.DR_ax` |
| `DR_FONT_GEN_PATH` | `DR_bx` the GState handle, `DR_cx` `FontGenPathFlags`, `DR_dx` the character | |

The kernel unlocks the block of `DR_FONT_GEN_WIDTHS` itself. A
character that is `CHAR_NOT_BUILT` is built by the video driver
(`VidBuildChar` in `vidcomChars.c`): `DR_FONT_GEN_CHAR` through
`GrCallFontDriverID`, and the new address of the font goes back in
`VPS_font`, which the kernel takes over after `DR_VID_PUTSTRING`.

`FontDrFindOutlineData` returns the `OutlineEntry` (header, first or
second part) of the richest style that is a subset of the one asked
for, and the styles still to do. `FontDrGetMaxBufSize` and
`FontDrCharDataSize` are those of the asm.

**The TrueType driver** (`truetype.geo`): the FreeType 1 engine and the
GEOS adapter were C already and are unchanged in their logic.
`Main/mainManager.c` (beside `mainManager.asm`) has `DriverTable`, the
strategy, the bitmap and variable blocks, and the wrappers of
`truetype*.asm`. Port32 changes:
- `TT_Long` and the other FreeType "long" types are 32 bits
  (`TT_LONG_` in `freetype.h`, `tttypes.h`), and `LONG64` is not used:
  the engine was written for a 32-bit `long`.
- The render pool is of 32-bit `Storage`; a profile takes whole
  pointers of it and starts on a pointer boundary (`AlignProfile` in
  `ttraster.c`). The engine counted the profile in `sizeof(long)`,
  which overlapped the profiles where `long` is 64 bits.
- The driver is compiled with `-funsigned-char`, as HighC and ARM have
  it: the adapter counts the GEOS characters 128-255 in `char`.
- `TT_Alloc`/`TT_Free` are the C ones of `ttmemory.c` (a locked block
  each), not `ttmemory_asm.asm`; `TrueType_GrMulWWFixed` is the middle
  32 bits of the 64-bit product, as `ansic_runtime.asm`.
- The adapter takes the kernel's structures (`Internal/fontDr.h`); its
  `FontMatrix` is the `TMatrix` (checked at compile time).
- Without `PRIVDATA` there is no `TTF Cache` file; the cache functions
  then do nothing (they took a null handle before).
- `GeodeGetCodeProcessHandle` is a macro in a geode (`geode.h`): entry
  0 of its own resource handles.

**Kernel routines the driver needs** came along: the EC handle checks
(`Heap/heapErrorCheck.c`: by the handle's type; `ECCheckBounds` and
`ECCheckStack` do nothing, the host guards memory and stacks),
`CFatalError` (fatal `C_FATAL_ERROR`, the code logged) and
`CWarningNotice` (logged), Bezier curves (`graphicsSpline.c`:
`GrDrawCurve`, `GrDrawCurveTo`, `GrDrawSpline`, `GrDrawSplineTo`,
halved in the device until flat) and `GrComment` (stored in graphics
strings, §13p).

**Not yet:** `DR_FONT_GEN_IN_REGION` (no region paths in the kernel; it
fails), the escape functions, greyscale text, `GrCharMetrics` in the
kernel.

**Tests.** ctest `kernel.draw` puts `truetype.geo` into `SYSTEM/FONT`
and `FontData/Ttf/cooper.ttf` into `USERDATA/FONT/TTF`. It checks the
enumeration of the outline font by name, its height and the width of
"I" at 40 point, and the pixels of a drawn "I", which the driver builds
when it is drawn (on every host, arm64 and armhf in qemu too).

## 13p. Graphics strings (`Graphics/graphicsString.c`, `graphicsStringStore.c`, `graphicsStringMisc.c`, P6)

**The elements stay DOS16.** A graphics string holds the elements of
`gstring.def` byte packed and little endian, as goc writes static
graphics strings (`GS*` macros of `gstring.h`) and as documents and
metafiles hold them. The kernel builds them field by field
(`GrGSRecord`) and reads them field by field when it plays them; no
host structure is stored. The size of an element comes from the
element table (`grGSElemInfo`, after `GSElemInfoTab`): a fixed size
and, for a variable element, the offset of its count and what it counts
(bytes, words, coordinate pairs).

**A graphics string is a GState** without a window whose `GS_gstring`
is the handle of a block with the `GString` state (type, the handle
and chunk or VM block, the position, the element read). Every `Gr*`
routine that has an element stores it when `GS_gstring` is set; the
attribute and transformation routines also set the GState, and the
drawing routines move the pen as they would when drawing. Wrappers that
call another routine with an element (`GrDrawHLine`, `GrDrawCharAtCP`,
`GrDrawRelLineTo`) use internal routines, so each call stores one
element.

| Type | Kept in | Position |
|---|---|---|
| `GST_CHUNK` | a chunk of an LMem block, appended | bytes |
| `GST_STREAM` | a file; created with the metafile header (a `GR_COMMENT` "GEOS METAFILE 2.0") | the file position |
| `GST_VMEM` | a huge array of variable size, one element each | elements |
| `GST_PTR` | memory, read only | bytes |

The asm buffered stream and VM elements until a flush; here each
element is written whole when it is stored. A failed write sets
`GSF_ERROR`, and `GrEndGString` returns `GSET_DISK_FULL`.

**`GST_PTR`.** `GrLoadGString(PtrToSegment(p), GST_PTR, PtrToOffset(p))`
cannot carry a host pointer, and `geosport.h` has no segment macros.
Port32 adds `GrLoadGStringPtr(const void *gstring)` (`gstring.h`, under
`GEOS_PORT32`, a Port32 kernel export); `GrLoadGString` with `GST_PTR`
returns 0.

**Playing** (`GrDrawGString`, `GrDrawGStringAtCP`): the element at the
position is copied to the C heap, the variable part (points, text,
bitmaps) to an aligned buffer, and the `Gr*` routine of the opcode is
called. Drawing moves the transformation to the pen and makes it the
default (`GrSaveTransform`, `GrApplyTranslation`,
`GrInitDefaultTransform`) until the string is complete, faults or
reaches a new page. `GSControl` stops before an element of the asked
kind; that element stays read and is played first the next time, as
in the asm. The end of the data reads as `GR_END_GSTRING`. A complete
string goes back to its start. `GrParseGString` calls the callback for
the elements of the asked kind and plays all into the GState if one is
given. `GrCopyGString` into a graphics string copies the elements byte
for byte; into a GState it plays them. `GrGetGStringElement`,
`GrSetGStringPos` (skip, relative, beginning, end) and
`GrDeleteGStringElement` (VM only) work on the position.
`GrComment`, `GrEscape`, `GrLabel`, `GrNewPage`, `GrNullOp` and
`GrSetGStringBounds` store their elements only.

**Bitmaps** are stored whole, as `SliceToString` stores the first
slice: the header, a complex bitmap's palette and the data, packed data
as far as its scan lines go (`GrBitmapStringSize`).

**Not yet:** `GR_DRAW_TEXT_FIELD` (style runs; reading one is a
fault), the `*_PTR` and `*_OPTR` elements, bitmap slices after the
first (callbacks), palettes, patterns, `GrGetGStringBounds` and
`GSF_FIGURE_BOUNDS`, the truncation of a full stream. (`GST_PATH` and
the other path and clip elements: §13q.)

**Tests.** ctest `kernel.draw` records into a chunk and compares the
bytes with the DOS16 elements, plays the string at a position, in parts
with `GSC_OUTPUT`, and from a goc-style byte array (`GrLoadGStringPtr`);
reads elements and moves the position; parses; copies it into a huge
array (one element each, the bytes again) and into a file (the
metafile header), and plays both.

## 13q. Paths (`Graphics/graphicsPath.c`, P6)

**Defining.** `GrBeginPath` saves the GState and points `GS_gstring` at
a graphics string of type `GST_PATH`, whose elements are kept in the C
heap. It starts with `GR_BEGIN_PATH` (the `PathCombineType`), the
transformation and the pen. The `Gr*` routines then store into it
instead of drawing. A path keeps output, transformation, pen and path
elements, not the attributes, as the asm did. `GrEndPath` stores
`GR_END_PATH` and restores the GState (so the attributes set in between
are dropped). It then combines the group with the current path, a chunk
of the GState block (`GS_currentPath`), so `GrSaveState` and
`GrRestoreState` carry it:
- `PCT_REPLACE` keeps the new group alone;
- `PCT_UNION` and `PCT_INTERSECTION` append it;
- `PCT_NULL` (at `GrBeginPath`) removes the path.

The stored elements are the DOS16 elements of §13p.

**Using.** The elements are played to polygons in device coordinates,
one group at a time. They use the GState's own transformation and pen
as the elements change them, and the GState gets both back afterwards
(`W_curTMatrix` is composed again):
- An element that is not a "to" one starts a subpath, and the "to"
  elements continue the subpath from the pen.
- `GR_CLOSE_SUB_PATH` closes the subpath, and a move ends it.
- Rectangles, polygons and ellipses are closed subpaths, and arcs as
  their `ArcCloseType` says. Curves and splines are halved as when
  drawn (`GrSplineDevPoints`).

`GrFillPath` turns each group into a region with the fill rule
(`GrPolygonRegion`). It combines the regions by union or intersection
(`GrPtrRegOp`) and draws the result with the area attributes
(`DR_VID_REGION`). `GrDrawPath` draws each subpath with the line
attributes, a closed one back to its start. `GrGetPathBounds(DWord)`
takes the device bounds of the filled path back through the inverse
matrix, and returns TRUE when there is no path. `GrTestPath` returns
TRUE when the path exists, as the C stub did. In a graphics string,
`GR_BEGIN_PATH`, `GR_END_PATH`, `GR_CLOSE_SUB_PATH`, `GR_FILL_PATH` and
`GR_DRAW_PATH` are stored and played.

**Clip paths.** The GState also keeps `GS_clipPath` and
`GS_winClipPath` as chunks in the same form, each group carrying its
fill rule in `OBP_flags` (0x80 = rule valid, bit 0 = the rule):
- `GrSetClipPath` and `GrSetWinClipPath` combine the current path into
  one of them;
- `GrSetClipRect` and `GrSetWinClipRect` combine a rectangle, which is
  recorded as a path of its own; the window rectangle is in page
  coordinates (a null transformation). The current path stays.
- `PCT_NULL` removes the clip path.

`WinValWinStruct` ANDs the mask with the regions of both
(`GrClipMask`), so the mask lies in both, as in GEOS. A change makes
the window's mask invalid for that GState. The clip paths are saved and
restored with the GState. In a graphics string the four elements are
stored and played.

**Asking.** These work on the path's region, computed through the
GState's window:
- `GrGetPathRegion` and `GrGetClipRegion` return that region (the clip
  region is both clip paths ANDed) in device coordinates, in a block
  with the bounds first. They return 0 when the region is empty or
  there is no window.
- `GrTestPointInPath` tests a document point against the region.

`GrGetPath` returns a path as a graphics string, as the asm did. Before
each `GR_BEGIN_PATH` it puts the group's transformation, as
`GR_APPLY_TRANSFORM`, and its pen. Played into a GState, the string
defines the same path again. `GrSetStrokePath` (it only stores the
element) and `GrGetPathPoints` (it returns 0) are as empty as in the
asm.

Rounded rectangles are closed subpaths (`GrRoundRectDevPoints`). 3-point
arcs are now stored in graphics strings and played (`GR_*_ARC_3POINT*`).
In paths, an open "to" arc continues the subpath at the pen.

**Not yet:** text and bitmaps in paths (they are skipped), a path
defined inside a graphics string.

**Tests.** ctest `kernel.draw`:
- a path defined from lines with nothing drawn, filled, with the pen
  kept;
- a rectangle united with an ellipse, and the bounds;
- `GrTestPointInPath`, `GrGetPathRegion`, and `GrGetPath` played back
  into the GState;
- an intersection of two rectangles;
- a stroked rectangle;
- a filled rounded rectangle and a filled 3-point chord;
- a path filled from a graphics string;
- `PCT_NULL`;
- a clip rectangle that stays when the transformation changes, an
  intersection with a clip path, `GrSaveState`/`GrRestoreState`, a
  window clip rectangle with `GrGetClipRegion`, removal.

## 14. Open

1. **Loading geodes in the kernel** (`GeodeLoad` on `ghal_module`, §7.1).
   Open:
   - the protocol a library needs (glue took it from the library's
     `.ldf`; `GML_proto*` is 0.0, "any", for now);
   - reading the library list without a first load with dummy imports
     (a note or section the loader can read before it resolves
     imports).
2. **DOS16 file formats under dos32**: which structures of VM files and
   documents need converters, given 32-bit handles and 64-bit optrs in
   memory.
3. **GEOS `stdio.h` and the DBCS string functions** on top of the host C
   library.
