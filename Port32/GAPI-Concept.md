# GAPI: GEOS Host API

**Status:** concept, with the first function implemented: setting the clock (`gapi_time_set_local`, 2026-11-12). The decisions of 2026-11-12 are in section 5.

**Goal:** on Linux, GEOS can be a desktop of its own, as KDE or GNOME are: the user's session, which manages the machine through the host's services. GHAL is the abstraction of the hardware; GAPI is the way back into the system.

**Scope:** pcgeos only, next to the GHAL (`Port32/GHAL-Concept.md`).

## 1. Why a second layer

GEOS is an operating system of its own. It has drivers for everything and its own system management: the Preferences set the date and time, the printers and their ports, the modem, the network, the keyboard layout, the video mode, the sound and the power. Under DOS, GEOS does all of this itself with DOS and BIOS calls and its own drivers. DOS has nothing else to offer.

On a hosted system (Linux, later Windows and GonWare) these things belong to the host. If GEOS kept its own world there, it would show a clock the host does not have and printers the host does not know. To be detached from DOS and packaged for Linux, GEOS must manage the host's settings through the host's own interfaces.

The GHAL does not cover this:

| | GHAL (`ghal_*`, `CInclude/ghal.h`, `Ghal/`) | GAPI (`gapi_*`, `CInclude/gapi.h`, `Gapi/`) |
|---|---|---|
| Question | What does GEOS need to **run** on the host? | What does GEOS **manage** for its user, as an operating system? |
| Examples | read the clock, ticks, memory, contexts, interrupts, display surface, input events, files | set the clock and time zone, configure printers, switch the machine off, configure the network, set the keyboard layout and language, mount drives |
| Required | every host implements the GHAL core completely | every function is optional; a host may refuse |
| Under DOS | DPMI, PIT, VESA, PS/2 … | GEOS itself: DOS/BIOS calls and its own drivers, as the asm kernel did |
| Under Linux | POSIX, X11, ELF … | the host's services: system clock, systemd (timedated, logind, localed), CUPS, NetworkManager, udisks |

```
┌───────────────────────────────────────────────────────────────────┐
│ Applications, Preferences modules (Appl/Preferences, Library/Config)
├───────────────────────────────────────────────────────────────────┤
│ GEOS kernel and system libraries (Timer, Sys, Spool, Socket, ...) │
├──────────────────────────────┬────────────────────────────────────┤
│ GHAL: run on the host        │ GAPI: manage the host              │
├──────────────────────────────┴────────────────────────────────────┤
│ Host: DOS │ Linux │ Windows │ GonWare                             │
└───────────────────────────────────────────────────────────────────┘
```

## 2. Rules

1. **Only the kernel and system libraries call GAPI.** Applications and Preferences modules keep using the GEOS API they always had (`TimerSetDateAndTime`, `SysShutdown`, the spooler, the init file). The kernel or library then decides whether the host is asked.
2. **Every function may be refused.** `GHAL_ERR_UNSUPPORTED` means the host has no such function. `GHAL_ERR_ACCESS` means the host refuses it, for example because of missing privileges. GEOS then falls back to its own way, as `TimerSetDateAndTime` keeps an offset to the host's clock. A Preferences module may show "managed by the host". Nothing fails silently: the kernel logs a refusal once.
3. **GEOS does not run as root.** On Linux, privileged changes go through the host's privileged services, which ask the user themselves: D-Bus to systemd with polkit, CUPS. A direct system call (`clock_settime`) is only the first step, for root or for a GEOS that is the session.
4. **Same types and status codes as the GHAL.** `gapi.h` includes `ghal.h` and uses `ghal_status` and `ghal_datetime`.
5. **Two roles** (`[system] hostRole`):
   - `guest` (the default, today's case): GEOS runs inside another desktop and leaves the host alone. The clock is kept as an offset, and printing still goes to the host's printers.
   - `session`: GEOS is the user's only UI, as KDE or GNOME. It manages the host through GAPI. Where the user has the right, the host does it. Otherwise the host's service asks for the password (polkit over D-Bus). Its logs go to the journal.

   On DOS, GEOS always is the system.
6. **Tests never change the host.** The kernel uses GAPI only after a boot as a `session`. Unit tests without a boot never move the host's clock, and `gapi.time` sets the clock to the time it already has.

## 3. Groups

| Group | Content | `dos32` | `linux` | GEOS side |
|---|---|---|---|---|
| `gapi_time` | set the date and time; later the time zone and whether NTP runs | int 21h/2Bh, 2Dh (as `RestoreMSDOSTime`) | `clock_settime`; then `org.freedesktop.timedate1` (polkit) through our D-Bus client | `TimerSetDateAndTime`, Preferences Date & Time. **Implemented:** `gapi_time_set_local` |
| `gapi_power` | switch off, reboot, suspend the machine | APM, BIOS reset (power drivers) | `org.freedesktop.login1` `PowerOff`/`Reboot`/`Suspend` | `SysShutdown` (`SST_POWER_OFF`, `EF_RESET`), the power driver |
| `gapi_print` | list printers and queues, default printer, add or change one, its options; submit a job | GEOS printer drivers to LPT/COM/file, as today | CUPS (IPP): queues and options; jobs as PostScript or PDF | Spool library, printer control panel. GEOS renders PostScript/PDF, CUPS prints |
| `gapi_locale` | keyboard layout, language, country formats, time zone | `COUNTRY`, code page, `geos.ini` | `org.freedesktop.locale1`, `timedate1` | `Local*`, keyboard driver, Preferences International |
| `gapi_net` | interfaces, addresses, DNS, dial-up, Wi-Fi | GEOS's own TCP/IP and its configuration | NetworkManager over D-Bus (read first, change later) | Socket library, Preferences Network/Modem |
| `gapi_storage` | drives and media, mount, eject, format | kernel `Disk`, `Format`, `Diskcopy` | udisks2 | Disk API, the file manager |
| `gapi_display` | resolution and modes, several screens | VESA modes | X11/RandR, the window size | Preferences Video |
| `gapi_audio` | volume, output device | SoundBlaster mixer | PulseAudio/PipeWire, ALSA | Preferences Sound |

The order follows what the Preferences need first and what can be tested:

1. time (done)
2. power
3. printing
4. locale and time zone
5. network
6. storage
7. display and audio

## 4. Build and layout

- `CInclude/gapi.h`: the interface, prefix `gapi_`.
- `Gapi/CMakeLists.txt`: the static library `gapi`, which links `ghal`. The kernel links both.
- One backend per host: `Gapi/Dos32/gapi_dos32.c` and `Gapi/Linux/gapi_linux.c`. The Linux backend is not POSIX: it uses Linux services (D-Bus, systemd, CUPS). Later `Gapi/Win32` and `Gapi/GonWare`.
- Tests: `Port32/Tests/test_gapi.c` (ctest `gapi.time`).

## 5. Decisions (2026-11-12)

1. **D-Bus: a small client of our own** that fits the GEOS system. It talks the D-Bus wire protocol over the Unix socket, so there is no libdbus or sd-bus and no target library in the cross builds. It goes under `Gapi/Linux`. It is the path to systemd (`timedate1`, `login1`, `locale1`), polkit, NetworkManager and udisks, and the way logs reach the journal in the `session` role.
2. **Printing: GEOS gives CUPS PostScript or PDF** of its own. It does not send raw data of the old printer drivers. On Linux, the spooler renders the job with a GEOS PostScript/PDF driver and hands it to the CUPS queue. The printer's own driver is then CUPS's business.
3. **Guest by default; session when GEOS is the only UI** (section 2, rule 5). Journal logging and the password prompts come with the D-Bus client, when GEOS is that far.

Still open: command-line tools (`timedatectl`, `lpstat`) as a stopgap before the D-Bus client, or not at all.
