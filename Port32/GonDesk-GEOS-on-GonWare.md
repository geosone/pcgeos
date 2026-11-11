# GonDesk: GEOS as the desktop on the GonWare kernel

**Status:** concept paper, no implementation.

**Based on:**

- this repository (as of 2026-11)
- the pilot `Library/Math/Float/floatFloatToAscii.c` (test in `Port32/Tests/`)
- `../GonWare/CONCEPT.md` (section "Outlook: GonDesk – a desktop with GEOS")
- `../GonWare/PLAN.md`

GonWare terms are used as defined there: GDOS, `SERVER`, GLM, Ring 0/3, mlibc, openinput, openfat, NSS, GDS.

## 1. Starting point

**GonWare** is a standalone NetWare-style server operating system, not Linux. It has:

- its own kernel for x86-64, i686 and ARM64, booted via UEFI or, on BIOS PCs, via OpenDuet,
- a DOS-like phase (GDOS), after which `SERVER` loads,
- GLMs (ELF modules) in Ring 0 plus protected Ring 3 processes for .NET, PowerShell and third-party software,
- the license GPLv2-only.

According to `CONCEPT.md`, **GonDesk** is a desktop system of its own on the GonWare kernel with GEOS as the user interface. Already decided there:

| Decision (CONCEPT.md) | Consequence for the GEOS side |
|---|---|
| Cooperation with OpenGEOS, contributions go upstream under Apache 2.0 | Everything created in `pcgeos` stays Apache 2.0 and must not contain GPL code (see section 8). **Deviation (2026-11-10): no PRs to upstream, work only in the fork `geosone/pcgeos`.** |
| ~~Emulated first (x86-16 emulator)~~, native port as a Ring 3 program with a layer for the GEOS memory model | **Decision 2026-11-10: no emulation, a real port; breaking compatibility with old GEOS is allowed.** |
| Put far pointers and 16-bit assumptions behind own types and macros right away | exactly the approach of the pilot (`fpstack.h`, `FFA_stackFrame`), but it must become mandatory |
| GEOS stays a program of its own and **not a GLM** in the kernel address space | GEOS runs in Ring 3, links only against `LIBC.DLL` (mlibc, MIT) and its own libraries |

## 2. Layers

```
┌──────────────────────────────────────────────────────────────────┐
│ GEOS apps: GeoWrite, GeoCalc, FileMgr, Breadbox ...              │ Ring 3
│ Components: Text, GrObj, Spreadsheet, Chart, Math, Parse         │
│ SpecUI: CommonUI (+ Motif/ISUI/NewUI)                            │
│ User:   Gen*/Vis* classes                                        │
│ GEOS runtime: Object · Geodes · LMem · VMem · Graphics · Win · IM│
├──────────────────────────────────────────────────────────────────┤
│ GEOS host layer (GHAL, new, Apache 2.0)                          │
│   host "gonware" │ host "linux" (sdl3/x11/fbdev) │ host "dos32"  │
├──────────────────────────────────────────────────────────────────┤
│ GonWare Ring 3: LIBC.DLL (mlibc), Ring 3 pthreads, ELF loader    │
│   with shared libraries, framebuffer access, input events,       │ phase 6
│   files (FAT/exFAT/NSS), sockets                                 │
├──────────────────────────────────────────────────────────────────┤
│ GonWare kernel + GLMs: GWVIDEO, GWSOUND, openinput, openfat, NSS │ Ring 0
└──────────────────────────────────────────────────────────────────┘
```

The host "linux" is the **development target**. On GonWare, Ring 3 (processes, shared libraries, pthreads in user land) only exists from GonWare phase 6 on. The native GEOS port can be developed under Linux with gdb and ASan right away. Which host is built is **specified at build time** (section 4.5); above that, the code is identical for all hosts.

## 3. No emulation: consequences

GonDesk is **native only**. There is no x86-16 emulator and no "classic" mode.

**Decision 2026-11-10: a real port; breaking compatibility with old GEOS is accepted.** That means:

- **Binary compatibility: none.** Old 16-bit geodes (`.geo`) do not run. Every app is rebuilt from source (GOC) or translated (asm). Apps without source in the repository are dropped.
- **Data compatibility: none.** Old documents (GeoWrite, GeoCalc, GeoDraw, GeoDex …), GEOS fonts, `.ini` files and GStrings need not be read. Formats are redefined cleanly (see 6.3). An import tool for old documents is at most a later, optional extra.
- **Source compatibility: as far as sensible.** This is the *only* compatibility worth having: ~0.83 M lines of GOC apps depend on the GOC/C API (Gen classes, messages, `@object`, `MemLock`, `ObjCallInstanceNoLock` etc.). Changing the API needlessly means touching all of them. Breaks here only where they bring real simplification, and then convertible mechanically (script instead of manual work).
- **Until the native desktop stands (N3), there is no visible GEOS on GonWare.** Intermediate results are shown under Linux with the SDL3 backend and through console tests.

**What remains of `Library/HostIf`:** its split of host services (video, sockets, SSL, resize notifications) is a usable template for the interface of the native host layer (GHAL). The `int 0xA0` protocol itself is not taken over. On the GHAL side SSL goes directly to mbed TLS, because GonWare does not use OpenSSL.

## 4. Native port

### 4.1 What the GonWare hardware layer replaces

On GonWare almost all of `Driver/` goes away. GEOS only talks to the host layer.

| GEOS today | Lines (asm+def) | GonWare replacement |
|---|---|---|
| `Loader/` | ~20 k | ELF start from Ring 3 (`COMMAND`, `AUTOEXEC` starts GonDesk) |
| Kernel `Heap` (physical), `Thread`, `Timer`, `Sys` | ~45 k | Ring 3 memory, pthreads, `clock_gettime` (the kernel side is already done in GonWare: threads, priorities, SMP affinity, timed waits) |
| `Driver/IFS` (DOS, GEOS, RFSD) | ~120 k | GonWare file systems: FAT/exFAT via openfat, **NSS** |
| `Driver/Video` (VGA ... SVGA, VGA16) + `VidCom` | ~138 k | **GWVIDEO** (`VIDEO.GLM` + `*.DRV`, see 4.4). SDL3 under Linux |
| `Driver/Keyboard`, `Driver/Mouse` | ~57 k + mouse | openinput (HID usages, layouts de/us, repeat, lock LEDs are already in the kernel) |
| `Driver/Font` | ~74 k | FreeType (`Driver/Font/TrueType` is **already C/FreeType**, ~27 k lines) |
| `Driver/Socket`, `Driver/Net`, `Library/Socket` | ~130 k | GonWare sockets, TCP/IP **and IPX** |
| `Driver/Printer` + `Library/Spool` | ~175 k | libcups3/PAPPL (planned in GonWare) |
| `Driver/Sound` | ~26 k | **GWSOUND** (`OSS.GLM`, OSS v4 API `/dev/dsp`, mixer) |
| `Driver/Stream`, `Power`, `Swap`, `Task`, `DMA` | ~30 k | dropped; power via GonWare's ACPI services |

### 4.2 GEOS and NetWare fit together

The repository already has `Driver/Net/NW`, `Driver/IFS/DOS/NetWare` and `Library/Net` (NetWare network library). GEOS was meant to be a client in NetWare networks.

On GonWare this can become a native integration:

- **NSS volumes** as GEOS drives.
- **GDS identity** as the GEOS user. Ring 3 processes on GonWare run with a GDS identity anyway, and NSS trustees apply directly.
- **Extended attributes:** today GEOS stores long names, token/file type, release and protocol in `@DIRNAME.000` or in the GEOS file header. Under **NSS** they belong in its name spaces and extended attributes. On FAT/exFAT a sidecar file remains as the fallback.

### 4.3 What is ported, i.e. rewritten in C

This is the same as for the Linux variant and does not depend on the host:

| Part | Lines (asm+def+ui) |
|---|---|
| Kernel `Object`, `Geodes`, `LMem`, `VMem`, `Local`, `File` API, `Initfile` | ~135 k |
| Kernel `Graphics` / `Win` / `IM` | ~103 k / ~18 k / ~11 k |
| `Library/User` | ~169 k |
| `Library/SpecUI/CommonUI` (Motif, ISUI, NewUI only 4–7 k each on top) | ~291 k |
| `Library/Text` | ~149 k |

Up to the first native desktop that is **just under 0.9 M lines**. After that come the components (~0.4 M) and the asm apps.

The **~0.83 M lines of GOC** in `Appl/` (219 k of them in `Appl/Breadbox`) are not rewritten, only rebuilt. That needs a new `goc` backend.

### 4.4 GWVIDEO and GWSOUND (already defined in GonWare)

On 2026-11-10 GonWare defined its display and sound architecture (`CONCEPT.md`, "Display and sound drivers"; `PLAN.md` phase 3). Both fit GonDesk directly:

- **GWVIDEO** = `VIDEO.GLM` (core) + one `*.DRV` per graphics card. The model is **derived clean-room from the GEOS video driver model** (`Driver/Video` + `VidCom`: standard operation table, generic implementations for simple drivers).
  - **Tier 1** (every driver): modes, linear framebuffer, 2D primitives (fill, BitBlt, bitmap/glyph, line, palette, save-under for the pointer, XOR). A software renderer does this for the dumb framebuffer via GOP, which always works. First real targets: virtio-gpu, bochs-display.
  - **Tier 2** (optional): 3D, DRM/GEM-like. Not needed for GonDesk.
  - **Consequence for GonDesk:** the GEOS kernel `Graphics` renders through exactly these operations today. The native graphics port can therefore put its output almost 1:1 on GWVIDEO Tier 1 instead of computing everything into a framebuffer itself. On hosts without these operations (SDL3, X11, fbdev, VBE) the GHAL's software renderer takes over (4.5).
- **GWSOUND** = `OSS.GLM` (OSS v4 API: `/dev/dsp`, mixer, `SNDCTL_*`) + one `*.DRV` per card. GEOS's `Driver/Sound` (~26 k) is replaced by the OSS API; under Linux there is OSS emulation or SDL3 audio.

**License:** because GWVIDEO only takes over the *model* of GEOS, not its code, the separation stays clean: GonWare code (GPLv2) contains no GEOS code (Apache 2.0), and GonDesk talks to GWVIDEO/GWSOUND only through Ring 3 interfaces.

### 4.5 Host layer (GHAL) and build targets

> **`Port32/GHAL-Concept.md` is now authoritative** (GEOS Hardware Abstraction Layer, independent of GonWare; hosts `dos32`, `linux`, `win32/64`, later `gonware`). This section only shows the first version from the GonDesk point of view.

The separation between GEOS and the system below is **hard**: nothing above the GHAL knows the host. The host is chosen at build time, not detected at run time.

#### Build targets

| `GEOS_HOST` | Architectures | Compiler/toolchain | Purpose |
|---|---|---|---|
| `linux` | x86-64, i686, aarch64 | GCC or Clang, glibc/musl | **development**: gdb, ASan/UBSan, fast cycles, CI |
| `gonware` | x86-64, i686, aarch64 | Clang/lld against mlibc (`LIBC.DLL`), toolchain files from GonWare | **target**: GonDesk |
| `dos32` | i686 | Open Watcom 2 or GCC (DJGPP, or MinGW PE for HX) | DOS with DPMI (HX/HDPMI32): classic PCs, retro hardware |

For `linux` the display is selected as well:

| `GEOS_LINUX_DISPLAY` | Display | Input | Use |
|---|---|---|---|
| `sdl3` (default) | window under X11/Wayland or KMS-DRM | SDL events | everyday development |
| `x11` | Xlib/XCB with MIT-SHM | X11 events | without SDL, old systems, remote via SSH X forwarding |
| `fbdev` | `/dev/fb0` | evdev (`/dev/input/event*`) | console without a graphics server; closest to GonWare (framebuffer + raw events) and thus tests its path |

Example (CMake, like GonWare):

```sh
cmake -S . -B build-linux   -G Ninja -DGEOS_HOST=linux -DGEOS_LINUX_DISPLAY=sdl3
cmake -S . -B build-fb      -G Ninja -DGEOS_HOST=linux -DGEOS_LINUX_DISPLAY=fbdev
cmake -S . -B build-gw-a64  -G Ninja -DGEOS_HOST=gonware \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-gonware-aarch64.cmake
cmake -S . -B build-dos32   -G Ninja -DGEOS_HOST=dos32 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-dos32-hx.cmake
```

Several display backends under Linux at once (e.g. as loadable modules, selected by an environment variable) are possible later but not needed. The basic rule stays: **one host per build**.

#### GHAL interfaces

The GHAL is a narrow C API (prefix `ghal_`) in its own headers. Each group has one implementation per host.

| Group | Content | `linux` | `gonware` | `dos32` |
|---|---|---|---|---|
| `ghal_mem` | request and release pages/blocks | `mmap` | mlibc/syscalls | DPMI memory (`int 31h`) |
| `ghal_thread` | threads, mutex (recursive), cond, semaphore, TLS, priorities | pthreads | Ring 3 pthreads | **own scheduler** (DOS has no threads): context switch + IRQ0 timer, like the old GEOS kernel |
| `ghal_time` | monotonic clock, wall clock, timers | `clock_gettime`, `timerfd` | `clock_gettime` | PIT/RTC |
| `ghal_file` | files, directories; the GEOS **attributes** (long name, token, protocol) are the DOS16 `GeosFileHeader` at the start of the file on every host (ABI.md §13c) | POSIX | POSIX | `int 21h`, 8.3 names as DOS16 GEOS |
| `ghal_module` | load libraries/apps, resolve symbols | `dlopen` (ELF `.so`) | `dlopen` (ELF `.DLL`) | PE DLLs via HX `DPMILD32`, or DXE3 with DJGPP, or statically linked |
| `ghal_display` | modes, surface (pointer, pitch, format), flush rectangles, optional 2D operations | SDL3 / X11-SHM / fbdev | **GWVIDEO Tier 1** | VESA VBE 2/3 with linear framebuffer via DPMI |
| `ghal_input` | events in the **openinput format** `{page, usage, value, source}` | SDL/X11/evdev → HID usages | openinput directly | openinput (runs under DOS too, per the GonWare plan) or PS/2, `int 33h` |
| `ghal_audio` | PCM output | SDL3 audio or OSS/ALSA | GWSOUND (OSS) | Sound Blaster / AC'97 (low priority) |
| `ghal_net` | TCP/UDP sockets, name resolution | BSD sockets | BSD sockets (+ IPX) | packet driver + Watt-32 or lwIP; optional |
| `ghal_tls` | TLS connections | mbed TLS | mbed TLS | mbed TLS (optional) |
| `ghal_log` | debug output, assertions, crash report | stderr, `backtrace` | serial/console | serial port, file |

(The authoritative grouping, e.g. `ghal_ctx`/`ghal_crit` instead of `ghal_thread`, and `ghal_fs` instead of `ghal_file`, is in `GHAL-Concept.md`.)

**Two formats are fixed for all hosts**, so the layers above need not translate anything:

- **Input:** the openinput format (HID usages). Linux backends translate to it, GonWare delivers it directly, under DOS openinput itself delivers it.
- **Display:** the 2D operations of GWVIDEO Tier 1 (which themselves derive from the GEOS/VidCom model). If a host lacks them (SDL, X11, fbdev, VBE), a shared software renderer in the GHAL does the work on the framebuffer.

#### Rules

1. **`#ifdef GEOS_HOST_*` only inside `ghal/`.** A host `#ifdef` elsewhere is a build error (checked by a script in CI).
2. **No host headers outside `ghal/`**: no `<unistd.h>`, `<SDL3/...>`, `<dos.h>`, no GonWare syscall headers.
3. The **architecture** (x86-64, i686, aarch64) is independent of the host and handled only through types (`uintptr_t`, fixed widths), not through `#ifdef`.
4. Capabilities a host lacks are **queried** (`ghal_caps()`, e.g. "audio", "network", "dynamic loading"), not guessed from the host name.
5. Every host has a **test run in CI**: `linux` fully, with graphics tests through an offscreen backend; `gonware` in QEMU like the GonWare boot tests; `dos32` in QEMU or DOSBox-X with FreeDOS + HDPMI32.

#### Directory layout

```
CInclude/ghal.h              shared API
Ghal/
    Common/                  openinput queue and tables, helpers
    Posix/                   mem, thread, file, time, net ... (Linux)
    Display/                 x11, null; later sdl3, fbdev
    GonWare/                 mlibc, GWVIDEO, openinput, NSS attributes
    Dos32/                   DPMI, scheduler, VBE, int 21h, PS/2
cmake/
    toolchain-gonware-{x86_64,i686,aarch64}.cmake
    toolchain-dos32-hx.cmake   toolchain-dos32-djgpp.cmake
```

#### Specifics of `dos32`

- **Threads:** DOS has none. The GHAL brings a small scheduler of its own, preemptive via the timer interrupt. This is the only host where the GHAL is more than a thin wrapper.
- **DOS is not reentrant:** file access from several GEOS threads must be serialized in the GHAL (a DOS lock, as in the old GEOS kernel).
- **i686 only.** No 64-bit DOS target; since the code is 64-bit clean, one can come later via Dos64-stub.
- **License:** HX is freeware with sources, DJGPP and Open Watcom have free licenses; none of it is mixed into the Apache 2.0 code, the GHAL `dos32` parts remain our own code.

## 5. Mapping to GonWare mechanisms

| GEOS | GonWare counterpart | Remark |
|---|---|---|
| Geode (`.geo`) with `.gp` exports and protocol number | **Ring 3 ELF shared object** (`.DLL`, like `LIBC.DLL`) | The versioning principle is the same as the GLM ABI (export with version number, module names the required API version). GEOS protocol numbers map 1:1. **No GLMs.** |
| GEOS thread with event queue | Ring 3 pthread (via `LIBC.DLL`, GonWare phase 6) | The GEOS thread model stays. Priorities and affinity already exist in GonWare (interface 1.21/1.22) |
| Semaphores, `ThreadGrabThreadLock` | `pthread_mutex` (recursive), `sem_t` | The recursive mutex is still missing in the Ring 0 POSIX layer (`PTHREAD_MUTEX_NORMAL` only) but is needed for Ring 3 anyway |
| The whole GEOS system | **one** Ring 3 process (session) | GEOS has no internal memory protection. A crash hits the session, not the server |
| Video driver strategy (`DR_VID_*`) + `VidCom` | **GWVIDEO Tier 1** (modes, linear framebuffer, 2D primitives) | The operations are designed after the GEOS/VidCom model and therefore fit almost 1:1. Access from Ring 3 is still open (section 7) |
| Sound driver (`Driver/Sound`) | GWSOUND via the OSS API | `/dev/dsp` + `SNDCTL_*` from Ring 3 |
| `IM` (input manager) | openinput event stream to the Ring 3 process | HID usages → GEOS `KeyboardEvent`/`PtrEvent`. Keyboard layouts come from openinput instead of `Driver/Keyboard/*` |
| GCN, clipboard, drag & drop | stay GEOS-internal | The session is one process; a connection to the host is only needed later |
| `.ini` | file in the GonWare file system, later perhaps GDS attributes per user | |

## 6. Technical key points of the native port

These apply to all three GonWare architectures.

### 6.1 Memory and handle model

This is the "layer for the GEOS memory model" from CONCEPT.md. Without the compatibility constraint it can become much simpler:

- **Handles: 32-bit**, indices into a table. No more 64 k limit.
- **`optr` = 64-bit (handle 32 : chunk 32).** The concept stays (addressing objects by IDs, messages across threads, storing in documents), just wider.
- **Blocks no longer need to move.** Moving and compacting the heap was a consequence of 640 KB and real mode. With virtual memory a block can stay at its address. `MemLock`/`MemUnlock` stay as API (source compatibility) but become cheap: lock returns the fixed pointer, unlock only counts for debug checks. Swapping (`Driver/Swap`) goes away completely.
- **LMem chunks** may keep moving within their block, because object blocks depend on that (chunks grow and shrink). ChunkHandles become 32-bit indices. The rule "re-fetch pointers after `LMemAlloc`" stays. Alternative (bigger break): every chunk its own allocation; that removes the rule but costs memory and locality. **Recommendation:** keep the block model.

### 6.2 Types and macros right away

As CONCEPT.md requires, a central header set is needed, e.g. `CInclude/geosport.h`:

- `GEOS_FAR`/`_far`/`_pascal` become empty macros.
- `SegPtr` and `MemLock` become native pointers.
- `word`/`dword` stay fixed widths; **addresses never in `word`/`dword`**. That matches the GonWare rule "`uintptr_t` instead of fixed sizes".

### 6.3 On-disk data formats

The old formats (packed 16-bit structures) are **not** taken over. New formats:

- **VM files (documents):** a new format with fixed field widths (32/64-bit, little-endian) and a version header. It must be interchangeable between i686, x86-64 and ARM64, so **still** separate in-memory structure and disk format via accessors (`memcpy`/`le32toh`). No direct casting onto disk structures, also because of alignment and the weaker memory model on ARM64 (GonWare rule "clean C11 atomics and barriers").
- **Fonts:** TrueType/OpenType via FreeType only. The GEOS-specific formats (Nimbus, Bitstream, bitmap fonts in `FontData/`) and `Driver/Font/Nimbus`, `Bitstream` are dropped.
- **Graphics metafiles (GStrings):** a new, wider format. Coordinates may exceed 16 bits (GEOS graphics is limited to ±32 k or `WWFixed` today; for large screens and documents the break pays off here).
- **Configuration:** the `.ini` syntax can stay, the keys are reorganized. Per user in the user directory, later perhaps GDS attributes.
- **Text:** Unicode internally instead of SBCS/DBCS (see 6.7).

### 6.4 Floating point

A finding from the pilot:

- On x86-64 and i686, `long double` is the x87 extended format and thus bit-identical to GEOS-80.
- On **ARM64, `long double` is a 128-bit quad** (in software). The `memcpy` trick from `fpstack.c` does not work there.

Since bit identity with old GEOS is no longer required, there are two clean options:

| Option | Advantage | Disadvantage |
|---|---|---|
| **`double` (IEEE binary64)** everywhere | in hardware on all three architectures, fast, simple; same results everywhere | 15–17 instead of 18–19 significant digits |
| SoftFloat `extF80` | same precision as GEOS-80 | slower, more code |

**Recommendation: `double`.** GEOS formatting only shows `DECIMAL_PRECISION` = **15** digits anyway (`CInclude/math.h`). The extra precision of GEOS-80 was never visible to the outside. The float *stack* as an API (Forth-like, `FloatPush`/`FloatAdd` …) can stay for source compatibility of Spreadsheet/Parse/Chart but computes with `double` internally.

### 6.5 Messages

- Instead of the register ABI (`ax`/`cx`/`dx`/`bp`, carry), handlers get a C signature; the return value replaces the carry.
- Dispatch through class tables stays.
- Message numbers: 32 instead of 16 bits (class base + offset), so today's allocation across class levels no longer gets tight.
- Asm special paths (handlers passing registers through, `@callsuper` with manipulated registers) are dropped; the GOC side is hardly affected because it already uses C signatures today.

### 6.6 Toolchain

GonWare uses Clang/lld, and `tools/gdos-cc.sh` already exists.

| Tool | Change |
|---|---|
| `goc` | new backend next to `COM_WATCOM`/Borland/MSC/HighC (`Tools/goc/symbol.c`), target Clang |
| `uic` | output as C data instead of asm `.rdef` (affects ~460 k lines of `.ui`) |
| `esp`, `glue` | dropped |
| `swat` | replaced by gdb, under GonWare via the GDB stub, under Linux natively |

### 6.7 Text and character set

- **Unicode internally.** SBCS with code pages and the DBCS variants are dropped.
- The DBCS branches in the source (`*DBCS` directories, `#ifdef DO_DBCS`) show where 16-bit characters are processed today. They are the best template for the conversion.
- Choice between **UTF-16** (close to DBCS, little rework) and **UTF-32** (simple indexing, more memory); UTF-8 only at the edges (file names, network, import/export).

### 6.8 What else can go

With the break, legacy can be removed instead of ported:

- Swapping (`Driver/Swap`), EMS/XMS, task switcher (`Driver/Task`), DOS file system drivers.
- All video drivers except the one framebuffer driver; all printer drivers except output to PAPPL/CUPS.
- Several SpecUI variants: initially only **Motif** (or CommonUI + one look); ISUI, NewUI, PM, BBoxUI later or never.
- Hardware-specific parts (pen mice, Zoomer/palmtop special cases, the `GDI` library for Win32 products).

## 7. What GonWare must provide for GonDesk

These points are not yet explicit in GonWare's PLAN.md:

1. **GWVIDEO for Ring 3:** the driver side is planned (GWVIDEO, see 4.4). Still missing is **access from Ring 3**: set a mode, map the linear framebuffer (or an offscreen surface plus flush) into the process, and possibly use the Tier 1 primitives by call. Plus a handover of the display between the kernel console (`fbcon`) and the Ring 3 program, like the screen switcher with Ctrl+Esc. "LVGL on the framebuffer" (phase 6) needs the same interface, so it can be defined jointly.
2. **Input events for Ring 3:** forward the openinput stream (`{page, usage, value, source}`) to the foreground process.
3. **Ring 3 pthreads with recursive mutex and timed waits** (phase 6, "user libc must grow pthreads").
4. **Ring 3 ELF loader with shared libraries** (phase 6). GEOS loads libraries and apps dynamically, so `dlopen` is needed.
5. ~~xattrs or NSS name spaces from Ring 3 for the GEOS file attributes.~~ Not needed: the attributes are in the file (decided 2026-11-11, ABI.md §13c).
6. **Sound:** GWSOUND with the OSS API is planned. `/dev/dsp` from Ring 3 is enough for GEOS. Not urgent; GEOS works without sound.
7. **Graphics acceleration:** not needed. GEOS draws in software and is built for 2D. GWVIDEO Tier 2 (3D) does not concern GonDesk.

Hence the dependency: **a native GonDesk on GonWare is only possible after GonWare phase 6.**

## 8. Licenses

| Part | License | Relation |
|---|---|---|
| GonWare kernel, GLMs | GPLv2-only | separated by the Ring 3 boundary (syscalls) |
| OpenGEOS (`pcgeos`) | Apache 2.0 | standalone program |
| mlibc (`LIBC.DLL`) | MIT | may be linked against Apache 2.0 |
| FreeType | FTL (BSD-like) or GPLv2 | **choose FTL** |
| SoftFloat | BSD-3 | no issue |
| SDL3 (Linux development backend) | zlib | no issue |

**Important:** Apache 2.0 is **not** compatible with GPLv2-only, only with GPLv3. GEOS code may therefore be linked neither into a GLM nor into a GPLv2 library, and no GPLv2 code may enter the Apache 2.0 part. CONCEPT.md gets this right ("not a GLM in the kernel address space"). For the **host layer** this means: the GonWare side (syscalls) stays GPL in the kernel, the GEOS side (backend in Ring 3) is Apache 2.0 and talks to the kernel only through syscalls or mlibc.

## 9. Phases

Aligned with GonWare's PLAN.md:

| GEOS phase | Content | Needs from GonWare |
|---|---|---|
| **N0** toolchain + GHAL skeleton | `goc` backend for Clang/GCC, `uic` → C, `geosport.h`, CInclude 64-bit clean; CMake with `GEOS_HOST`, GHAL headers, host `linux` (sdl3) first | nothing |
| **N1** runtime kernel natively under **Linux** | Heap/LMem/VMem/Object/Geodes/Thread/File (~135 k) | nothing |
| **N2** graphics under Linux | Graphics/Win/IM + FreeType; `ghal_display` for sdl3, then fbdev and x11 | nothing |
| **N3** UI under Linux | User + CommonUI + Motif → first native GEOS desktop, `Appl/SDK_C` runs | nothing |
| **N4** host `gonware` | GHAL for GonWare: mlibc, GWVIDEO, openinput, NSS attributes | phase 6 complete |
| **N4b** host `dos32` | GHAL for DOS/DPMI: scheduler, VBE, int 21h, PS/2 or openinput | nothing (possible any time after N3) |
| **N5** text, GOC apps | Library/Text, rebuilding the Breadbox apps etc. | – |
| **N6** components and asm apps | GrObj, Spreadsheet, Chart ... FileMgr, GeoWrite | – |
| **N7** NetWare/GDS integration | NSS volumes, GDS login, printing via PAPPL | phase 5/7 |

(The pcgeos-wide order — `dos32` first, then `linux` — is in `PLAN.md`; this table is the GonDesk view.)

**The work can be parallelized:** N0 to N3 do not depend on GonWare and can start right away while GonWare goes through phases 2 to 6. GonWare is currently at phase 2 (SMP, scheduler); Ring 3 comes in phase 6.

**Order of magnitude** (as stated for the other variants):

- Up to N3, the visible native desktop: ~0.75 M lines of asm to translate, i.e. several person-years.
- Up to N6: 10+ person-years.
- ARM64 and i686 cost **hardly any extra effort** if 6.2 to 6.4 are followed from the start.

## 10. Open decisions

1. **How far source compatibility?** Proposal: keep the GOC/C API as stable as possible; breaks only with a conversion script.
2. **LMem:** keep the block model with moving chunks (recommended) or single allocations?
3. **Unicode:** UTF-16 or UTF-32 internally?
4. **Float:** `double` (recommended) or `extF80`?
5. **Import of old documents:** leave out entirely or later as a separate tool?
6. **Multi-session:** one GEOS session per GDS user, e.g. via remote access? Natural for a server operating system.
7. **First target hardware for GonDesk:** QEMU, a PC with GOP, or an ARM64 board (Raspberry Pi via device tree).

## 11. Relation to the pilot `Library/Math/Float/floatFloatToAscii.c`

- The method is confirmed: keep the structure 1:1 and replace helper routines with a shim. For the first pass this remains sensible, because it allows checking against the original.
- **With the compatibility break, three things change:**
  - `fpstack.c` computes with `double` instead of the x87 `memcpy` trick (6.4), so it also runs on ARM64.
  - The deliberately preserved **QUIRKs** (truncation after rounding, flag overwrite on the fallback to scientific notation, negative exponent with `DONT_USE_SCIENTIFIC`) may now be fixed.
  - The ABI question of buffer offsets (`FFA_decimalPoint == 0` ambiguous) is solved freely, e.g. with pointers or `-1` for "not present".
- The pilot is C11 with `_Thread_local` and therefore also builds with Clang for GonWare/mlibc once Ring 3 TLS exists.
