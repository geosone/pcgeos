# PC/GEOS
This repository is the official place to hold all the source codes around the PC/GEOS graphical user
interface and its sophisticated applications. It is the source to build SDK and release version of PC/GEOS.
It is the place to collaborate on further developments.

![Screenshot showing a typical GEOS desktop](TechDocs/Markdown/Art/title-screenshot.png)

The base of this repository is the source code used to build Breadbox Ensemble 4.13 reduced by some modules identified as critical in regard to the license chosen for the repository.

While now the WATCOM is used to compile the C parts, the full SDK is available for Windows and Linux.

# Branch `ghal`: PC/GEOS in C on a hardware abstraction layer

This branch (fork `geosone/pcgeos`, on top of upstream's protected mode
branch) ports PC/GEOS from 16-bit assembler to portable C. It is **work
in progress**: the kernel, the UI library, Motif and the text library run
as C code and are checked by tests, but there is no complete desktop yet.
The 16-bit build described further down is untouched and still works.

## What changes and what does not

The user interface is meant to be exactly the original's: the C code
follows the assembler file by file (a C port lies next to the asm it
replaces) and the screens are compared with the original 16-bit build.
Only what talks to the machine is different, and that sits behind one
interface so other systems can attach there.

From bottom to top:

1. **Host**: Linux (amd64, i386, arm64, armhf) and 32-bit DOS today;
   Windows and GonWare are planned. No emulation: GEOS runs as native
   code of the host.
2. **GHAL**, the GEOS Hardware Abstraction Layer (`Ghal/`,
   `CInclude/ghal.h`): memory, clocks, critical sections, thread
   contexts, interrupts and faults, module loading, the host's files,
   the display as a surface, keyboard and pointer events. Beside it **GAPI**, the GEOS
   Host API (`Gapi/`, `CInclude/gapi.h`), for services a host offers
   (clock, printers, power). A new host implements these two.
3. **Kernel in C** (`Library/Kernel`): where the original calls DOS, the
   BIOS or touches hardware, it calls GHAL (boot, heap, local and
   virtual memory, threads, timers, files and disks, geode loading, the
   init file, input).
4. **Drivers** are real GEOS drivers in `Driver/` that run on GHAL and
   GAPI: the host file system (`IFS/HostFS`), video (`Video/Host`),
   mouse, keyboard, power, printing through CUPS (`Stream/Cups`).
   Parallel, serial and network access belong here as well.
5. **Unchanged in what they do**: the UI library, the specific UI
   (Motif), the text library and the applications. There is no host
   code in them.

The files in `Ghal/` are not GEOS drivers but the host's side of the
interface. A driver in `Driver/` only knows the GHAL calls and has no
host code; which file of `Ghal/` answers them is chosen by `GEOS_HOST`:

| | GEOS driver (the same on every host) | Host side in `Ghal/` (one per system) |
|---|---|---|
| File system | `Driver/IFS/HostFS` calls `ghal_fs_*` | `Posix/ghal_fs_posix.c` (Linux), `Dos32/ghal_fs_dos.c` (DOS) |
| Screen | `Driver/Video/Host` calls `ghal_display_*` | `Display/ghal_display_x11.c` (X11) |
| Mouse, keyboard | `Driver/Mouse/Host`, `Driver/Keyboard` call `ghal_input_*` | `Common/ghal_input.c`, `Common/ghal_hid_linux.c` |

A new host adds its files to `Ghal/` (and `Gapi/`); `Driver/` stays as
it is.

One thing does reach every layer: there are no segments any more, so
handles and pointers are 32 bits wide and the binary interface is new
(`Port32/ABI.md`). Files stay compatible: a GEOS file keeps its 256 byte
header on every host. On DOS a geode is a DJGPP DXE3 module, on Linux an
ELF shared object.

More: `Port32/GHAL-Concept.md` (architecture), `Port32/GAPI-Concept.md`,
`Port32/PLAN.md` (plan and state), `Port32/ABI.md` (what differs from
the 16-bit interfaces, per library).

## Building the C port

The C port is built with CMake; `Port32/` only holds documents, tests
and tools.

Needed: CMake 3.20 or newer, Ninja, a C11 compiler (gcc), Python 3, and
`bin/goc` (the GEOS object C preprocessor of this branch; build the SDK
tools as described below, it needs Open Watcom). For the display: the
X11 development files, and Xvfb for the tests.

    cmake -S . -B build -G Ninja          # Linux, native
    cmake --build build
    ctest --test-dir build                # one after the other, no -j

The display tests bring their own X server (Xvfb) and never open a
window on your desktop. To watch one, run it through
`Port32/tools/geos-xserver.sh -vnc`, or take a picture of the UI test
with `Port32/tools/uiboot-shot.sh build out.png`.

Other targets take a toolchain file (the cross compilers and, for the
tests, qemu user mode with a sysroot must be installed):

| Target | Toolchain file |
|---|---|
| Linux i386 | `cmake/toolchain-linux-i686.cmake` |
| Linux amd64 (from another host) | `cmake/toolchain-linux-x86_64.cmake` |
| Linux arm64 | `cmake/toolchain-linux-aarch64.cmake` |
| Linux armhf | `cmake/toolchain-linux-armhf.cmake` |
| DOS, 32 bit (DJGPP) | `cmake/toolchain-dos32-djgpp.cmake` |
| DOS, 32 bit (Open Watcom, older, fewer tests) | `cmake/toolchain-dos32-watcom.cmake` |

    cmake -S . -B build32 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-linux-i686.cmake
    cmake -S . -B build-dos32 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-dos32-djgpp.cmake

Options: `GEOS_HOST` (`linux`, `dos32`; set by the toolchain file),
`GEOS_LINUX_DISPLAY` (`x11`; `sdl3` and `fbdev` are placeholders),
`GEOS_ERROR_CHECK` (the EC variant, on by default).

The DOS tests run on real DOS in QEMU (`Port32/tools/dosrun.sh`). No DOS
files are part of this repository: make a disk image of your own DOS
with `Port32/tools/mkdosbase.sh` and name it in `DOSRUN_BASE_IMG`.

    DOSRUN_BASE_IMG=$PWD/Local/dosbase.img ctest --test-dir build-dos32

## Comparing with the original

`Port32/tools/uiref-cmp.sh` builds the UI test's objects as a plain
16-bit application with the original tools, runs it in the original
GEOS on DOS, lets the port draw the same, and puts both screens side by
side (original left, port right). It needs the 16-bit build below, the
DOS image and ImageMagick. A difference in the UI is a bug of the port.

# How to build? (the 16-bit build)

## Prerequisites
The SDK requires "sed" (https://en.wikipedia.org/wiki/Sed) and "perl" (https://en.wikipedia.org/wiki/Perl) to be installed. Both are pre-installed in most Linux-distributions. Windows-users should install "sed" by adding the usr/bin of the official git distribution (https://git-scm.com) to the path (or Cygwin), and should use the perl-variant "Strawberry Perl" (http://strawberryperl.com/).

On Linux if you want to use swat for debugging with good system integration is is required to install xdotools package. It ensures swat receives the keyboard focus once needed. 

## Install WATCOM and set environment
- Unzip WATCOM tools from a suitable [release-tar-gz](https://github.com/open-watcom/open-watcom-v2/releases/download/2020-12-01-Build/ow-snapshot.tar.gz) snapshot (currently tested: 2020-12-01) for instance to `C:\WATCOM-V2`
- add WATCOM env variable: `WATCOM=c:\WATCOM-V2`
- set `BASEBOX=basebox` to use the advanced emulator backend from [pcgeos-basebox](https://github.com/bluewaysw/pcgeos-basebox/tags) if it is on the executable path, alternatively you may provide the full path to the executable as well
- set `ROOT_DIR` to the root of the checkout
- set `LOCAL_ROOT` to a local working directory (can be empty at first, but should be under `pcgeos`, so you can use it for development of your own apps as well)
- add `C:\WATCOM-V2\binnt` to your system path variable
- add `bin` of the checkout of this repo to path variable
- add sed and perl to path variable - note the order to avoid loading the wrong Perl version. Example:

        set WATCOM=c:\WATCOM-V2
        set ROOT_DIR=C:\Geos\pcgeos
        set LOCAL_ROOT=c:\Geos\pcgeos\Local
        set BASEBOX=basebox
        PATH %WATCOM%\binnt;%ROOT_DIR%\bin;C:\Geos\pcgeos-basebox\binnt;%PATH%;c:\Program Files\Git\usr\bin

Document is work in progress.... stay tuned!


## Building PC/GEOS SDK
Build pmake tool:

    cd %ROOT_DIR%/Tools/pmake/pmake
    wmake install

Build all the other SDK Tools:

    cd %ROOT_DIR%/Installed/Tools
    pmake install

Build all PC/GEOS (target) components:

    cd %ROOT_DIR%/Installed
    pmake

Build the target environment:

    perl %ROOT_DIR%/Tools/build/product/bbxensem/Scripts/buildbbx.pl

  - the answers to the questions from the above perl-script are:
    - nt (for the platform)
    - y (for the EC version)
      - Alternatively, reply "n" for the NC ("release") version that is faster and corresponds to the actual release
      - The EC ("debug") version has stricter error checking and is better suited for debugging
    - n (as the build for Double-Byte Character Sets is not yet fully functional)
    - y (for the geodes)
    - n (for the VM files)
    - and then you'll have to enter the path to a "gbuild"-folder in your LOCAL_ROOT-folder.
  - BTW: It's expected that the current version of the perl-script creates several "Could not find file _name_ in any of the source trees."-messages.
  - Advanced use: You can use the EC target and the NC target simultaneously.
    - Build the EC target (answers nt/y/n/y/n/path). The path should end with `gbuild.ec`.
    - Build the NC target (answers nt/y/n/y/n/path). The path should end with `gbuild` or `gbuild.nc`.
    - The `target` command (see below) starts the EC target by default. To start the NC target, enter `target -n`.

Launch the target environment in dosbox:
- make sure dosbox is added to your path variable, or [pcgeos-basebox](https://github.com/bluewaysw/pcgeos-basebox/tags) is installed and configured using BASEBOX environmental variable
- `%ROOT_DIR%/bin/target` to launch the EC ("debug") version
  - the "swat" debugger stops immediately after the first stage of the boot process
  - enter `quit` at the "=>" prompt to detach the debugger and launch PC/GEOS stand-alone
    - or: enter `c` to launch with the debugger running in the background (slower)

## Customize target environment
If you want to customize the target environment settings only for yourself, you should not change the file `%ROOT_DIR%/bin/basebox.conf`.
- Create a file called basebox_user.conf in %LOCAL_ROOT% folder.
- Enter the new settings here. These settings overwrite those from basebox.conf. Example:
  - [cpu]
  - cycles=55000


# How to develop?

PC/GEOS comes with extensive technical documentation that describes tools, programming languages and API calls from the perspective of an SDK user. This documentation can be found in the `TechDocs` folder and is available in Markdown format.

You can find a browsable, searchable version of the documentation here: https://bluewaysw.github.io/pcgeos/

##
We are on https://discord.com/ for more efficient collaboration. Please register at https://discord.gg/qtMqgZXhf9 for blueway.Softworks or use an existing discord-account to get access to our developer community. Welcome!
