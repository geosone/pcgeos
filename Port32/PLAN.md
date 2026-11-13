# Port32: Work Plan

**As of:** 2026-11-10.

**Belongs to:** `Port32/GHAL-Concept.md` (architecture). The pilot is `Library/Math/Float/floatFloatToAscii.c` (test in `Port32/Tests/`). `Port32/GonDesk-GEOS-on-GonWare.md` describes a later host.

**Goal:** Free GEOS from hard-wired 16-bit assembler and rebuild it in C, on top of a GEOS Hardware Abstraction Layer (GHAL). Hosts, in this order:

1. `dos32` (DOS must keep working the whole time)
2. `linux` (first extension)
3. `win32` / `win64`
4. later `gonware`

Architectures: amd64, i386, arm64, armhf.

**Language rule:** everything in the repository (documents, comments, commit messages, generated reports) is written in English.

**Layout rule (2026-11-10):** a C port of an asm file goes **next to the asm it replaces** (e.g. `Library/Math/Float/floatFloatToAscii.c` beside `floatFloatToAscii.asm`, with a `CMakeLists.txt` in that directory). `Port32/` holds **only new material**: documents, tools, tests, and new code without an asm original (GHAL core, generic host drivers). The CMake entry point is the repository root (`CMakeLists.txt`, toolchain files in `cmake/`).

---

## 1. Upstream situation (bluewaysw/pcgeos, issues and PRs reviewed 2026-11-10)

Upstream has 450 issues (199 open) and 811 PRs (723 merged, 19 open). The most active contributors since 2025 are bluewaysw, HubertHuckevoll, nschulux, jirkakunze, mgroeber9110 and rabe-soft.

### 1.1 What matters directly for the port

| Topic | Issue/PR | State | Meaning for us |
|---|---|---|---|
| **Protected mode** | #665, branch **`665-protected-mode-2026`** | open, active (last commit 2026-11-08 `56e18c03`), 32 commits ahead of `master`; **356 files, +58,121 / −35,718 lines**, of which kernel + `Loader32` 81 files (+29,137 / −14,409) | "GEOS32": GEOS runs in **16-bit protected mode** under **HDPMI16** (selectors instead of segments), with a new **`Loader32`** and **GPMI** (GEOS Protected Mode Interface, a vector table over DPMI: blocks, aliases, descriptors, interrupt and exception handlers, DMA buffers …). Plus a `swat` `Stub32` and changes in kernel, User, SpecUI, Text, Spool and video drivers. Preemptive multitasking works again since 2026-11-05. Target: "6.1 release in the long run". |
| Loader32 error reporting | #1210 | open | The maintainer wants to **keep `swat` as the central debugging tool**. Relevant for our tooling plan. |
| Host interface (Basebox) | #829 (done), #149 / PR #176, #1050, PR #1246 (open, on the PM branch) | active | `Library/HostIf` + `Driver/Socket/HstTCPIP` + SSL via the host. The PM branch adds **`Driver/IFS/HostFS`**, a host file system via Basebox. **This already is the "host services + generic drivers" pattern of the GHAL concept.** |
| 16-bit variant stays | #672, #960 | open | 8086/8088/V20/286 are to stay as a separate build variant. #960 says it "requires some concept how the configuration should happen". **A build-variant concept is missing there; the GHAL build configuration can provide it.** |
| Host-side tests | #1239 | open | Wanted: tests with a standard location, invocation, reporting, a "documented API for … substituting GEOS services". **Matches our `linux` test host and the Math pilot exactly.** |
| Toolchain | #983 (newer Open Watcom), #117/#120/#55 (tools on 64-bit Linux), #72 (ia16-gcc), #28/#162/#393/#1147 (Watcom/glue) | partly open | The toolchain is an old 2020 OW snapshot and fragile. The C port needs GCC/Clang **in parallel**, without disturbing the existing OW chain. Upstream branch `983-fix-watcom-latest-incompatibilitites` makes the tools build with current OW (merged into `ghal`, see P0). |
| XMS swap overflow | #1209 | open | An example of 16-bit limits in the swap driver. The whole class disappears with 32-bit DPMI. |
| Keep CGA | #456 | open | Belongs to the 16-bit variant, not to the port. |

### 1.2 Conclusions

1. **Upstream has already decided on protected mode**, but on 16-bit PM first. Our plan must **build on that branch**, not next to it. GPMI is already a small abstraction for memory, descriptors and interrupts. It becomes the DOS side of `ghal_mem` / `ghal_irq`; we do not reinvent it.
2. **Upstream has three variants**, and the GHAL build configuration must carry all of them:
   - **real16**: today's GEOS, 8086 to 286, #672
   - **pm16**: GEOS32 from #665, HDPMI16
   - **c32/c64**: our port, `dos32` / `linux` / `win` / `gonware`
3. **HostIf/HostFS** show that host services with generic drivers are accepted upstream. The GHAL host services continue this, without being tied to Basebox.
4. **`swat` stays** for the DOS variants (maintainer's wish). gdb is added for `linux` / `win`. Longer term, evaluate a `swat` stub on top of the GHAL.
5. **Host-side tests (#1239)** are the natural first piece of work.
6. The old mirror `gitea.disconnected-by-peer.at/bluewaysw/pcgeos` lacks the PM branch. **The new full mirror `gitea.disconnected-by-peer.at/dbp/pcgeos` has it** (including `665-protected-mode` and PR refs); it is the remote `dbp` in the clone.

---

## 2. Phases

Every phase ends with a verifiable result. **After every phase GEOS still runs under DOS.** The related upstream topic is noted for each phase.

**Way of working (decided 2026-11-10):**
- Work happens **only in the fork `github.com/geosone/pcgeos`**, branch `ghal` (on top of `665-protected-mode-2026`).
- **No pull requests to upstream.** **One issue** upstream is created only **once something runs** (at the earliest after P5, e.g. the GEOS kernel in C under DOS/Linux). It points to the fork so upstream can take a look. No issues before that.
- Upstream is only **watched and pulled in**: new commits on `master` and the PM branch are merged into `ghal` regularly.
- "Related upstream topic" below only says which upstream topic a phase connects to.

### P0: Foundations

- ~~Make the PM branch available, set up the fork~~ **done 2026-11-10**: remote `geosone` (SSH, because the global git `insteadOf` rule redirects `https://github.com` to Gitea), PM branch pushed to the fork, work branch `ghal`.
- ~~Build the GEOS toolchain~~ **done 2026-11-10**: Open Watcom 2.0 (May 2026) in `/opt/watcom`; the Linux tool builds failed on "`_WCI86FAR` is reserved" (#983), fixed by merging upstream branch `983-fix-watcom-latest-incompatibilitites` into `ghal`. `pmake`, `esp`, `glue`, `goc`, `uic`, `loc`, `swat` build for Linux. All built tools were smoke- and function-tested; fixed: pmake segfault on every error exit (`DieHorribly` lacked `_LINUX`), geodump (DOS upper-casing, `/` options), grev endless loop at EOF, sym2dis crash without argument. The full target build (`Installed/`, 810 geodes incl. the real16 kernel/`Loader` and the pm16 `Kernel/GEOS32`/`Loader32`) needed two more fixes: `<ansi/...>` includes (case) and a pointer-keyed hash in `Tools/utils/localize.c` that made `.rsc`/`.vm` order random.
- **Reproducible builds (2026-11-10):** run the build through `Port32/tools/geosbuild.sh` (sets the environment, runs pmake under `setarch -R`). Open Watcom's `wcc` leaks heap addresses into `.obj` files, so with ASLR two clean builds differ. Compare builds with `Port32/tools/geocmp.py` (`snapshot` / `compare`), which masks the link timestamps (file header create date/time, `GH_geodeSerial`, the serial in the `.sym`/`.ldf` info field). This is the byte-identical check for P2.
- Define how upstream is pulled in: how often, merge vs. rebase, who resolves conflicts with the PM branch.
- Decide the open questions of the GHAL concept (section 9) ourselves: relation to GPMI/HostIf/HostFS, build variants (#960), tests (#1239), `swat`.
- Conventions: C standard (C11), formatting, prefixes (`ghal_`), Apache 2.0 license header, English everywhere in the repo.
- **Build system: CMake (decided 2026-11-10).** Everything new (GHAL, C port, tests, tools) is built with CMake + CTest, out-of-tree build directories, architecture/host selected via toolchain files in `cmake/` and the cache variables `GEOS_HOST`, `GEOS_LINUX_DISPLAY`, `GEOS_ERROR_CHECK`. The existing 16-bit / PM builds (pmake, esp, glue, Open Watcom) keep running unchanged for now; bringing them under CMake as well is a separate work item in P3. **Status:** the root `CMakeLists.txt` exists; the Math pilot builds and tests through it (x86-64 and i686).

**Result:** fork and branch in place; pulling in upstream is defined; the open questions of the GHAL concept are answered.

### P1: Host-side tests (related upstream: #1239)

- Define location, invocation and reporting for host-side tests (CTest).
- Minimal `linux` GHAL core **without graphics**: `ghal_mem`, `ghal_time`, `ghal_log`, stub services.
- Bring in the **Math pilot** (`Library/Math/Float/*.c`, test `Port32/Tests/test_ffa.c`) as the first test case; switch `fpstack.c` to `double` for that. **Done 2026-11-11:** the float stack holds `double` (`FpValue`); GEOS-80 `FloatNum` is converted arithmetically (no byte copy of `long double`), NaN/infinity are tested by bit pattern (Open Watcom gets `x != x` wrong for NaN), and only C89 `<math.h>` is used. `math.floatFloatToAscii` passes 41/41 on x86-64, i686, ASan/UBSan, arm64 (cross build `cmake/toolchain-linux-aarch64.cmake`, run under qemu-user) and dos32 (DOSBox-X and MS-DOS 7.1 in QEMU). Precision is now that of `double` (15-17 digits); the expected strings were unaffected.
- **Reference from the original (done 2026-11-11):** the expected strings come from the original GEOS math library, not from the documentation. `Port32/Tests/GeosRef/ffaref.goc` runs the same vectors under GEOS32 on MS-DOS 7.1 in QEMU (`Port32/tools/geosrun.sh`, a few seconds per run), and ctest `math.floatFloatToAscii.geosref` compares them; all 40 strings match. Getting GEOS32 (Loader32 + HDPMI16) to run on real DOS outside DOSBox took fixes in the kernel (FSD probe, relocations through a data alias, `ObjInstantiate` with a null DS), in TrueType (EC), `vga16`, Math and the coprocessor libraries. These are intended code changes in the PM geodes, so those geodes are no longer byte-identical to `pm-baseline.json`. One upstream bug was independent of protected mode: the C entry `FLOATFLOATTOASCII` was not redirected to the coprocessor library (see `GeosRef/README.md`).
- CI on linux-amd64, i386, arm64 and armhf (ARM via QEMU user mode).

**Result:** test infrastructure in the fork; the pilot runs on four architectures.

### P2: Pull the GHAL into the existing assembler (step S0)

Work is based on the PM branch.

- **Inventory by script** (repeatable, in CI): all `int 21h/10h/15h/16h/2Fh/67h`, port I/O, `SysCatch*Interrupt`, `SysLockBIOS`, `INT_OFF`/`cli` in kernel and Loader/Loader32. **Done 2026-11-10:** `Port32/tools/ghal_inventory.py`, report `Port32/GHAL-Inventory.md`, baseline `Port32/tools/ghal_baseline.json`; CMake targets `ghal-inventory` and `ghal-inventory-check` (fails if upstream brings new host contacts). Result on `ghal`: 3,531 sites in total; about 1,200 in the GHAL core area (kernel + Loader + Loader32), a good 900 of them excluding `INT_OFF`/`INT_ON`, and more than 500 of those are in `Loader`/`Loader32` and the GPMI use in the kernel heap.
- GHAL entry points and macros in asm (`GHAL_CRIT_ENTER`, `GHALDosCall`, `GHALCatchIrq` …). **real16** and **pm16** map them to the previous behaviour or to GPMI respectively. Starting point: `INT_OFF`/`INT_ON` are already macros (`Include/Internal/interrup.def`, `equ <cli>` / `<sti>`); the kernel also has an old, barely used `NO_DOS` switch (`DOS`/`NODOS` macros in `kernelMacro.def`, Geoworks 1993).
- Route the kernel (`Sys`, `Timer`, `FSD`, `File`, `Boot`, `Thread`) and `Loader32` through them.
- **Acceptance:** the real16 variant builds **byte-identical** to before, or with documented differences. pm16 boots to Desk as before.
- Deliver a build-variant concept for #960 (V20/186/286 optimizations) on the same mechanisms.

**Result:** all host contacts of kernel and loader are in one place and have names. Commits in small packages per kernel module, so pulling in upstream stays manageable.

### P3: C toolchain next to Open Watcom

- `goc` backend for GCC/Clang (next to `COM_WATCOM` etc. in `Tools/goc`).
  **Started 2026-11-11:** `goc -cg` (`COM_GCC`) emits object resources as tables with compile-time chunk handles, unrelocated optrs in resource data and resource numbers from the `.gp` file (`Port32/tools/gpres.py`). `ClassStruct` points to its method tables (`Port32/ABI.md` §4). The 16-bit backends are byte-identical. ctest `goc.resources` checks the tables on linux amd64/i686/arm64 and dos32; the target `goc_sdk` compiles 44 of 55 SDK_C samples (`cmake/GeosGoc.cmake`). Messages are sent through parameter structures (C99 compound literals) and per-method thunks (ABI.md §5), so dos32 moves to DJGPP gcc for the port. **Done 2026-11-11:** dos32 builds with DJGPP gcc 16 from crossdev (`cmake/toolchain-dos32-djgpp.cmake`; Gentoo packages `dev-embedded/djcrx` (DJGPP CVS libc) and `dev-embedded/cwsdpmi` (Gentoo overlay), plus a two-hunk gcc patch for `float.h`/`.bss`; the libc gets `struct timespec` for the libstdc++ of gcc 16, so C++ works too: iostream, exceptions and chrono tested on DOS in QEMU). The GHAL dos32 backend serves both compilers (DJGPP: `_go32` IRQ wrapper with the BIOS handler called through a simulated interrupt, CPU exceptions through libc signals, memory locked via `_crt0_startup_flags`). Every executable carries CWSDPMI (`Port32/tools/djgpp-bind.sh`). All 6 tests pass on real DOS in QEMU, `goc.resources` included. **Done 2026-11-11:** optrs in static data in code (GenControl feature lists, usability tables) are unrelocated constants resolved where used (`GOCR_RESOLVE`, `GeodeResolveOptrAt`; ABI.md §6); goc recognises static initializers by following the C structure of its output. **Done 2026-11-11:** objects of other files of the geode (`@extern`) in resource and static data: `goc -cg -x` exports chunk constants as macros, collected per geode in `<geode>_objs.h` (ABI.md §6.1). **Done 2026-11-11:** `ghal_module` loads geodes in the module format of the host: DXE3 (DJGPP's own, `dxe3gen`) on dos32, ELF shared objects on linux (amd64, i386, arm64, armhf) and later gonware (GHAL-Concept.md §4.4, `cmake/GeosGeode.cmake`); ctest `ghal.module` on all hosts, armhf build added (`cmake/toolchain-linux-armhf.cmake`). **Done 2026-11-11:** the `.gp` data is part of the module (`__geos_module`, `CInclude/geosmod.h`, written by `Port32/tools/gpgeode.py`; ABI.md §7); the 45 `goc_sdk` samples build as geodes on every host (ELF, DXE3 on dos32), and ctest `geode.hello` loads Hello and checks its data against `hello.gp`. Geodes use GEOS's C library headers (`CInclude/Ansi`).
- `uic` → C data instead of `.rdef`.
- `geosport.h`: types, `_far` / `_pascal` empty, 64-bit clean `CInclude/`. First as a check: CInclude compiles with `-m64 -Wall`.
  **Started 2026-11-11:** decisions in `Port32/ABI.md`: no binary compatibility with 16-bit GEOS, 32-bit handles and 64-bit optr on every host, dos32 stays **data-compatible** with DOS16 files. `CInclude/geosport.h` behind `GEOS_PORT32` in `geos.h` (16-bit build unchanged per `geocmp.py`). ctest `cinclude.geosport` checks the type widths on linux amd64/i686/arm64 and dos32; the target `cinclude_port32` compiles 81 of 114 `CInclude/*.h` warning-free on their own.
- **Geode format:** ELF shared object plus a load format derived from `.gp` (exports, protocol numbers). Specification + prototype loader as a host-side test.
- Build system: CMake (see P0). The asm variants use pmake for now; work item: move them under CMake as well (esp/glue/goc/uic as custom commands or a CMake language of their own), so that there is **one** build system in the end.

**Result:** a GOC sample app (`Appl/SDK_C/*`) compiles for `linux` (without running); geodes in the host's module format (ELF, DXE3 on dos32) load in the test.

### P4: Runtime kernel in C

Ordered by dependency, each with host-side tests under `linux`:

1. Heap (32-bit handles, blocks do not move), LMem (chunks still move within a block), Initfile, Local
   **Started 2026-11-11:** handle table and memory block API (`Library/Kernel/Heap/heapHandle.c`, `heapHigh.c`, `Sys/sysError.c`; ABI.md §8), ctest `kernel.heap` on all hosts. Threads, scheduler, semaphores and thread locks on the new `ghal_ctx` (`Thread/threadThread.c`, `threadSem.c`; ABI.md §9), ctest `kernel.thread` on all hosts. LMem heaps (`LMem/lmemCode.c`, resource blocks from `GocResource`; ABI.md §10), ctest `kernel.lmem`. Chunk, element and name arrays (ABI.md §10), ctest `kernel.carray`; `goc -cg` counts `@chunkArray` elements. Timer (`Timer/timerList.c`, `timerInt.c`; ABI.md §11): timers, `TimerSleep`, the 60 Hz interrupt, time slices and CPU usage aging with soft preemption; ctest `kernel.timer`. Events, queues and messages (`Geodes/geodesEvent.c`, `Object/objectClass.c`; ABI.md §12): routing like `ObjMessage`, remote calls, message flags, master parts and variant superclasses; `goc -cg` packs object base structures; ctest `kernel.msg` with goc-made classes. The object system (`Object/objectMeta.c`, `objectProcess.c`, `objectUtils.c`, `objectVarData.c`, `objectReloc.c`; ABI.md §13): `MetaClass` and `ProcessClass` in C, `ObjInstantiate`, variable data, relocation, freeing objects and blocks; master parts and variable data aligned to 8 (`goc -cg` too); ctest `kernel.obj`. Composites (`objectComposite.c`, `objectLinkable.c`): add, remove, move, find, process children, `CObjSendToChildren`, `ObjLinkFindParent`. GCN lists (`LMem/lmemGCNList.c`): system lists, lists in blocks, status events, relocation, `MSG_META_GCN_LIST_*`. Detach and quit (`objectShutdown.c`): `ObjInitDetach`/`IncDetach`/`EnableDetach`, acks, quit over the active list, process detach. The geode loader (`Geodes/geodesLoad.c`, `geodesLibrary.c`, `geodesAccess.c`; ABI.md §7.1): `GeodeLoad` on `ghal_module`, imports by name from the kernel's export table (`Port32/tools/kexports.py`) and the libraries, protocol checks, core block, resources, library entries, the process thread with `MSG_META_ATTACH`, freeing on the last thread's exit; ctest `kernel.geode` with a library and a process geode. optrs in chunks that are not objects: `goc -cg` writes a shadow of such a chunk, and the loader relocates where the two differ (ABI.md §4). Duplicating resources (`Object/objectDup.c`): `ObjDuplicateResource` from the module data, references within the template to the copy (`OLMBH_resource`), the saved-block list, `ObjSaveBlock`, `ObjFreeDuplicate`. `ghal_fs` (POSIX, DOS via the C library), ctest `ghal.fs` on all hosts. The file API on it (`File/filePath.c`, `fileOpenClose.c`, `fileIO.c`, `fileFile.c`; ABI.md §13a): standard paths with the DOS16 layout, current paths per thread, open/create/read/write/seek/truncate/close, duplicated handles, date and time, attributes, rename, directories, `ThreadGetError`; ctest `kernel.file`. VM files (`VMem/vmemBlkManip.c`, `vmemHeader.c`, `vmemHigh.c`, `vmemChain.c`; ABI.md §13b) in the DOS16 format on every host, checked by `Port32/tools/vmcheck.py` (sound for all 823 `.sym` files of the tools) and against a tools-written file; ctests `kernel.vm`, `kernel.vm.format`. GEOS files (`Driver/IFS/HostFS`, `File/fileEA.c`; ABI.md §13c): the DOS16 `GeosFileHeader` in the file on every host, extended attributes, VM files of type `GFT_VM`, DOS 8.3 names with `@DIRNAME.000` on dos32 and long names elsewhere, either layout readable on every host, `Port32/tools/geostree.py` to convert trees; ctests `kernel.file.geos`, `tools.geostree`. LMem blocks in VM files in the DOS16 LMem layout, converted on read and write (ABI.md §13b), `VMSetReloc`, `VMSetExecThread`, `VMAllocLMem`; huge arrays in the DOS16 structure (`VMem/vmemHugeArray.c`, ABI.md §13b), chains of them copied and compared; object blocks in VM files unrelocated completely, classes as exported entries (ABI.md §13d); `vmcheck.py` checks LMem blocks too; ctests `kernel.vm`, `kernel.geode`. The state file (`Object/objectFile.c`; ABI.md §13e): `ObjAssocVMFile`, `ObjDisassocVMFile`, `ObjCloseVMFile`, extra block, protocol check, `CompactObjBlock` and `MergeObjBlock`; restored at once instead of on the first lock; kernel export numbers from `geos.gp`; ctest `kernel.geode` saves and restores `kgapp`. The FSD layer (`FSD/fsdDrive.c`, `Drive/driveHigh.c`, `Disk/diskHigh.c`; ABI.md §13f): file system drivers as tables of C functions after `DR_FS_*` (`CInclude/Internal/fsDriver.h`), drives, disks, paths with drive names; the file API resolves a path to a disk and calls its driver; HostFS (`Driver/IFS/HostFS/hostfsDriver.c`) is that driver on `ghal_fs` for the top directory (drive `C`); `ghal_fs_space`; ctest `kernel.file` checks drives and disks. `FileEnum` (`File/fileEnum.c`; ABI.md §13g) over `DR_FS_FILE_ENUM`: standard return types, match attributes, search flags, wildcard and caller callbacks, skip and real skip counts, the standard subdirectories of a standard path; ctest `kernel.fileenum` with long and DOS names. `FileCopy`/`FileMove` (ABI.md §13a) through the drivers, between disks too; ctest `kernel.file` with a second HostFS drive. The init file (`Initfile/initfileLow.c`, `initfileConstruct.c`, `initfileHigh.c`; ABI.md §13h): geos.ini and its `[paths] ini` chain, read on first use, all reading, writing and deleting routines, sections, data, commit, save/revert, grab/release; ctest `kernel.initfile`. The HostFS name layout from `[system] primaryFSD` (`FSD/fsdInit.c`; ABI.md §13c), as DOS16 chose MSLF. Standard paths spread over several directories by `[paths]` (`File/filePath.c`; ABI.md §13a): the places of a path, `FileLocate`, `FileOpOnPath`, `FileEnum` over all places; ctest `kernel.stdpath` with a second drive. Geode files: `GeodeLoad` reads the file, libraries come from `SP_SYSTEM` (ABI.md §7.1). The boot in C (`Boot/bootInit.c`, the program `Loader/main.c`; ABI.md §13i): the top, the primary FSD with drives from `[hostfs] drives`, `[paths]`, the UI from `[ui] generic`, the process of the command tail, `KernelRun`; ctest `kernel.boot`. Drivers (`Geodes/geodesDriver.c`, `CInclude/geosdriver.h`; ABI.md §7.2): C strategy routines with the registers in a structure, `DriverTable` in `__geos_module`, `DR_INIT`/`DR_EXIT`, system drivers, `GeodeUseDriver`(`PermName`) and `GeodeUseLibrary`(`PermName`) by file name and by permanent name; ctest `kernel.geode` with the driver `kgdrv`. The shutdown (`Sys/sysMisc.c`, `Boot/bootBoot.c`; ABI.md §13i): `SysShutdown` with the confirmation over `GCNSLT_SHUTDOWN_CONTROL`, `EndGeos` (drivers, files, system drivers), exit flags and reason out of `KernelRun`, `GEOS_ACT.IVE` and `SCF_CRASHED`, `SysGetConfig`; ctest `kernel.shutdown`. `ghal_display` (GHAL-Concept.md 4.7): X11 window or full screen, a null surface elsewhere, `Port32/tools/geos-xserver.sh` for a screen of its own (Xvfb, Xvnc); ctest `ghal.display`. `ghal_input` (GHAL-Concept.md 4.7): keyboard and pointer as HID usage events, a queue with the interrupt source `GHAL_IRQ_INPUT`, X11 input; ctest `ghal.input`. The keyboard driver (`Driver/Keyboard`, C beside the asm; ABI.md §7.3): HID events of `ghal_input` become set-1 scan codes to the output (`MSG_IM_KBD_SCAN`), `DR_KBD_XLATE_SCAN` ports the asm translation on the keyboard maps (C from `kmap*.def` by `Port32/tools/kmapgen.py`, all 28 SBCS maps), the `[keyboard]` options; geodes `kbd.geo` and `kbde_gr.geo`; the boot loads `[keyboard] driver` (`LoadKeyboardDriver`); geodes may import `memcpy`/`memmove`/`memset`/`memcmp` from the kernel; ctest `driver.keyboard`. The input manager (`Library/Kernel/IM`, ABI.md §13j): the thread with `IMClass`, the monitor chain, pointer combination (moves, buttons, double press, drags, wheel), `ImConstrainMouse`, the screen limits, the output (`ImGrabInput`), the routines of `geos.gp`; the keyboard driver is its monitor at `ML_DRIVER`; ctests `kernel.im`, `driver.keyboard`. The mouse drivers (`Driver/Mouse`, ABI.md §7.4): `mouseCommon.c` beside `mouseCommon.asm`, the host pointer `Driver/Mouse/Host` (`hostmse.geo`) on `ghal_input`; ctest `driver.mouse`. The video drivers (`Driver/Video`, ABI.md §7.5): `VidCom/*.c` beside `vidcom*.asm` (strategy, exclusive, clipped rectangles, lines, polylines, dashes, polygons, regions, GETBITS/PUTBITS/PUTLINE/MOVBITS, PUTSTRING, the XOR region), the host display `Driver/Video/Host` (`hostvid.geo`) on `ghal_display` with its pointer; `CInclude/Internal/window.h`, `gstate.h`, `videoDr.h`; `WinValClipLine` in `Library/Kernel/Win`; ctest `driver.video`. The graphics system (`Library/Kernel/Graphics`, ABI.md §13k): region arithmetic (`graphicsRegion.c`, `CInclude/Internal/grWinInt.h`); ctest `kernel.region`. Graphics states (`graphicsState.c`, `graphicsGetState.c`, `graphicsColor.c`, `graphicsTables.c`): create, destroy, save and restore, colors with palette mapping, draw masks, line, area and text attributes; ctest `kernel.gstate`. Fixed point math and transformations (`graphicsMath.c`, `graphicsTransformUtils.c`, `graphicsTransform.c`, `graphicsUtils.c`): bit-exact products, quotients, square root, sine tables; translation, scale, rotation, set/apply/save/restore/default matrices, a window's composed matrix, transforming both ways, the pen position; ctest `kernel.transform`. The window system (`Library/Kernel/Win/*.c`, ABI.md §13l): opening and closing, the tree with layers and priorities, regions and validation, washing out and exposures, updates, the drawing mask, invalidation, moving, resizing, priority, information, window transformations; ctest `kernel.win`. Drawing (`Graphics/graphicsOutput.c`, `graphicsLine.c`, ABI.md §13m): `EnterGraphics` with the window, rectangles, points, frames, lines through the video driver; filled polygons (scan converted to a region), polylines, fat lines with ends and joins, dashes (`graphicsPolygon.c`, `graphicsPolyline.c`, `graphicsFatLine.c`, `graphicsLineStyles.c`); ellipses with the conic scan converter, arcs and rounded rectangles (`graphicsEllipse.c`, `graphicsArc.c`, `graphicsArcLow.c`, `graphicsCalcEllipse.c`, `graphicsCalcConic.c`, `graphicsRoundedRect.c`); regions and the window's bounds, mask tests and invalidation (`graphicsRegion.c`, `graphicsWin.c`); bitmaps unscaled, scaled and rotated, in slices, PackBits, palettes, converted for devices of fewer colors, LZG, huge bitmaps, images (`GrDrawImage`), bitmaps to draw into (`graphicsBitmapCreate.c`, the memory video driver `Driver/Video/Dumb/VidMem`), and `GrBitBlt` (`graphicsRaster.c`); `WinScroll` (`winWindows.c`); ctest `kernel.draw` on the host display. Fonts and text (`graphicsInit.c`, `graphicsChars.c`, `graphicsFont.c`, `graphicsTextMetrics.c`, `graphicsText.c`, `CInclude/Internal/fontDr.h`, `Driver/Video/VidCom/vidcomChars.c`; ABI.md §13n): the DOS16 font files read and converted at the boot, the default font, fonts in use per GState with substitution, widths, metrics, kerning, `GrDrawText` with underline and strike-through through `DR_VID_PUTSTRING`; ctest `kernel.draw` with Berkeley. Font drivers (`graphicsFontDriver.c`, `Driver/Font/TrueType/Main/mainManager.c`; ABI.md §13o): `[system] font` loaded at the boot, `DR_FONT_*` with `DriverRegs`, characters built when drawn; the TrueType driver `truetype.geo` with FreeType 1 and the adapter on 32-bit longs, the render pool aligned for 64-bit hosts, `-funsigned-char`; EC checks, `CFatalError`, Bezier curves in the kernel; ctest `kernel.draw` with Cooper Black. Graphics strings (`graphicsString.c`, `graphicsStringStore.c`, `graphicsStringMisc.c`; ABI.md §13p): the DOS16 elements stored by every `Gr*` routine with one, in chunks, huge arrays, files with the metafile header and (read only) memory, `GrLoadGStringPtr` for `GST_PTR`; playing, partial playing, parsing, copying, element access; ctest `kernel.draw`. Paths (`graphicsPath.c`; ABI.md §13q): defined through a `GST_PATH` graphics string, kept in the GState, combined by replace, union and intersection, filled as regions by the fill rule, stroked, bounds; clip rectangles and clip paths in the window mask; path and clip regions, point tests, `GrGetPath`; rounded rectangles, 3-point arcs and text (outlines from the font driver) in paths; ctest `kernel.draw`. Patterns (`graphicsPattern.c`; ABI.md §13r): system and custom hatches, tiled bitmaps, for figures, text and paths, through a clip path; ctest `kernel.draw`. Masked windows (`WCF_MASKED`, `WCF_DRAW_MASK`): the 50% checker in the C VidCom, the mask washed black; ctest `kernel.draw`. Bounds of graphics strings (`graphicsStringBounds.c`; ABI.md §13p): the elements' extent in page coordinates through the path walker, back through the starting transformation; ctest `kernel.draw`. Save under areas (`Driver/Video/VidCom/vidcomUnder.c`, `Library/Kernel/Win/winSaveUnder.c`; ABI.md §7.5, §13l): pixels saved and restored by the C VidCom, collision checks, given up as the asm does, the restored part not exposed; ctests `driver.video`, `kernel.draw`. Bitmaps in paths as the rectangles they cover (`graphicsRaster.c`); ctest `kernel.draw`. Geode private data (`Geodes/geodesPriv.c`; ABI.md §7.1); ctest `kernel.geode`. The window under the pointer (`Win/winNotification.c`, `winGeode.c`, `IM/imPtr.c`; ABI.md §13j, §13l): `WinMovePtr` from the input manager, `MSG_META_WIN_CHANGE` and `WinChangeAck`, raw universe enters and leaves, implied window changes, pointer images per level (`ImSetPtrImage`, `WinSetPtrImage`, the geode's), `ImSetPtrWin`, the window variables of geodes; ctests `kernel.win`, `kernel.geode`, `kernel.obj`. Text fields (`GrDrawTextField` in `graphicsText.c`, a C style callback; `GR_DRAW_TEXT_FIELD` recorded, read, played, bounded and in paths; ABI.md §13n, §13p); ctest `kernel.draw`. The input queue flush (`MSG_META_OBJ_FLUSH_INPUT_QUEUE`, `ObjStartQueueFlush` in `Object/objectMeta.c`; ABI.md §13): freeing objects, blocks and windows waits for it; `WinClose` and `WinSetInfo` hold the window until `MSG_META_WIN_DEC_REF_COUNT`; ctests `kernel.win`, `kernel.obj`. `WinForEach`, `WinRealizePalette` (ABI.md §13l); the palette routines (`GrCreatePalette`, `GrSetPalette`, `GrSetPaletteEntry`, `GrGetPalette`, `GrDestroyPalette` in `graphicsColor.c`; ABI.md §13k), in graphics strings too; ctests `kernel.win`, `kernel.draw`. The `*_PTR` and `*_OPTR` graphics string elements, resolved when played (ABI.md §13p); ctest `kernel.draw`. `ImSetPtrFlags`/`ImGetPtrFlags` (`IM/imPtr.c`; ABI.md §13j); ctest `kernel.win`. The screen saver, `SysEnableAPO`/`SysDisableAPO` (`IM/imScreenSaver.c`; ABI.md §13j); `ImBumpMouse`, the move/resize xor (`ImStartMoveResize`, `ImStopMoveResize`); ctest `kernel.im`. Paths defined while writing a graphics string go into it (`GrEndPath`, ABI.md §13q); ctest `kernel.draw`. Bitmap slices in graphics strings (`GSE_BITMAP_SLICE`, ABI.md §13p); ctest `kernel.draw`. The memory video driver's palette functions on the bitmap's palette (ABI.md §7.5); ctest `kernel.draw`. Localization (`Library/Kernel/Local`, ABI.md §13s): character classes, case, lengths, dictionary order, DOS code pages, quotes, number and currency formats, measurement system, numbers and distances as text, dates and times (formatting, parsing, time zone, normalizing), `LocalInit` with `[localization]` of geos.ini; the tables and strings made of the asm by `Port32/tools/localgen.py`; ctest `kernel.local`. The database manager (`Library/Kernel/DBase`, ABI.md §13t): groups, items, item blocks and the ungrouped groups in the DOS16 format of the file, DB items in VM chains, `vmcheck.py` checks databases (all 2217 files of the repository sound); ctests `kernel.db`, `kernel.db.format`, `kernel.db.format16` with DOS16 files. Date and time, set on the host through the new GAPI layer (`Port32/GAPI-Concept.md`, `Gapi/`; on Linux systemd-timedated over GEOS's own D-Bus client, logs to the journal in the `session` role; reboot and power off at the end of a shutdown through logind or, on DOS, the keyboard controller and APM; the host's printers and print jobs through our own IPP client to CUPS, and the CUPS port driver `Driver/Stream/Cups` for the spooler; ctests `gapi.time`, `gapi.dbus`, `gapi.print`, `driver.cups`; the export side of the PostScript translation library `eps.geo` in C beside the asm, for the PostScript printer driver, without text and bitmaps yet, ABI.md §13v, ctest `library.eps` with Ghostscript where installed) or kept as an offset, real-time timers (`Timer/timerMisc.c`; ABI.md §11); shared and exclusive memory locks, thread-private words; `SysGetInfo`, `SysStatistics`, `SysNotify`, exit flags, `UtilHex32ToAscii`/`UtilAsciiToHex32`, geode references (ABI.md §8, §9, §13u); ctests `kernel.timer`, `kernel.thread`, `kernel.sys`, `kernel.geode`. Next: the rest of the graphics system and the window system in the kernel, the boot's other drivers (`[system] memory`/`power`), links (`FileCreateLink`).
2. Thread/Sem/Timer on `ghal_ctx` / `ghal_time` (GEOS scheduler + kernel lock)
3. Object (message dispatch with C signatures, 32-bit message numbers), Geodes (`GeodeLoad` on `ghal_module`: DXE3 on dos32, ELF on linux/gonware)
4. VMem (new file format), File API, FSD + HostFS driver on `ghal_fs`

**Result:** a GEOS kernel without graphics runs under `linux` as a process, loads geodes, sends messages, reads and writes VM files. Everything is covered by tests.

### P5: Host `dos32`

- GHAL core `dos32` (HDPMI32/HX): memory, scheduler with context switch, IRQ stubs, DOS lock. Reuse the structure of GPMI/Loader32.
- Drivers to C/32-bit:
  - video: VESA LFB from `VGAlike` / `VidCom`
  - keyboard, mouse (PS/2, serial)
  - IFS/DOS with LFN
  - HostFS for Basebox
- Startup, `geos.ini`, system directory.

**Result:** the kernel from P4 runs under DOS (FreeDOS + HDPMI32, QEMU and real hardware) with a test display. **From here on DOS is the reference platform in the C branch as well.**

**Milestone "presentable":** from here on the upstream issue can be created (see way of working), at the latest after P6 with the first C desktop.

### P6: Graphics, windows, UI, text

- Kernel `Graphics` / `Win` / `IM` (~130 k lines of asm) → C, plus the generic FB video driver on `ghal_display` and the software renderer.
- `Library/User` (~169 k) → C.
- `SpecUI/CommonUI` + Motif (~295 k) → C. Other SpecUIs later.
- `Library/Text` (~149 k) → C.
- Hook up the FreeType driver (already C).

**Result:** **first GEOS desktop in C, under `dos32` and `linux` (SDL3).** `Appl/SDK_C` samples run.

### P7: Host `linux` complete

- Display: SDL3 → fbdev → X11.
- Input → HID usages, HostFS with xattrs, sockets (host socket driver modelled on `HstTCPIP`), audio, printing via CUPS.

**Result:** GEOS as a normal Linux program on amd64, i386, arm64 and armhf.

### P8: Components and applications

- Components: GrObj, Spreadsheet, Chart, Math (rest), Parse, Spline, Impex.
- Rebuild the GOC apps (~0.83 M lines, including Breadbox).
- Translate or replace the asm apps: FileMgr, GeoWrite, Calendar …

### P9: Host `win32` / `win64`

- GHAL core for Windows: Win32 threads, `VirtualAlloc`, Winsock, display via DIB or SDL3, HostFS with NTFS streams.
- Mind LLP64 (GHAL concept, build rules).

### P10: Host `gonware`

- See `GonDesk-GEOS-on-GonWare.md`. Requires Ring 3 in GonWare (its phase 6).

---

## 3. Order of magnitude

| Phase | Scope |
|---|---|
| P0–P3 | Infrastructure and toolchain. Months, not years. Most of it is already useful for the 16-bit code. |
| P4–P6 | ~0.75 M lines of asm up to the desktop. Several person-years. |
| P7–P10 | Hosts and apps. Hosts are small once the GHAL stands. The asm apps are the big remainder (10+ person-years in total for the full build-out). |

The pilot gave a ratio of about 1.9 : 1 for asm code to C. Most of the time goes into clarifying semantics, not typing.

---

## 4. Risks

| Risk | Mitigation |
|---|---|
| **Drifting apart from upstream** (the PM branch moves fast) | work on the PM branch, pull in upstream regularly, small commits, keep changes to upstream files minimal (new code in separate directories) |
| Upstream changes things we build on (e.g. GPMI, Loader32) | inventory script (P2) in the fork's CI, so new host contacts from upstream show up immediately |
| Toolchain (old OW, 64-bit Linux bugs) | C port with GCC/Clang in parallel; the OW chain stays untouched apart from upstream fixes |
| `swat` vs. gdb | keep `swat` for the DOS variants; gdb for hosted systems; align symbol formats later |
| Size (3.7 M lines of asm) | order by value (kernel → UI → text → GOC apps); asm apps last or replaced |
| Semantic errors while translating | every ported part with host-side tests, compared against the 16-bit original where it makes sense (as in the Math pilot) |
| License | everything Apache 2.0; no GPL parts in GEOS code (relevant e.g. for HX parts and helper libraries) |

---

## 5. Next concrete steps

1. ~~Confirm that two clean builds via `geosbuild.sh` are identical under `geocmp.py`.~~ Done 2026-11-10: two clean builds, 2356 files, 0 changed. The manifest is kept as `Port32/reference/pm-baseline.json`; every P2 step must pass `geocmp.py compare --code Port32/reference/pm-baseline.json Installed` (strict mode without `--code` when no source lines move) (refresh it only on intended output changes).
2. Review the PM branch (GPMI, `Loader32`, kernel changes, HostFS) and record the result here.
3. Start S0 with `INT_OFF`/`INT_ON`: new `Include/Internal/ghal.def`, route kernel `Timer` and `Sys` through it, verify byte-identical output. **Started 2026-11-10:** `Include/Internal/ghal.def` (dos backend) has `GHAL_IntOff`/`GHAL_IntOn` (now behind `INT_OFF`/`INT_ON`), `GHAL_PicEoi`, `GHAL_TimerRead`, `GHAL_TimerWrite` and `GHAL_TimerReset`; all 8253/8259 accesses of the PC kernel `Timer` go through them. A clean build has identical `.geo`/`.exe`/`.com`/`.vm` (`geocmp.py compare --code`); only `.sym`/`.ldf` of geodes that include `interrup.def` differ (debug info: new include file, moved source lines). The inventory does not count `ghal.def`. Second step (same day): `GHAL_DosCall` replaces every `int 21h` in the kernel (51), `Loader` (57) and `Loader32` (47); `Loader`/`Loader32` include `ghal.def` from `main.asm`. Code again identical. The remaining kernel `dos_int21` hits are calls of `FileInt21`/`SysCallDOS`, which issue `GHAL_DosCall` themselves. Note: pmake does not track the includes of `Loader/*/loader.asm`; delete the objects in `Installed/Loader*/*` before checking a loader change. Third step (same day): one macro per host service (`GHAL_VideoBios`, `GHAL_DiskBios`, `GHAL_SysBios`, `GHAL_KbdBios`, `GHAL_BootstrapBios`, `GHAL_ClockBios`, `GHAL_DosAbsRead`/`Write`, `GHAL_MultiplexCall`, `GHAL_EmsCall`), applied by `Port32/tools/ghal_route_int.py` (code lines only) to 85 sites in kernel and loaders. Code identical again; the loader video files are all part of the built VGA loader, so every changed file is covered. The kernel and loaders now have no direct BIOS, EMS, multiplex or other DOS interrupts left. Open: port I/O (`Loader` 94, mostly video detection; `Sys` 38; kernel 17; `Thread` 6), RTC accesses in `timerMisc.asm` (GULLIVER only), `SysLockBIOS`, IRQ vectors, and GPMI/DPMI in `Loader32` and the kernel heap. These need GHAL interfaces with real semantics (memory, selectors, IRQs, video probe) rather than a one-to-one macro. **Fixed 2026-11-10 in GHAL-Concept.md 4.6:** GPMI is the pm16 backend of `ghal_mem`/`ghal_irq`/`ghal_fault` (mapping table per GPMI call); port I/O of the handheld/XIP variants is not ported; the active PC accesses (PIC in-service register, NMI status) now go through `GHAL_PicReadIsr` and `GHAL_NmiStatusAT/XT`, code identical. **Done 2026-11-10:** `CInclude/ghal.h` (API 1: `ghal_mem`, `ghal_time`, `ghal_crit`, `ghal_hostlock`, `ghal_irq`, `ghal_fault`, `ghal_log`, `ghal_exit`, `ghal_caps`) and the POSIX backend `Ghal/Posix/ghal_posix.c` (everything except `ghal_irq`/`ghal_fault`, which return `GHAL_ERR_UNSUPPORTED`); test `ghal.core` passes on x86-64, i686 and under ASan/UBSan. **Done 2026-11-10:** POSIX `ghal_irq` (`GHAL_IRQ_TICK` from a host thread with absolute deadlines; handlers run inside `ghal_crit`) and `ghal_fault` (signals on an alternate stack; kind, address and pc on x86-64/i386/arm64/armhf); tests stable in 40 repeated runs and produce no core dumps. **Done 2026-11-10:** dos32 backend `Ghal/Dos32/ghal_dos32.c` (Open Watcom 386, DOS/32A, toolchain `cmake/toolchain-dos32-watcom.cmake`): `ghal_mem` on the extender's C heap (protection accepted, not enforced: DPMI 0.9), `ghal_time` from BIOS ticks plus the 8253 counter in mode 2 (sub-tick resolution), `ghal_crit` as nested `cli`/`sti`, `GHAL_IRQ_TICK` on IRQ0 with the PIT reprogrammed and the BIOS chained at 18.2 Hz. The tests are split: `test_ghal_core.c` runs on every host (ctest `ghal.core`; under dos32 headless in DOSBox-X via `Port32/tools/dosrun.sh`, 20 of 20 runs pass), `test_ghal_posix.c` keeps the POSIX-only parts. Since then also on real MS-DOS 7.1 in QEMU (`dosrun.sh` engines `qemu` and `qemu-hd`; the base image with HIMEM, EMM386 and mouse driver is built unattended by `Port32/tools/mkdosbase.sh` into `Local/`): 15 of 15 runs pass with KVM and with TCG. **2026-11-11:** dos32 `ghal_fault` through DPMI exception handlers (0203h; the stub sends the faulting context on to a trampoline, so the C handler may `longjmp`; no fault address under DPMI 0.9); test `ghal.dos32` (divide, invalid selector, invalid opcode, `int 3`) passes in DOSBox-X, QEMU with bare DOS and QEMU with the full DOS image, 10 of 10 runs each. **2026-11-11:** the float pilot runs on `double` (see P1); it builds and passes under dos32 as well. Open for dos32: a run on real hardware.
4. Define how upstream is pulled in (P0).
5. Align the Math pilot with #1239 (location, invocation, `double`).
6. GEOS32 on real DOS: **2026-11-11** `vga16` sets VESA modes without the host interface again (desktop at 1024×768 in QEMU, reference run in DOSBox-X too). Open: a run on real hardware, and a check of the kernel fixes in pcgeos-basebox (host interface). Candidates for the showcase issue upstream (draft first, confirm before posting).

---

## Appendix: Reviewed issues/PRs related to the port

**Open:**

- #665 (protected mode)
- #1210 (Loader32 diagnostics)
- #672, #960 (16-bit variant)
- #1239 (host-side tests)
- #983 (newer OW)
- #117, #55, #85, #16 (building on Linux)
- #72 (ia16-gcc)
- #149 / PR #176, #1050, PR #1246 (host TCP / HostIf)
- #1209 (XMS swap)
- #456 (CGA)
- #28, #162, #393, #168, #169, #177, #453, #1089 (glue/esp/Watcom)
- #224 (OW debug info)
- #421, #573, #306, #1074 (swat)

**Closed, as background:**

- #829 (Basebox interface)
- #89 / #90 (swat on Windows/Linux)
- #91 / #104 / #126 (swat via TCP/IPX/serial)
- PR #51 (toolchain on Linux)
- PR #686 / #688 (HostIf library)
- PR #1226 (handle leak, PM branch)
- PR #1140 (DOSBox detection in the kernel)
