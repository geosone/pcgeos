# GHAL: GEOS Hardware Abstraction Layer

**Status:** Basic concept, no implementation yet.

**Scope:** pcgeos only. How GonWare itself abstracts hardware (GWVIDEO etc.) is a different topic; here GonWare is just one of several later hosts.

**Based on:** measurements in this repository (as of 2026-11), the pilot `Library/Math/Float/floatFloatToAscii.c`, `Port32/GonDesk-GEOS-on-GonWare.md`, the inventory `Port32/GHAL-Inventory.md`.

## 1. Goal

GEOS is to run on several underlying systems (*hosts*) without the code above knowing the host:

| Order | Host | Architectures | Remark |
|---|---|---|---|
| 1 | **`dos32`** | i686 | DOS with DPMI (HX/HDPMI32). The existing DOS parts are taken over, but 32-bit. **DOS must stay runnable the whole time.** |
| 2 | **`linux`** | x86-64, i686, aarch64, armhf | first extension; display through SDL3, X11 or fbdev; also the development environment (gdb, ASan) |
| 3 | `win32` / `win64` | i686, x86-64 (aarch64 possible) | Windows API |
| 4 | `gonware` | x86-64, i686, aarch64 | separate concept (`GonDesk-GEOS-on-GonWare.md`); here just another host |
| – | others | – | e.g. macOS or bare metal, if someone needs it |

The **GHAL** is the one place where GEOS touches the host. The host is chosen **at build time**.

## 2. Inventory: where does GEOS touch the hardware today?

### 2.1 What is already abstracted: the driver model

GEOS has always had drivers with a strategy routine and function numbers (`Include/driver.def`, 26 `DRIVER_TYPE_*`). The most important interfaces:

| Interface | File | Functions |
|---|---|---|
| Video `DR_VID_*` | `Include/Internal/videoDr.def` | 34 (rectangle, line, polygon, region, BitBlt, strings, pointer, save-under, XOR, palette, exclusive access …) |
| File system `DR_FS_*` | `Include/Internal/fsDriver.def` | 24 (disk, paths, handle/alloc/path operations, enum, drive lock …) |
| Keyboard `DR_KBD_*` | `Include/Internal/kbdDr.def` | 10 (state, scan code translation, hotkeys …) |
| Mouse `DR_MOUSE_*` | `Include/Internal/mouseDr.def` | 15 (rate, acceleration, calibration …) |
| Swap `DR_SWAP_*` | `Include/Internal/swapDr.def` | 7 |
| Task switcher `DR_TASK_*` | `Include/Internal/taskDr.def` | 6 |
| Power `DR_POWER_*` | `Include/Internal/powerDr.def` | 16 |
| Fonts `DR_FONT_*` | `Include/Internal/fontDr.def` | 6 |
| Stream, serial, parallel, sound, socket, printer … | `streamDr.def`, `serialDr.def`, `socketDr.def`, `printDr.def` … | – |

There are also **two precursors of a host abstraction** in the repository:

- **`Library/GDI`** ("Generic Device Interface", Geoworks 1996): keyboard, pointer and power through registered callbacks (`GDIKeyboardRegister`, `GDIPointerRegister`, `GDIPowerRegister` …), with an implementation `GenPC` and product variants `win32`. The matching drivers are `Driver/Keyboard/GDI` and `Driver/Mouse/GDIPointer`. Geoworks already once detached GEOS input from the PC hardware model.
- **`Library/HostIf`** (blueway 2023, `int 0xA0`): video, sockets and SSL go to the host. Used by `Driver/Video/VGAlike/VGA16`, `Driver/Socket/HstTCPIP` and `Library/SSL`. On the PM branch also `Driver/IFS/HostFS`.

**Consequence:** most of the hardware already sits behind drivers. The GHAL does not have to reinvent the driver model. It mainly has to cover what is **not** behind drivers.

### 2.2 What is not abstracted: direct access

Full, regenerable numbers are in `Port32/GHAL-Inventory.md` (script `Port32/tools/ghal_inventory.py`). Overview:

| Access | Where |
|---|---|
| DOS `int 21h` | `Driver/IFS/DOS` (most), **kernel `FSD`**, **`Loader`/`Loader32`**, `Driver/Task`, `Driver/Swap/XMS`, `Driver/Net/NW`, **kernel `Format`, `File`, `Sys`, `Timer`, `Diskcopy`, `Boot`, `Dosappl`** |
| BIOS `int 10h/15h/16h` | Loader (video detection), kernel `Graphics`, `Timer`, `Thread`, `Boot`, `Sys`; PS/2 mouse drivers |
| EMS/XMS/multiplex `int 67h/2Fh` | `Driver/Swap/EMS`, `XMS`, `Driver/Task`, `IFS/DOS`, kernel `Sys`, `FSD`, `Boot` |
| Port I/O `in`/`out` | video, serial, parallel, sound, mouse drivers; **kernel `Sys`** (PIC, PIT) and `Timer` |
| Interrupt vectors (`SysCatchInterrupt`, `SysCatchDeviceInterrupt`) | kernel `Timer`, `Thread`, `Sys`, `FSD`; drivers IFS/DOS, serial, parallel, SoundBlaster, EMS, task, pen mice, `Library/Math/Compiler` (FPU) |
| BIOS/DOS lock `SysLockBIOS` | IFS/DOS, task, APM power, NetWare, EtherODI, kernel `Sys`/`Thread`, swap |
| Interrupts off (`INT_OFF`/`cli`) | kernel, other libraries, drivers |
| GPMI (PM branch) | `Loader32`, kernel `Heap` |

What is in **bold** lives in the kernel and the loader and therefore outside the driver model. That is the actual core of the GHAL.

## 3. Structure

The GHAL has **two levels**:

```
┌───────────────────────────────────────────────────────────────────┐
│ Apps, components, SpecUI, User                                     │
│ GEOS kernel: Heap, LMem, VMem, Object, Geodes, Graphics, Win, IM,  │
│              thread scheduler, File API, FSD                       │
├────────────────────────────┬──────────────────────────────────────┤
│ GEOS drivers (driver model │ GHAL core (new, small, used only by  │
│ DR_VID_*, DR_FS_*, KBD,    │ the kernel and the drivers):         │
│ MOUSE, stream, sound,      │ boot, memory, time, contexts,        │
│ power, font, socket …)     │ events/critical sections, modules,   │
│ one driver set per host    │ log/debug, exit                      │
│                            │ + host services: display, input,     │
│                            │   files, audio, network (optional)   │
├────────────────────────────┴──────────────────────────────────────┤
│ Host: DOS+DPMI │ Linux (SDL3/X11/fbdev) │ Windows │ GonWare │ …   │
└───────────────────────────────────────────────────────────────────┘
```

- **Level 1, the GHAL core:** a small C API (prefix `ghal_`) that does not exist today. It replaces the direct accesses of kernel and loader from 2.2. Every host must implement it **completely**.
- **Level 2, the drivers:** the existing driver interfaces remain the contract (ported to C, with C signatures instead of registers). There is one **driver set** per host. So that not every host has to rewrite every driver, the GHAL core optionally offers **host services** (display surface, input events, files, audio, network). A **generic driver** on top of these services then serves many hosts:
  - **one** video driver "FB" (VidCom model, software rendering on a linear surface) for VESA LFB, SDL3, X11, fbdev, Windows DIB and GonWare
  - **one** file system driver "HostFS" (`DR_FS_*` over `ghal_fs`) for Linux, Windows and GonWare
  - **one** keyboard and **one** mouse driver on `ghal_input`, similar to what `Library/GDI` already did
  - **one** socket driver on the host sockets, like `HstTCPIP`

Under DOS, on the other hand, the real hardware drivers stay (VGA/VESA, PS/2, IFS/DOS, serial, SoundBlaster …), ported to 32-bit. That is exactly how "DOS takes over the existing DOS parts".

## 4. The GHAL core in detail

| Group | Task | `dos32` | `linux` | `win32/64` |
|---|---|---|---|---|
| `ghal_boot` | startup, arguments, environment, path to `geos.ini`, system directory | DPMI program (PE via HX or LE), replaces `Loader/` | `main()` | `WinMain`/`main` |
| `ghal_mem` | memory blocks for the kernel heap (one host allocation per block, see 4.6), protection of code blocks | DPMI `int 31h` (0501h …) | `mmap` | `VirtualAlloc` |
| `ghal_time` | monotonic clock, wall clock, **tick source** for the scheduler | PIT (IRQ0) + RTC | `clock_gettime`, timer thread | `QueryPerformanceCounter`, timer thread |
| `ghal_ctx` | execution contexts for GEOS threads (see 4.1; implemented, ABI.md §9) | own stacks + context switch | one host thread per GEOS thread | same with Win32 threads |
| `ghal_crit` | critical sections, replaces `INT_OFF`/`INT_ON` (see 4.2) | `cli`/`sti` | kernel lock | kernel lock |
| `ghal_irq` | register asynchronous event sources, replaces `SysCatch(Device)Interrupt` (see 4.2) | real IRQ vectors via DPMI | host thread / event loop calls the handler | same |
| `ghal_hostlock` | lock for non-reentrant host services, replaces `SysLockBIOS` | DOS lock as today | only where needed, e.g. calls that must go to the main thread (4.3) | same |
| `ghal_module` | load code (see 4.4) | own loader, DXE3 modules | own loader, ELF shared objects (optionally `dlopen` for debugging) | own loader |
| `ghal_log` | debug output, assert, crash report, breakpoint | serial / file | stderr, `SIGTRAP` | `OutputDebugString`, `DebugBreak` |
| `ghal_exit` | quit, restart, return to the host | back to DOS | `exit` | `ExitProcess` |
| `ghal_caps` | query capabilities (audio, network, multiple windows …) | – | – | – |

**Host services** (optional, for the generic drivers):

| Service | Content | `dos32` | `linux` | `win32/64` |
|---|---|---|---|---|
| `ghal_display` | modes, surface (pointer, pitch, format), flush rectangles, resize | VESA VBE 2/3 LFB (or the VGA driver directly) | SDL3 / X11-SHM / fbdev | DIB section + `BitBlt` or SDL3 |
| `ghal_input` | events in the **openinput format** `{page, usage, value, source}` (HID usages) | PS/2 or the DOS drivers | SDL/X11/evdev → HID | Win32 messages → HID |
| `ghal_fs` | files, directories, attributes (long name, token, protocol) | (not needed, IFS/DOS) | POSIX + xattrs `user.geos.*` | Win32 `*W` API + NTFS streams (`:geos`) |
| `ghal_audio` | PCM output | (SoundBlaster driver) | SDL3 audio / OSS / ALSA | SDL3 / WASAPI |
| `ghal_net` | TCP/UDP, name resolution | (GEOS's own TCP/IP stack over a packet driver) | BSD sockets | Winsock |

### 4.1 Threads and scheduler

GEOS has its own scheduler (priorities, time slices driven by the timer interrupt). **It stays authoritative**, so GEOS behaves the same on all hosts. The GHAL only provides the contexts:

- **`dos32`:** as today. Own stacks and context switch, preemption via IRQ0.
- **Hosts with threads (Linux, Windows, GonWare):** every GEOS thread becomes a host thread, but **only the thread chosen by the GEOS scheduler runs**. A global kernel lock is handed over on every context switch. Blocking host calls (file, network) release the lock first, so other GEOS threads continue.
- **Preemption on these hosts:** a tick thread sets "reschedule pending", checked at kernel entry points and at the end of critical sections. Hard preemption (a signal on Linux, `SuspendThread` on Windows) is only needed later, if compute loops without kernel calls get in the way.
- **Rejected:** pure user-level contexts (`ucontext`/fibers) on hosted systems. A blocking host call would stop all of GEOS.

### 4.2 Interrupts become events

Today the kernel protects itself with `INT_OFF`/`INT_ON`, and drivers hook interrupt vectors.

- `INT_OFF`/`INT_ON` already are macros (`Include/Internal/interrup.def`, since 2026-11-10 `equ <GHAL_IntOff>` / `<GHAL_IntOn>` from `Include/Internal/ghal.def`) and become **`ghal_crit_enter`/`leave`** (nestable). Under `dos32` that is `cli`/`sti` as before; on other hosts a lock against the "interrupt context".
- **Interrupt context:** under DOS that is the real IRQ handler. On other hosts it is a host thread (timer, event loop, network) that calls the driver's handler between `ghal_irq_enter`/`exit`. That serializes it against the kernel, exactly like a real interrupt.
- `SysEnterInterrupt`/`SysExitInterrupt` and the kernel's wake-up paths keep their logic unchanged.

### 4.3 The host's main thread

SDL, X11 and Win32 expect windows and events to be served on **one particular thread**. Therefore the GHAL **owns the host's main thread** and runs the event loop there. GEOS runs on its own threads. Calls to `ghal_display` are forwarded to the main thread when needed. Under DOS this distinction does not exist.

### 4.4 Loading geodes

Today the GEOS kernel loads its `.geo` files **itself** (own format, own relocation). That stays: the kernel reads the file and hands its contents to **`ghal_module`**, which lays it out, resolves the imports through a function of the kernel (never through the host's dynamic linker or C library) and relocates it. Every host keeps **its own module format** (decided 2026-11-11: "DOS stays DOS"):

| Host | Geode format | Produced by | Loader |
|---|---|---|---|
| `dos32` | **DXE3**, the module format of DJGPP | `dxe3gen -U` from the DJGPP objects | `Port32/ghal/dos32/ghal_module_dxe.c` |
| `linux`, later `gonware` | **ELF shared object** (`ET_DYN`), `-shared -nostdlib -z now` | gcc/clang of the architecture | `Port32/ghal/elf/ghal_module_elf.c` (amd64, i386, arm64, armhf) |
| `win32/64` | (open, presumably PE DLL) | | |

- `cmake/GeosGeode.cmake` (`geos_geode()`) builds a geode in the host's format. A geode has no host C library and no sanitizer or stack-protector runtime; libgcc is linked in. What libgcc itself imports (DXE3 with unwind tables: `__register_frame_info`/`__deregister_frame_info`; ARM: `raise` for division by zero) is provided by the kernel.
- Names are C names on every host (DJGPP's leading underscore is dropped by the loader).
- `ghal.module` loads a test geode on all five builds (linux amd64, i686, arm64, armhf under qemu-user, dos32 on DOS in QEMU): imports by call and by data reference, pointers in data, function pointers, bss, initializers/finalizers, two instances, unresolved imports and broken images.
- The GEOS data of a geode (the `.gp` tables: name, token, attributes, protocol, libraries, exported entries, resource table) is the exported object `__geos_module` in the module (`Port32/ABI.md` §7, `Port32/tools/gpgeode.py`). Still open: `GeodeLoad` of the C kernel on top of `ghal_module` (`Port32/ABI.md` §14.1).
- On Linux additionally a debug mode via `dlopen`, so gdb sees the symbols (or registration through the gdb JIT interface).
- The kernel and the GHAL core are built together as a normal host program: DOS (DJGPP COFF with CWSDPMI), ELF program or `.exe`.

### 4.5 The former loader tasks

`Loader/` (video detection, logo, locating `geos.ini`, loading the kernel, preparing memory) goes into `ghal_boot` and kernel startup. Video detection becomes part of the video driver or of `ghal_display`.

### 4.6 What S0 leaves for real interfaces (status 2026-11-10)

After the S0 macro steps (`Include/Internal/ghal.def`), the kernel and the loaders issue no direct DOS, BIOS, EMS or multiplex interrupt and touch the 8253/8259 only through GHAL macros. The remaining host contacts in the GHAL core area cannot be routed one-to-one; this section fixes how they map.

**GPMI is the pm16 backend of `ghal_mem`, `ghal_irq` and `ghal_fault`.** In the PM branch `Loader32` provides a call table (`KLV_GPMIVectorTable`, `GPMICallType` in `Include/gpmi.def`), and only the kernel uses it, through about 30 wrappers at the end of `Library/Kernel/Heap/heapCore.asm` (about 45 call sites elsewhere in the kernel). No driver or library calls GPMI. These wrappers are therefore the GHAL boundary of the 16-bit protected-mode kernel; the 474 `gpmi` hits of the inventory are mostly the implementation in `Loader32` (204) and declarations in `Include` (138).

In the 32-bit C kernel (flat memory, no selectors) the GPMI calls map as follows:

| GPMI call | C GHAL | Note |
|---|---|---|
| `ALLOCATE_BLOCK`, `RESIZE_BLOCK`, `FREE_BLOCK` | `ghal_mem_alloc` / `_resize` / `_free` | one host allocation per heap block, as in the PM branch; a locked block is a plain pointer, so the heap need not move blocks |
| `CONVERT_TO_CODE_BLOCK` | `ghal_mem_protect(…, GHAL_PROT_RX)` | code blocks become read/execute after relocation |
| `ALIAS`, `FREE_ALIAS` | – | flat model: the alias is the same pointer; write access to code goes through `ghal_mem_protect` |
| `GET/SET_DESCRIPTOR`, `IS_SELECTOR_16_BIT`, `SELECTOR_GET_LIMIT`, `SELECTOR_CHECK_LIMITS` | – (EC: `ghal_mem_size`) | selectors disappear; error checking asks the size of a block |
| `MAKE_PRESENT`, `MAKE_NOT_PRESENT`, `ALLOCATE_NOT_PRESENT_BLOCK` | – | lazy loading of discarded resources via not-present faults; the C kernel loads them explicitly in `MemLock` (as the real-mode kernel does) |
| `GET/SET_EXCEPTION_HANDLER` | `ghal_fault_set_handler` | DPMI exceptions / `SIGSEGV`, `SIGFPE`, `SIGILL` / vectored exception handler |
| `GET/SET_INTERRUPT_HANDLER` | `ghal_irq_*` (4.2) | real IRQs only under `dos32`; elsewhere event sources |
| `GET_INFO` | `ghal_mem_info` | free / total memory, page size |
| `ACCESS/MAP/UNMAP_REAL_SEGMENT`, `RELEASE_SEGMENT_ACCESS`, `MAP_PHYSICALL_ADDRESS` | `dos32` host service `ghal_dos_lowmem` / `ghal_phys_map` | only for `dos32` drivers (VGA at A000h, VESA LFB); not part of the portable core |
| `REAL_INTERRUPT`, `ALLOCATE/FREE_DOS_BLOCK`, `ALLOCATE/FREE_REAL_MODE_CALLBACK` | `dos32` host service `ghal_dos_*` | DOS-specific drivers (IFS/DOS, mouse, packet driver) |
| `STARTUP`, `RESTART_LOADER` | `ghal_boot`, `ghal_exit(GHAL_EXIT_RESTART)` | |

**Port I/O.** About 130 of the roughly 160 port accesses in kernel and loaders belong to handheld and XIP variants that are not built for the PC (`GULLIVER`, `EMMXIP`, `VG230`, `_GPCHW`, `_DOVEHW`, `ZOOMER`: `Loader/load.asm`, `Sys/sysUtilWindow.asm`, `kernelMacro.def`, the RTC code in `Timer/timerMisc.asm`). They are **not ported**; these variants end with the 16-bit code. The active PC accesses are routed: PIC in-service register and NMI status in `Thread/threadException.asm` (`GHAL_PicReadIsr`, `GHAL_NmiStatusAT/XT`); the COM1 probe in `Sys/sysInterrupt.asm` exists only for debugging (`CATCH_MISSED_COM1_INTERRUPTS`) and goes to `ghal_log`. The loader video probe (`Loader/video*.asm`) moves into the video driver or `ghal_display` (4.5) and stays asm only as long as the 16-bit loader exists.

**Kernel APIs that already are the boundary.** `SysCatchInterrupt`/`SysCatchDeviceInterrupt`/`SysReset*` (drivers hook IRQs) and `SysLockBIOS`/`SysUnlockBIOS` keep their API; in the C kernel they are implemented on `ghal_irq` and `ghal_hostlock`. Their call sites in drivers do not change.

## 5. Driver sets per host

| Driver class | `dos32` (from today's DOS parts, 32-bit) | `linux` | `win32/64` | `gonware` (later) |
|---|---|---|---|---|
| Video | VGA/SVGA/**VESA LFB** (from `VGAlike`, `VidCom`) | FB driver on `ghal_display` (SDL3/X11/fbdev) | FB driver on DIB/SDL3 | FB driver on GWVIDEO |
| Keyboard | `Driver/Keyboard` (int 9, layouts) | driver on `ghal_input` | same | same |
| Mouse | PS/2, serial (`Driver/Mouse`) | driver on `ghal_input` | same | same |
| File system | **IFS/DOS** (+ MS7/LFN), IFS/GEOS | **HostFS** on `ghal_fs` | HostFS | HostFS (NSS) |
| Serial/parallel | `Driver/Stream/Serial`, `Parallel` | termios / CUPS file | COM API | – |
| Sound | SoundBlaster (+ new AC'97) | on `ghal_audio` | same | GWSOUND |
| Power | APM / hltpwr | NoPower | NoPower | ACPI via host |
| Swap, task switcher | **dropped** (DPMI memory is enough; the task switcher is a DOS legacy) | dropped | dropped | dropped |
| Fonts | FreeType (`Driver/Font/TrueType`, already C) | same | same | same |
| Network | GEOS TCP/IP (`Driver/Socket/TCPIP`) over a packet driver (`EtherPKT`) | host sockets (`HstTCPIP` model) | Winsock | host sockets |
| Printing | `Driver/Printer` (ported) | output to CUPS | Windows printing | PAPPL |

## 6. Introduction in steps: DOS always runs

**Principle:** after every step GEOS runs under DOS. Linux is added as the first extension.

| Step | Content | Result |
|---|---|---|
| **S0: pull the GHAL into the 16-bit code** | In *today's* asm kernel, put the direct accesses from 2.2 (Loader, kernel `Sys`, `Timer`, `FSD`, `File`, `Boot`, `Thread`) behind GHAL entry points, still in asm. `INT_OFF`/`INT_ON`, `SysLockBIOS` and `SysCatch*Interrupt` go through the GHAL macros and routines. | Unchanged 16-bit GEOS under DOS, but **every host contact is visible in one place** and named. Testable on real hardware and in the usual DOS environments. |
| **S1: 32-bit under DOS** | Port the code above the GHAL to C/32-bit (see pilot). GHAL core `dos32` from the S0 parts via DPMI; DOS drivers to 32-bit (VESA LFB instead of bank switching, keyboard, mouse, IFS/DOS). | GEOS 32-bit under DOS/HX. DOS remains the reference platform. |
| **S2: Linux** | GHAL core `linux`, host services, generic drivers (FB, HostFS, input, sockets); display SDL3 first, then fbdev and X11 | GEOS under Linux; gdb and ASan usable for all platforms |
| **S3: Windows** | GHAL core `win32`/`win64`, same generic drivers | GEOS under Windows |
| **S4: GonWare** | GHAL core `gonware` (see the GonDesk document) | GonDesk |

**Practical note on S1/S2:** a minimal `linux` host without graphics (just core, memory, threads, files) pays off early in S1 as a **test environment** for the ported kernel parts. No emulator is needed, only unit and integration tests with gdb and ASan. The platform order (DOS first) does not change.

## 7. No more assembler: C is the only language

As soon as more than one architecture is involved (amd64, arm64, armhf, i386 …), hand-written assembler no longer makes sense: every routine would have to be rewritten and maintained per architecture. Therefore:

1. **No assembler above the GHAL.** Kernel, libraries, drivers and apps are C (or GOC → C). Today's asm code (about 3.7 M lines) is translated, not rebuilt per architecture. This applies to DOS too (`dos32` is also C, just built for i386).
2. **As little assembler as possible in the GHAL core**, only where technically unavoidable:
   - **Context switch** only for `dos32` (i386, one small routine). Hosted systems need none, because GEOS threads are host threads there (4.1).
   - **IRQ entry stubs under DPMI** (`dos32`): short stubs that jump to C handlers.
   - Nothing else. Atomics and barriers come from C11 (`<stdatomic.h>`), bit tricks from compiler builtins (`__builtin_clz` …).
3. **Optimizations** (e.g. blit and fill routines of the software renderer) first in portable C. Only where measurements demand it, compiler intrinsics (SSE2/NEON) **with a C fallback**; no free-standing assembler.
4. **What assembler guarantees implicitly today must become explicit in C:**
   - **Memory model:** x86 orders memory accesses strongly, ARM does not. Data shared between threads / interrupt context needs C11 atomics or `ghal_crit` sections, no "it'll be fine" assumptions.
   - **Alignment:** armhf tolerates unaligned access only to a limited extent. Read packed structures only through `memcpy` accessors (see data formats in the GonDesk document, 6.3).
   - **Byte order:** all targets are little-endian; still, on-disk formats only through `le16`/`le32` accessors, so a big-endian target remains possible.
   - **Floating point:** `double` everywhere. That also fits armhf, where `long double` is only 64-bit (one more reason against the 80-bit format).
   - **Word size:** 32- and 64-bit targets at the same time. Hence fixed widths for data (`uint16_t`/`uint32_t`) and `uintptr_t`/`size_t` for addresses and sizes.
5. **Architecture matrix in CI:** at least one 32-bit, one 64-bit and one ARM target, e.g. linux-amd64, linux-i386, linux-arm64 and linux-armhf (ARM via QEMU user mode), plus dos32. 64-bit, alignment and memory-model bugs then show up immediately.

Step S0 (section 6) does not contradict this: it only puts the existing 16-bit asm code behind GHAL entry points so DOS keeps running. From S1 on, new code is written in C only.

## 8. Build

**The build system is CMake** (decided 2026-11-10); tests run through CTest. Entry point: the root `CMakeLists.txt`; toolchain files in `cmake/`.

```sh
cmake -S . -B build-dos   -G Ninja -DGEOS_HOST=dos32  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-dos32-hx.cmake
cmake -S . -B build-linux -G Ninja -DGEOS_HOST=linux  -DGEOS_LINUX_DISPLAY=sdl3   # or x11, fbdev
cmake -S . -B build-w64   -G Ninja -DGEOS_HOST=win64  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake
```

| `GEOS_HOST` | Toolchain |
|---|---|
| `dos32` | Open Watcom 2 (LE/PE for DOS extenders) or i686 GCC (DJGPP, or MinGW PE for HX) |
| `linux` | GCC or Clang |
| `win32`/`win64` | MinGW-w64 (GCC/Clang) or clang-cl/MSVC |
| `gonware` | Clang/lld against mlibc |

**Rules:**

1. **`#ifdef GEOS_HOST_*` and host headers only in `ghal/` and in the host driver directories.** Everything else is built identically for all hosts. CI checks this with a script.
2. **Architecture is not host.** x86-64, i686, aarch64 and armhf are handled only through types (`uintptr_t`, `intptr_t`, fixed widths), not through `#ifdef`.
3. **64-bit Windows is LLP64:** `long` is 32-bit there. So **never use `long` for pointers or sizes**; use `intptr_t`/`size_t`. And `wchar_t` is 16-bit there, so never use `wchar_t` as the GEOS character type.
4. Query missing capabilities via `ghal_caps()`; do not infer them from the host name.
5. Every host has its own CI run:
   - `linux` natively (graphics through an offscreen backend),
   - `win*` with Wine or a Windows runner,
   - `dos32` in QEMU with FreeDOS + HDPMI32,
   - `gonware` in QEMU.

**Directory layout (proposal):** new code without an asm original lives under `Port32/` (see the layout rule in `PLAN.md`); C ports of asm files sit next to their asm.

```
Port32/ghal/
  include/ghal/*.h            API of the GHAL core and the host services
  common/                     software renderer, HID tables, helpers
  dos32/  linux/  win/  gonware/
  linux/display-sdl3/  linux/display-x11/  linux/display-fbdev/
Port32/Driver/Host/           generic drivers on the host services (new, no asm original)
  Video/FB  IFS/HostFS  Keyboard/Host  Mouse/Host  Socket/Host  Sound/Host
```

## 9. Open decisions

1. **Really do S0 in 16-bit asm?** Recommendation: yes. It makes all host contacts visible without breaking anything, and every later step builds on it. The alternative is to introduce the GHAL only with the C port, but then without a runnable intermediate result.
2. **Scheduler on hosted systems:** host threads plus kernel lock (recommended) or something else?
3. ~~**Geode format:** own ELF loader (recommended) or host loader (`dlopen`, `LoadLibrary`, HX PE DLLs)?~~ **Decided 2026-11-11:** own loader (`ghal_module`) with the host's module format: DXE3 on `dos32` (DOS stays DOS), ELF on `linux`/`gonware` (4.4).
4. **DOS extender:** HX (PE, with Win32 emulation via DKRNL32 as a bonus) or a classic LE extender (DOS/4GW compatible, e.g. DOS/32A)?
5. **Reuse `Library/GDI`?** Its callback model for keyboard, pointer and power fits `ghal_input` well: use it as a template or keep it as a bridge.
6. **What is dropped for good?** Swap (EMS/XMS), task switcher, CGA/EGA/Hercules, pen devices, PCMCIA. Proposal: all of it in S1.

## 10. Related documents

- `Port32/PLAN.md`: phases and current status.
- `Port32/GHAL-Inventory.md`: generated list of all direct host contacts.
- `Port32/Tests/README.md`: pilot. The shim `fpstack.c` is an example of "code above the GHAL, independent of the host".
- `Port32/GonDesk-GEOS-on-GonWare.md`: the `gonware` host in detail (GWVIDEO, openinput, NSS, Ring 3). The GHAL there is the same as here.
