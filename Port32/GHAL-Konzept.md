# GHAL: GEOS Hardware Abstraction Layer

**Status:** Grundkonzept, keine Implementierung.

**Gilt für:** pcgeos allein. Wie GonWare selbst Hardware abstrahiert (GWVIDEO usw.), ist ein anderes Thema. GonWare ist hier nur einer von mehreren späteren Hosts.

**Grundlage:** Messungen in diesem Repo (Stand 2026-11), der Pilot `Port32/Math/`, `Port32/GonDesk-GEOS-auf-GonWare.md`.

## 1. Ziel

GEOS soll auf mehreren Unterbauten (*Hosts*) laufen, ohne dass der Code darüber den Host kennt:

| Reihenfolge | Host | Architekturen | Bemerkung |
|---|---|---|---|
| 1 | **`dos32`** | i686 | DOS mit DPMI (HX/HDPMI32). Die vorhandenen DOS-Teile werden übernommen, aber 32-Bit. **DOS muss die ganze Zeit lauffähig bleiben.** |
| 2 | **`linux`** | x86-64, i686, aarch64, armhf | erste Erweiterung; Anzeige über SDL3, X11 oder fbdev; zugleich die Entwicklungsumgebung (gdb, ASan) |
| 3 | `win32` / `win64` | i686, x86-64 (aarch64 möglich) | Windows-API |
| 4 | `gonware` | x86-64, i686, aarch64 | eigenes Konzept (`GonDesk-GEOS-auf-GonWare.md`); hier nur ein weiterer Host |
| – | weitere | – | z. B. macOS oder Bare Metal, wenn jemand es braucht |

Die **GHAL** ist die eine Stelle, an der GEOS den Host berührt. Der Host wird **beim Build** gewählt.

## 2. Bestandsaufnahme: Wo berührt GEOS heute die Hardware?

### 2.1 Was schon abstrahiert ist: das Treibermodell

GEOS hat seit jeher Treiber mit einer Strategie-Routine und Funktionsnummern (`Include/driver.def`, 26 `DRIVER_TYPE_*`). Die wichtigsten Schnittstellen sind:

| Schnittstelle | Datei | Funktionen |
|---|---|---|
| Video `DR_VID_*` | `Include/Internal/videoDr.def` | 34 (Rechteck, Linie, Polygon, Region, BitBlt, Strings, Mauszeiger, Save-Under, XOR, Palette, Exklusivzugriff …) |
| Dateisystem `DR_FS_*` | `Include/Internal/fsDriver.def` | 24 (Disk, Pfade, Handle-, Alloc- und Pfadoperationen, Enum, Laufwerkssperre …) |
| Tastatur `DR_KBD_*` | `Include/Internal/kbdDr.def` | 10 (Status, Scancode-Übersetzung, Hotkeys …) |
| Maus `DR_MOUSE_*` | `Include/Internal/mouseDr.def` | 15 (Rate, Beschleunigung, Kalibrierung …) |
| Swap `DR_SWAP_*` | `Include/Internal/swapDr.def` | 7 |
| Task-Switcher `DR_TASK_*` | `Include/Internal/taskDr.def` | 6 |
| Energie `DR_POWER_*` | `Include/Internal/powerDr.def` | 16 |
| Fonts `DR_FONT_*` | `Include/Internal/fontDr.def` | 6 |
| Stream, Seriell, Parallel, Sound, Socket, Drucker … | `streamDr.def`, `serialDr.def`, `socketDr.def`, `printDr.def` … | – |

Dazu gibt es **zwei Vorläufer einer Host-Abstraktion** im Repo:

- **`Library/GDI`** („Generic Device Interface“, Geoworks 1996): Tastatur, Zeiger und Energie über registrierte Callbacks (`GDIKeyboardRegister`, `GDIPointerRegister`, `GDIPowerRegister` …). Es gibt eine Implementierung `GenPC` und Produktvarianten `win32`. Die passenden Treiber sind `Driver/Keyboard/GDI` und `Driver/Mouse/GDIPointer`. Geoworks hat also schon einmal GEOS-Eingabe vom PC-Hardware-Modell gelöst.
- **`Library/HostIf`** (blueway 2023, `int 0xA0`): Video, Sockets und SSL gehen an den Host. Benutzt wird es von `Driver/Video/VGAlike/VGA16`, `Driver/Socket/HstTCPIP` und `Library/SSL`.

**Folge:** Der größte Teil der Hardware ist schon hinter Treibern. Die GHAL muss das Treibermodell nicht neu erfinden. Sie muss vor allem das abdecken, was **nicht** hinter Treibern liegt.

### 2.2 Was nicht abstrahiert ist: direkte Zugriffe

Gezählt in `asm`/`def`:

| Zugriff | Wo (Treffer) |
|---|---|
| DOS `int 21h` | `Driver/IFS/DOS` 458, **Kernel `FSD` 93**, **`Loader` 36**, `Driver/Task` 16+, `Driver/Swap/XMS` 11, `Driver/Net/NW` 11, **Kernel `Format` 10, `File` 10, `Sys` 9, `Timer` 8, `Diskcopy` 8**, `Boot`, `Dosappl` |
| BIOS `int 10h/15h/16h` | Loader (Video-Erkennung), Kernel `Graphics`, `Timer`, `Thread`, `Boot`, `Sys`; Maustreiber PS/2 |
| EMS/XMS/Multiplex `int 67h/2Fh` | `Driver/Swap/EMS`, `XMS`, `Driver/Task`, `IFS/DOS`, Kernel `Sys`, `FSD`, `Boot` |
| Port-I/O `in`/`out` | Video 274, Seriell 93, Parallel 25, **Kernel `Sys` 28** (PIC, PIT), Sound 42, Maus 70+ |
| Interrupt-Vektoren (`SysCatchInterrupt`, `SysCatchDeviceInterrupt`) | Kernel `Timer`, `Thread`, `Sys`, `FSD`; Treiber IFS/DOS, Seriell, Parallel, SoundBlaster, EMS, Task, Pen-Mäuse, `Library/Math/Compiler` (FPU) |
| BIOS/DOS-Sperre `SysLockBIOS` | 60 Dateien (IFS/DOS 18, Task, APM-Power, NetWare, EtherODI, Kernel `Sys`/`Thread`, Swap) |
| Interrupts aus (`INT_OFF`/`cli`) | **Kernel 120**, übrige Libraries 35, Treiber 119 |

Was **fett** markiert ist, liegt im Kernel und im Loader und damit außerhalb des Treibermodells. Das ist der eigentliche Kern der GHAL.

## 3. Aufbau

Die GHAL hat **zwei Ebenen**:

```
┌───────────────────────────────────────────────────────────────────┐
│ Apps, Komponenten, SpecUI, User                                    │
│ GEOS-Kernel: Heap, LMem, VMem, Object, Geodes, Graphics, Win, IM,  │
│              Thread-Scheduler, File-API, FSD                       │
├────────────────────────────┬──────────────────────────────────────┤
│ GEOS-Treiber (Treibermodell│ GHAL-Kern (neu, klein, nur vom Kernel │
│ DR_VID_*, DR_FS_*, KBD,    │ und den Treibern benutzt):            │
│ MOUSE, Stream, Sound,      │ Start, Speicher, Zeit, Kontexte,      │
│ Power, Font, Socket …)     │ Ereignisse/kritische Abschnitte,      │
│ je Host ein Treibersatz    │ Module, Log/Debug, Ende               │
│                            │ + Host-Dienste: Anzeige, Eingabe,     │
│                            │   Dateien, Audio, Netz (optional)     │
├────────────────────────────┴──────────────────────────────────────┤
│ Host: DOS+DPMI │ Linux (SDL3/X11/fbdev) │ Windows │ GonWare │ …   │
└───────────────────────────────────────────────────────────────────┘
```

- **Ebene 1, der GHAL-Kern:** eine kleine C-API (Präfix `ghal_`), die es heute nicht gibt. Sie ersetzt die direkten Zugriffe von Kernel und Loader aus 2.2. Jeder Host muss sie **vollständig** implementieren.
- **Ebene 2, die Treiber:** Die bestehenden Treiberschnittstellen bleiben der Vertrag (nach C portiert, mit C-Signaturen statt Registern). Pro Host gibt es einen **Treibersatz**. Damit nicht jeder Host jeden Treiber neu schreiben muss, bietet der GHAL-Kern optional **Host-Dienste** (Anzeigefläche, Eingabe-Ereignisse, Dateien, Audio, Netz). Ein **generischer Treiber** auf diesen Diensten bedient dann viele Hosts:
  - **ein** Video-Treiber „FB“ (VidCom-Modell, Software-Rendering auf einer linearen Fläche) für VESA-LFB, SDL3, X11, fbdev, Windows-DIB und GonWare
  - **ein** Dateisystem-Treiber „HostFS“ (`DR_FS_*` über `ghal_fs`) für Linux, Windows und GonWare
  - **ein** Tastatur- und **ein** Maustreiber auf `ghal_input`, ähnlich wie `Library/GDI` es schon gemacht hat
  - **ein** Socket-Treiber auf den Host-Sockets, wie `HstTCPIP`

Unter DOS bleiben dagegen die echten Hardware-Treiber (VGA/VESA, PS/2, IFS/DOS, Seriell, SoundBlaster …), portiert auf 32-Bit. Genau so „übernimmt DOS die aktuellen DOS-Teile“.

## 4. GHAL-Kern im Einzelnen

| Gruppe | Aufgabe | `dos32` | `linux` | `win32/64` |
|---|---|---|---|---|
| `ghal_boot` | Start, Argumente, Umgebung, Pfad zu `geos.ini`, Systemverzeichnis | DPMI-Programm (PE über HX oder LE), ersetzt `Loader/` | `main()` | `WinMain`/`main` |
| `ghal_mem` | große Speicherbereiche holen, freigeben, Schutz setzen | DPMI `int 31h` (0501h …) | `mmap` | `VirtualAlloc` |
| `ghal_time` | monotone Uhr, Wanduhr, **Tick-Quelle** für den Scheduler | PIT (IRQ0) + RTC | `clock_gettime`, Timer-Thread | `QueryPerformanceCounter`, Timer-Thread |
| `ghal_ctx` | Ausführungskontexte für GEOS-Threads (siehe 4.1) | eigene Stacks + Kontextwechsel | ein Host-Thread pro GEOS-Thread | dto. mit Win32-Threads |
| `ghal_crit` | kritische Abschnitte, ersetzt `INT_OFF`/`INT_ON` (siehe 4.2) | `cli`/`sti` | Kernel-Sperre | Kernel-Sperre |
| `ghal_irq` | asynchrone Ereignisquellen anmelden, ersetzt `SysCatch(Device)Interrupt` (siehe 4.2) | echte IRQ-Vektoren über DPMI | Host-Thread/Ereignisschleife ruft den Handler | dto. |
| `ghal_hostlock` | Sperre für nicht reentrante Host-Dienste, ersetzt `SysLockBIOS` | DOS-Sperre wie heute | nur wo nötig, z. B. Aufrufe, die auf den Hauptthread müssen (4.3) | dto. |
| `ghal_module` | Code nachladen (siehe 4.4) | eigener Lader | eigener Lader (optional `dlopen` zum Debuggen) | eigener Lader |
| `ghal_log` | Debug-Ausgabe, Assert, Absturzbericht, Breakpoint | seriell/Datei | stderr, `SIGTRAP` | `OutputDebugString`, `DebugBreak` |
| `ghal_exit` | Beenden, Neustart, Zurück zum Host | zurück zu DOS | `exit` | `ExitProcess` |
| `ghal_caps` | Fähigkeiten abfragen (Audio, Netz, Mehrfach-Fenster …) | – | – | – |

**Host-Dienste** (optional, für die generischen Treiber):

| Dienst | Inhalt | `dos32` | `linux` | `win32/64` |
|---|---|---|---|---|
| `ghal_display` | Modi, Fläche (Pointer, Pitch, Format), Rechtecke ausgeben, Größenänderung | VESA VBE 2/3 LFB (oder direkt der VGA-Treiber) | SDL3 / X11-SHM / fbdev | DIB-Section + `BitBlt` oder SDL3 |
| `ghal_input` | Ereignisse im **openinput-Format** `{page, usage, value, source}` (HID-Usages) | PS/2 bzw. die DOS-Treiber | SDL/X11/evdev → HID | Win32-Nachrichten → HID |
| `ghal_fs` | Dateien, Verzeichnisse, Attribute (langer Name, Token, Protokoll) | (nicht nötig, IFS/DOS) | POSIX + xattrs `user.geos.*` | Win32 `*W`-API + NTFS-Streams (`:geos`) |
| `ghal_audio` | PCM-Ausgabe | (SoundBlaster-Treiber) | SDL3-Audio / OSS / ALSA | SDL3 / WASAPI |
| `ghal_net` | TCP/UDP, Namensauflösung | (GEOS-eigener TCP/IP-Stack über Packet-Driver) | BSD-Sockets | Winsock |

### 4.1 Threads und Scheduler

GEOS hat einen eigenen Scheduler (Prioritäten, Zeitscheiben über den Timer-Interrupt). **Er bleibt maßgeblich**, damit sich GEOS auf allen Hosts gleich verhält. Die GHAL liefert nur die Kontexte:

- **`dos32`:** wie heute. Eigene Stacks und Kontextwechsel, Präemption über IRQ0.
- **Hosts mit Threads (Linux, Windows, GonWare):** Jeder GEOS-Thread wird ein Host-Thread. Es läuft aber immer **nur der Thread, den der GEOS-Scheduler gewählt hat**. Dafür gibt es eine globale Kernel-Sperre, die bei jedem Kontextwechsel weitergereicht wird. Blockierende Host-Aufrufe (Datei, Netz) geben die Sperre vorher ab, damit andere GEOS-Threads weiterlaufen.
- **Präemption auf diesen Hosts:** Ein Tick-Thread setzt „Neuplanung fällig“. Geprüft wird an Kernel-Eintrittspunkten und am Ende kritischer Abschnitte. Harte Präemption (Signal unter Linux, `SuspendThread` unter Windows) ist erst später nötig, wenn Rechenschleifen ohne Kernel-Aufruf stören.
- **Verworfen:** reine Benutzer-Kontexte (`ucontext`/Fibers) auf den gehosteten Systemen. Ein blockierender Host-Aufruf würde dort ganz GEOS anhalten.

### 4.2 Interrupts werden zu Ereignissen

Heute schützt sich der Kernel mit `INT_OFF`/`INT_ON` (120 Stellen im Kernel), und Treiber hängen sich an Interrupt-Vektoren.

- `INT_OFF`/`INT_ON` ist schon ein Makro und wird zu **`ghal_crit_enter`/`leave`** (verschachtelbar). Unter `dos32` ist das wie bisher `cli`/`sti`, auf den anderen Hosts eine Sperre gegen den „Interrupt-Kontext“.
- **Interrupt-Kontext:** Unter DOS ist das der echte IRQ-Handler. Auf den anderen Hosts ist es ein Host-Thread (Timer, Ereignisschleife, Netz), der den Handler des Treibers zwischen `ghal_irq_enter`/`exit` aufruft. Damit ist er gegen den Kernel serialisiert, genau wie ein echter Interrupt.
- `SysEnterInterrupt`/`SysExitInterrupt` und die Wake-up-Pfade des Kernels bleiben in ihrer Logik unverändert.

### 4.3 Der Hauptthread des Hosts

SDL, X11 und Win32 erwarten, dass Fenster und Ereignisse auf **einem bestimmten Thread** bedient werden. Darum gilt: Die GHAL **besitzt den Hauptthread des Hosts** und betreibt dort die Ereignisschleife. GEOS läuft auf eigenen Threads. Aufrufe an `ghal_display` werden bei Bedarf an den Hauptthread weitergereicht. Unter DOS gibt es diese Unterscheidung nicht.

### 4.4 Geodes laden

Heute lädt der GEOS-Kernel seine `.geo`-Dateien **selbst** (eigenes Format, eigene Relokation). Das bleibt so, nur mit einem 32/64-Bit-Format, weil es **auf allen Hosts gleich funktioniert** und kein `dlopen` braucht. Das ist wichtig für DOS, wo es kein `dlopen` gibt.

- **Empfehlung:** Ein kleiner, eigener **ELF-Lader** im Kernel. Ein Geode ist dann ein ELF-Shared-Object mit Export- und Import-Tabelle aus der `.gp`-Datei, und die Protokollnummern bleiben. Es gibt ein Format für alle Hosts. Die Toolchain (GCC/Clang/lld) erzeugt es direkt.
- Unter Linux zusätzlich ein Debug-Modus über `dlopen`, damit gdb die Symbole sieht (oder Anmeldung über die gdb-JIT-Schnittstelle).
- Der Kernel selbst und der GHAL-Kern werden zusammen als normales Programm des Hosts gebaut, also als DOS-PE/LE, ELF-Programm oder `.exe`.

### 4.5 Die bisherigen Loader-Aufgaben

`Loader/` (Video-Erkennung, Logo, `geos.ini` finden, Kernel laden, Speicher vorbereiten) geht auf in `ghal_boot` und den Start des Kernels. Die Video-Erkennung wird zum Teil des Video-Treibers bzw. von `ghal_display`.

## 5. Treibersätze pro Host

| Treiberklasse | `dos32` (aus den heutigen DOS-Teilen, 32-Bit) | `linux` | `win32/64` | `gonware` (später) |
|---|---|---|---|---|
| Video | VGA/SVGA/**VESA-LFB** (aus `VGAlike`, `VidCom`) | FB-Treiber auf `ghal_display` (SDL3/X11/fbdev) | FB-Treiber auf DIB/SDL3 | FB-Treiber auf GWVIDEO |
| Tastatur | `Driver/Keyboard` (int 9, Layouts) | Treiber auf `ghal_input` | dto. | dto. |
| Maus | PS/2, seriell (`Driver/Mouse`) | Treiber auf `ghal_input` | dto. | dto. |
| Dateisystem | **IFS/DOS** (+ MS7/LFN), IFS/GEOS | **HostFS** auf `ghal_fs` | HostFS | HostFS (NSS) |
| Seriell/Parallel | `Driver/Stream/Serial`, `Parallel` | termios / CUPS-Datei | COM-API | – |
| Sound | SoundBlaster (+ AC'97 neu) | auf `ghal_audio` | dto. | GWSOUND |
| Energie | APM / hltpwr | NoPower | NoPower | ACPI über Host |
| Swap, Task-Switcher | **entfallen** (DPMI-Speicher reicht, Task-Switcher ist eine DOS-Altlast) | entfallen | entfallen | entfallen |
| Fonts | FreeType (`Driver/Font/TrueType`, schon C) | dto. | dto. | dto. |
| Netz | GEOS-TCP/IP (`Driver/Socket/TCPIP`) über Packet-Driver (`EtherPKT`) | Host-Sockets (Modell `HstTCPIP`) | Winsock | Host-Sockets |
| Drucken | `Driver/Printer` (portiert) | Ausgabe an CUPS | Windows-Druck | PAPPL |

## 6. Einführung in Schritten: DOS läuft immer

**Grundsatz:** Nach jedem Schritt läuft GEOS unter DOS. Linux kommt als erste Erweiterung dazu.

| Schritt | Inhalt | Ergebnis |
|---|---|---|
| **S0: GHAL im 16-Bit-Code einziehen** | Im *heutigen* asm-Kernel die direkten Zugriffe aus 2.2 (Loader, Kernel `Sys`, `Timer`, `FSD`, `File`, `Boot`, `Thread`) hinter GHAL-Einsprungpunkte legen, noch in asm. `INT_OFF`/`INT_ON`, `SysLockBIOS` und `SysCatch*Interrupt` laufen über die GHAL-Makros und -Routinen. | Unverändertes 16-Bit-GEOS unter DOS, aber **jeder Kontakt zum Host ist an einer Stelle sichtbar** und mit Namen versehen. Testbar auf echter Hardware und in den üblichen DOS-Umgebungen. |
| **S1: 32-Bit unter DOS** | Code oberhalb der GHAL nach C/32-Bit portieren (siehe Pilot). GHAL-Kern `dos32` aus den S0-Teilen über DPMI; die DOS-Treiber auf 32-Bit (VESA-LFB statt Bank-Switching, Tastatur, Maus, IFS/DOS). | GEOS 32-Bit unter DOS/HX. DOS bleibt die Referenzplattform. |
| **S2: Linux** | GHAL-Kern `linux`, Host-Dienste, generische Treiber (FB, HostFS, Eingabe, Sockets); Anzeige zuerst SDL3, dann fbdev und X11 | GEOS unter Linux; gdb und ASan für alle Plattformen nutzbar |
| **S3: Windows** | GHAL-Kern `win32`/`win64`, dieselben generischen Treiber | GEOS unter Windows |
| **S4: GonWare** | GHAL-Kern `gonware` (siehe GonDesk-Dokument) | GonDesk |

**Praktischer Hinweis zu S1/S2:** Ein minimaler `linux`-Host ohne Grafik (nur Kern, Speicher, Threads, Dateien) lohnt sich schon früh in S1 als **Testumgebung** für die portierten Kernel-Teile. Dafür braucht es keinen Emulator, nur Unit- und Integrationstests mit gdb und ASan. Die Plattform-Reihenfolge (DOS zuerst) ändert sich dadurch nicht.

## 7. Kein Assembler mehr: C ist die einzige Sprache

Sobald mehr als eine Architektur dazukommt (amd64, arm64, armhf, i386 …), ergibt handgeschriebener Assembler keinen Sinn mehr. Man müsste jede Routine pro Architektur neu schreiben und pflegen. Deshalb gilt:

1. **Oberhalb der GHAL gibt es keinen Assembler.** Kernel, Libraries, Treiber und Apps sind C (bzw. GOC → C). Der heutige asm-Code (rund 3,7 Mio. Zeilen) wird übersetzt, nicht pro Architektur nachgebaut. Das gilt auch für DOS (`dos32` ist ebenfalls C, nur eben für i386 gebaut).
2. **Im GHAL-Kern so wenig Assembler wie möglich**, und nur dort, wo es technisch nicht anders geht:
   - **Kontextwechsel** nur für `dos32` (i386, eine kleine Routine). Gehostete Systeme brauchen keinen, weil GEOS-Threads dort Host-Threads sind (4.1).
   - **IRQ-Einsprünge unter DPMI** (`dos32`): kurze Stubs, die in C-Handler springen.
   - Sonst nichts. Atomics und Barrieren kommen aus C11 (`<stdatomic.h>`), Bit-Tricks aus Compiler-Builtins (`__builtin_clz` …).
3. **Optimierungen** (z. B. Blit- und Füllroutinen des Software-Renderers) zuerst in portablem C. Nur wo Messungen es verlangen, kommen Compiler-Intrinsics (SSE2/NEON) **mit C-Fallback** dazu, kein freistehender Assembler.
4. **Was der Assembler heute implizit garantiert, muss in C ausdrücklich werden:**
   - **Speichermodell:** x86 ordnet Speicherzugriffe stark, ARM nicht. Gemeinsame Daten zwischen Threads/Interrupt-Kontext brauchen C11-Atomics oder die `ghal_crit`-Abschnitte, keine „das geht schon“-Annahmen.
   - **Alignment:** armhf verträgt unausgerichtete Zugriffe nur eingeschränkt. Gepackte Strukturen nur über `memcpy`-Accessoren lesen (siehe Datenformate im GonDesk-Dokument, 6.3).
   - **Bytereihenfolge:** alle Ziele sind little-endian; trotzdem Plattenformate nur über `le16/le32`-Accessoren, dann bleibt ein Big-Endian-Ziel möglich.
   - **Fließkomma:** `double` überall. Das passt auch zu armhf, wo `long double` nur 64 Bit hat (ein Grund mehr gegen das 80-Bit-Format).
   - **Wortbreite:** 32- und 64-Bit-Ziele gleichzeitig. Darum feste Breiten für Daten (`uint16_t`/`uint32_t`) und `uintptr_t`/`size_t` für Adressen und Größen.
5. **Architektur-Matrix in der CI:** mindestens ein 32-Bit- und ein 64-Bit-Ziel sowie ein ARM-Ziel, z. B. linux-amd64, linux-i386, linux-arm64 und linux-armhf (ARM über QEMU-User), dazu dos32. Dann fallen 64-Bit-, Alignment- und Speichermodell-Fehler sofort auf.

Der Schritt S0 (6.) widerspricht dem nicht: Er ordnet nur den bestehenden 16-Bit-asm-Code hinter GHAL-Einsprungpunkte, damit DOS weiterläuft. Ab S1 entsteht neuer Code ausschließlich in C.

## 8. Build

**Build-System ist CMake** (Entscheidung 2026-11-10); Tests über CTest. Einstieg vorerst `Port32/CMakeLists.txt`.

```sh
cmake -S . -B build-dos   -G Ninja -DGEOS_HOST=dos32  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-dos32-hx.cmake
cmake -S . -B build-linux -G Ninja -DGEOS_HOST=linux  -DGEOS_LINUX_DISPLAY=sdl3   # oder x11, fbdev
cmake -S . -B build-w64   -G Ninja -DGEOS_HOST=win64  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake
```

| `GEOS_HOST` | Toolchain |
|---|---|
| `dos32` | Open Watcom 2 (LE/PE für DOS-Extender) oder i686-GCC (DJGPP, bzw. MinGW-PE für HX) |
| `linux` | GCC oder Clang |
| `win32`/`win64` | MinGW-w64 (GCC/Clang) oder clang-cl/MSVC |
| `gonware` | Clang/lld gegen mlibc |

**Regeln:**

1. **`#ifdef GEOS_HOST_*` und Host-Header nur in `ghal/` und in den Host-Treiberverzeichnissen.** Alles andere wird für alle Hosts identisch gebaut. Die CI prüft das per Skript.
2. **Architektur ist nicht Host.** x86-64, i686, aarch64 und armhf werden nur über Typen behandelt (`uintptr_t`, `intptr_t`, feste Breiten), nicht über `#ifdef`.
3. **Windows 64-Bit ist LLP64:** `long` hat dort 32 Bit. Also **nie `long` für Pointer oder Größen**, sondern `intptr_t`/`size_t`. Und `wchar_t` hat dort 16 Bit, darum nie `wchar_t` als GEOS-Zeichentyp.
4. Fehlende Fähigkeiten über `ghal_caps()` abfragen, nicht aus dem Host-Namen schließen.
5. Jeder Host hat einen eigenen CI-Lauf:
   - `linux` nativ (Grafik über ein Offscreen-Backend),
   - `win*` mit Wine oder einem Windows-Runner,
   - `dos32` in QEMU mit FreeDOS + HDPMI32,
   - `gonware` in QEMU.

**Verzeichnisaufbau (Vorschlag):**

```
ghal/
  include/ghal/*.h            API des GHAL-Kerns und der Host-Dienste
  common/                     Software-Renderer, HID-Tabellen, Hilfen
  dos32/  linux/  win/  gonware/
  linux/display-sdl3/  linux/display-x11/  linux/display-fbdev/
Driver/Host/                  generische Treiber auf den Host-Diensten
  Video/FB  IFS/HostFS  Keyboard/Host  Mouse/Host  Socket/Host  Sound/Host
```

## 9. Offene Entscheidungen

1. **S0 wirklich im 16-Bit-asm machen?** Die Empfehlung ist ja: Es macht alle Host-Kontakte sichtbar, ohne etwas zu brechen, und jeder spätere Schritt baut darauf auf. Die Alternative ist, die GHAL erst beim C-Port einzuführen, dann aber ohne lauffähiges Zwischenergebnis.
2. **Scheduler auf gehosteten Systemen:** Host-Threads plus Kernel-Sperre (Empfehlung) oder etwas anderes?
3. **Geode-Format:** eigener ELF-Lader (Empfehlung) oder Host-Lader (`dlopen`, `LoadLibrary`, HX-PE-DLLs)?
4. **DOS-Extender:** HX (PE, Win32-Emulation über DKRNL32 als Bonus) oder ein klassischer LE-Extender (DOS/4GW-kompatibel, z. B. DOS/32A)?
5. **`Library/GDI` wiederverwenden?** Ihr Callback-Modell für Tastatur, Zeiger und Energie passt gut zu `ghal_input`. Entweder als Vorlage nehmen oder direkt als Brücke weiterführen.
6. **Was entfällt endgültig?** Swap (EMS/XMS), Task-Switcher, CGA/EGA/Hercules, Pen-Geräte, PCMCIA. Vorschlag: alles davon in S1.

## 10. Bezug zu anderen Dokumenten

- `Port32/Math/README.md`: Pilot. Der Shim `fpstack.c` ist ein Beispiel für „Code oberhalb der GHAL, unabhängig vom Host“.
- `Port32/GonDesk-GEOS-auf-GonWare.md`: der Host `gonware` im Detail (GWVIDEO, openinput, NSS, Ring 3). Die GHAL dort ist dieselbe wie hier.
