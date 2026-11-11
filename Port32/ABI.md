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
object `__geos_module` (`Port32/include/geosmod.h`), written by
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
relocation: section 13. Not yet: `@children`/`CObjSendToChildren`
(composites).

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
time: the module loader made them absolute. goc casts a relocation
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
  count.

Not yet:

- Composites and links (`CObjSendToChildren`, `ObjCompAddChild`, ...),
  GCN lists (`TEMP_META_GCN`), detach and quit (`objectShutdown.asm`),
  duplicating resources and saving to state.
- **optrs in chunks that are not objects** (moniker lists, GCN list
  elements, chunk data). Glue relocated them through the geode's
  relocation table; under `goc -cg` they need a relocation list per
  resource (section 14).

## 14. Open

1. **Loading geodes in the kernel** (`GeodeLoad` on `ghal_module`). The
   module format is decided (`GHAL-Concept.md` §4.4) and the `.gp` data is
   in the module (§7). Open:
   - resolving imports by name over the geode's libraries, with a
     protocol check;
   - the protocol a library needs (glue took it from the library's
     `.ldf`; `GML_proto*` is 0.0, "any", for now);
   - the optrs in non-object chunks of a resource, which `goc -cg` has
     to list for the loader (section 13).
2. **DOS16 file formats under dos32**: which structures of VM files and
   documents need converters, given 32-bit handles and 64-bit optrs in
   memory.
3. **GEOS `stdio.h` and the DBCS string functions** on top of the host C
   library.
