# GonDesk: GEOS als Oberfläche auf dem GonWare-Kernel

**Status:** Konzeptpapier, keine Implementierung.

**Grundlagen:**

- dieses Repo (Stand 2026-11)
- der Pilot `Port32/Math/`
- `../GonWare/CONCEPT.md` (Abschnitt „Outlook: GonDesk – a desktop with GEOS“)
- `../GonWare/PLAN.md`

Die Begriffe aus GonWare werden übernommen: GDOS, `SERVER`, GLM, Ring 0/3, mlibc, openinput, openfat, NSS, GDS.

## 1. Ausgangslage

**GonWare** ist ein eigenständiges Server-Betriebssystem im Stil von NetWare, kein Linux. Es hat:

- einen eigenen Kernel für x86-64, i686 und ARM64, gestartet über UEFI bzw. auf BIOS-PCs über OpenDuet,
- eine DOS-artige Phase (GDOS), nach der `SERVER` lädt,
- GLMs (ELF-Module) in Ring 0 sowie geschützte Ring-3-Prozesse für .NET, PowerShell und Fremdsoftware,
- die Lizenz GPLv2-only.

**GonDesk** ist laut `CONCEPT.md` ein eigenes Desktop-System auf dem GonWare-Kernel mit GEOS als Oberfläche. Festgelegt ist dort bereits:

| Festlegung (CONCEPT.md) | Folge für die GEOS-Seite |
|---|---|
| Zusammenarbeit mit OpenGEOS, Beiträge gehen upstream unter Apache 2.0 | Alles, was in `pcgeos` entsteht, bleibt Apache 2.0 und darf keinen GPL-Code enthalten (siehe Abschnitt 8) |
| ~~Erst emuliert (x86-16-Emulator)~~, nativer Port als Ring-3-Programm mit einer Schicht für das GEOS-Speichermodell | **Entscheidung 2026-11-10: keine Emulation, ein richtiger Port; Bruch der Kompatibilität zum alten GEOS erlaubt.** CONCEPT.md sollte an dieser Stelle angepasst werden |
| Far-Pointer und 16-Bit-Annahmen sofort hinter eigene Typen und Makros legen | genau der Ansatz des Piloten (`fpstack.h`, `FFA_stackFrame`), muss aber verbindlich werden |
| GEOS bleibt ein eigenes Programm und **kein GLM** im Kernel-Adressraum | GEOS läuft in Ring 3, linkt nur gegen `LIBC.DLL` (mlibc, MIT) und eigene Bibliotheken |

## 2. Schichtenbild

```
┌──────────────────────────────────────────────────────────────────┐
│ GEOS-Apps: GeoWrite, GeoCalc, FileMgr, Breadbox ...              │ Ring 3
│ Komponenten: Text, GrObj, Spreadsheet, Chart, Math, Parse        │
│ SpecUI: CommonUI (+ Motif/ISUI/NewUI)                            │
│ User:   Gen*/Vis*-Klassen                                        │
│ GEOS-Laufzeit: Object · Geodes · LMem · VMem · Graphics · Win · IM│
├──────────────────────────────────────────────────────────────────┤
│ GEOS-Host-Schicht (GHAL, neu, Apache 2.0)                         │
│   Host „gonware“ │ Host „linux“ (sdl3/x11/fbdev) │ Host „dos32“    │
├──────────────────────────────────────────────────────────────────┤
│ GonWare Ring 3: LIBC.DLL (mlibc), Ring-3-pthreads, ELF-Loader    │
│   mit Shared Libraries, Framebuffer-Zugang, Eingabe-Events,      │ Phase 6
│   Dateien (FAT/exFAT/NSS), Sockets                               │
├──────────────────────────────────────────────────────────────────┤
│ GonWare-Kernel + GLMs: GWVIDEO, GWSOUND, openinput, openfat, NSS │ Ring 0
└──────────────────────────────────────────────────────────────────┘
```

Der Host „linux“ ist das **Entwicklungsziel**. Auf GonWare gibt es Ring 3, also Prozesse, Shared Libraries und pthreads im Userland, erst ab GonWare-Phase 6. Der native GEOS-Port kann aber sofort unter Linux mit gdb und ASan entwickelt werden. Welcher Host gebaut wird, wird **beim Build angegeben** (Abschnitt 4.5); darüber ist der Code für alle Hosts identisch.

## 3. Keine Emulation: Folgen

GonDesk ist **nur nativ**. Es gibt keinen x86-16-Emulator und keinen „Classic“-Modus.

**Entscheidung 2026-11-10: ein richtiger Port, der Bruch der Kompatibilität mit dem alten GEOS ist in Kauf genommen.** Das heißt:

- **Binärkompatibilität: keine.** Alte 16-Bit-Geodes (`.geo`) laufen nicht. Jede App wird aus dem Quelltext neu gebaut (GOC) oder übersetzt (asm). Apps ohne Quelltext im Repo entfallen.
- **Datenkompatibilität: keine.** Alte Dokumente (GeoWrite, GeoCalc, GeoDraw, GeoDex …), GEOS-Fonts, `.ini`-Dateien und GStrings müssen nicht gelesen werden. Formate werden neu und sauber definiert (siehe 6.3). Ein Import-Werkzeug für Altdokumente ist höchstens ein späteres, optionales Extra.
- **Quelltextkompatibilität: so weit wie sinnvoll.** Das ist die *einzige* Kompatibilität, die sich lohnt: An der GOC/C-API (Gen-Klassen, Messages, `@object`, `MemLock`, `ObjCallInstanceNoLock` usw.) hängen ~0,83 Mio. Zeilen GOC-Apps. Wird die API ohne Not geändert, müssen sie alle angefasst werden. Brüche hier deshalb nur, wenn sie echte Vereinfachung bringen, und dann mechanisch umstellbar (Skript statt Handarbeit).
- **Bis der native Desktop steht (N3), gibt es kein sichtbares GEOS auf GonWare.** Zwischenergebnisse werden unter Linux mit dem SDL3-Backend und über Konsolen-Tests gezeigt.

**Was von `Library/HostIf` bleibt:** Die Aufteilung der Host-Dienste (Video, Sockets, SSL, Benachrichtigungen bei Größenänderung) ist eine brauchbare Vorlage für die Schnittstelle der nativen Host-Schicht (GHAL). Das `int 0xA0`-Protokoll selbst wird nicht übernommen. Die SSL-Seite geht in der GHAL direkt auf mbed TLS, weil GonWare kein OpenSSL verwendet.

## 4. Nativer Port

### 4.1 Was die GonWare-Hardware-Schicht ersetzt

Auf GonWare entfällt fast das ganze `Driver/`. GEOS spricht nur noch mit der Host-Schicht.

| GEOS heute | Zeilen (asm+def) | GonWare-Ersatz |
|---|---|---|
| `Loader/` | ~20 k | ELF-Start aus Ring 3 (`COMMAND`, `AUTOEXEC` startet GonDesk) |
| Kernel `Heap` (physisch), `Thread`, `Timer`, `Sys` | ~45 k | Ring-3-Speicher, pthreads, `clock_gettime` (die Kernel-Seite ist in GonWare schon fertig: Threads, Prioritäten, SMP-Affinität, Timed-Waits) |
| `Driver/IFS` (DOS, GEOS, RFSD) | ~120 k | GonWare-Dateisysteme: FAT/exFAT über openfat, **NSS** |
| `Driver/Video` (VGA ... SVGA, VGA16) + `VidCom` | ~138 k | **GWVIDEO** (`VIDEO.GLM` + `*.DRV`, siehe 4.4). Unter Linux dafür SDL3 |
| `Driver/Keyboard`, `Driver/Mouse` | ~57 k + Maus | openinput (HID-Usages, Layouts de/us, Wiederholung, Lock-LEDs sind schon im Kernel) |
| `Driver/Font` | ~74 k | FreeType (`Driver/Font/TrueType` ist **schon C/FreeType**, ~27 k Zeilen) |
| `Driver/Socket`, `Driver/Net`, `Library/Socket` | ~130 k | GonWare-Sockets, TCP/IP **und IPX** |
| `Driver/Printer` + `Library/Spool` | ~175 k | libcups3/PAPPL (in GonWare geplant) |
| `Driver/Sound` | ~26 k | **GWSOUND** (`OSS.GLM`, OSS-v4-API `/dev/dsp`, Mixer) |
| `Driver/Stream`, `Power`, `Swap`, `Task`, `DMA` | ~30 k | entfällt, Energie über die ACPI-Dienste von GonWare |

### 4.2 GEOS und NetWare passen zusammen

Im Repo gibt es schon `Driver/Net/NW`, `Driver/IFS/DOS/NetWare` und `Library/Net` (NetWare-Netzwerk-Library). GEOS war als Client für NetWare-Netze gedacht.

Auf GonWare kann daraus eine native Integration werden:

- **NSS-Volumes** als GEOS-Laufwerke.
- **GDS-Identität** als GEOS-Benutzer. Ring-3-Prozesse laufen bei GonWare ohnehin mit einer GDS-Identität, und die NSS-Trustees gelten direkt.
- **Erweiterte Attribute:** GEOS legt lange Namen, Token/Dateityp, Release und Protokoll heute in `@DIRNAME.000` bzw. im GEOS-Dateiheader ab. Unter **NSS** gehören sie in dessen Name-Spaces und erweiterte Attribute. Auf FAT/exFAT bleibt als Fallback eine Sidecar-Datei.

### 4.3 Was portiert, also nach C neu geschrieben wird

Das ist unverändert gegenüber der Linux-Variante und hängt nicht am Host:

| Teil | Zeilen (asm+def+ui) |
|---|---|
| Kernel `Object`, `Geodes`, `LMem`, `VMem`, `Local`, `File`-API, `Initfile` | ~135 k |
| Kernel `Graphics` / `Win` / `IM` | ~103 k / ~18 k / ~11 k |
| `Library/User` | ~169 k |
| `Library/SpecUI/CommonUI` (Motif, ISUI, NewUI je nur 4–7 k obendrauf) | ~291 k |
| `Library/Text` | ~149 k |

Bis zum ersten nativen Desktop sind das **knapp 0,9 Mio. Zeilen**. Danach kommen die Komponenten (~0,4 Mio.) und die asm-Apps.

Ohne Neuschreiben, nur neu gebaut werden die **~0,83 Mio. Zeilen GOC** in `Appl/`, davon 219 k in `Appl/Breadbox`. Dafür braucht es ein neues `goc`-Backend.

### 4.4 GWVIDEO und GWSOUND (in GonWare schon festgelegt)

GonWare hat am 2026-11-10 seine Display- und Sound-Architektur festgelegt (`CONCEPT.md`, „Display and sound drivers“; `PLAN.md` Phase 3). Beides passt direkt zu GonDesk:

- **GWVIDEO** = `VIDEO.GLM` (Kern) + `*.DRV` pro Grafikkarte. Das Modell ist **clean-room aus dem GEOS-Videotreiber-Modell abgeleitet** (`Driver/Video` + `VidCom`: Standard-Operationstabelle, generische Implementierungen für einfache Treiber).
  - **Tier 1** (jeder Treiber): Modi, linearer Framebuffer, 2D-Primitive (Füllen, BitBlt, Bitmap/Glyphe, Linie, Palette, Save-Under für den Mauszeiger, XOR). Ein Software-Renderer macht das für den Dumb-Framebuffer über GOP, der immer funktioniert. Erste echte Ziele: virtio-gpu, bochs-display.
  - **Tier 2** (optional): 3D, DRM/GEM-artig. Für GonDesk nicht nötig.
  - **Folge für GonDesk:** Der Kernel `Graphics` von GEOS rendert heute über genau diese Operationen. Der native Grafik-Port kann seine Ausgabe deshalb fast 1:1 auf GWVIDEO Tier 1 legen, statt alles selbst in einen Framebuffer zu rechnen. Auf Hosts ohne diese Operationen (SDL3, X11, fbdev, VBE) übernimmt der Software-Renderer der GHAL das (4.5).
- **GWSOUND** = `OSS.GLM` (OSS-v4-API: `/dev/dsp`, Mixer, `SNDCTL_*`) + `*.DRV` pro Karte. GEOS' `Driver/Sound` (~26 k) wird durch die OSS-API ersetzt; unter Linux gibt es OSS-Emulation bzw. SDL3-Audio.

**Lizenz:** Weil GWVIDEO nur das *Modell* von GEOS übernimmt, nicht den Code, bleibt die Trennung sauber: GonWare-Code (GPLv2) enthält keinen GEOS-Code (Apache 2.0), und GonDesk spricht GWVIDEO/GWSOUND nur über Ring-3-Schnittstellen an.

### 4.5 Host-Schicht (GHAL) und Build-Ziele

> **Maßgeblich ist jetzt `Port32/GHAL-Konzept.md`** (GEOS Hardware Abstraction Layer, unabhängig von GonWare; Hosts `dos32`, `linux`, `win32/64`, später `gonware`). Dieser Abschnitt zeigt nur die erste Fassung aus GonDesk-Sicht.

Die Trennung zwischen GEOS und dem Unterbau ist **hart**: Alles oberhalb der GHAL kennt den Host nicht. Der Host wird beim Build gewählt, nicht zur Laufzeit erkannt.

#### Build-Ziele

| `GEOS_HOST` | Architekturen | Compiler/Toolchain | Zweck |
|---|---|---|---|
| `linux` | x86-64, i686, aarch64 | GCC oder Clang, glibc/musl | **Entwicklung**: gdb, ASan/UBSan, schnelle Zyklen, CI |
| `gonware` | x86-64, i686, aarch64 | Clang/lld gegen mlibc (`LIBC.DLL`), Toolchain-Dateien aus GonWare | **Ziel**: GonDesk |
| `dos32` | i686 | Open Watcom 2 oder GCC (DJGPP bzw. MinGW-PE für HX) | DOS mit DPMI (HX/HDPMI32): klassische PCs, Retro-Hardware |

Für `linux` wird zusätzlich die Anzeige gewählt:

| `GEOS_LINUX_DISPLAY` | Anzeige | Eingabe | Einsatz |
|---|---|---|---|
| `sdl3` (Standard) | Fenster unter X11/Wayland oder KMS-DRM | SDL-Events | normaler Entwicklungsalltag |
| `x11` | Xlib/XCB mit MIT-SHM | X11-Events | ohne SDL, alte Systeme, Fernzugriff über SSH-X-Forwarding |
| `fbdev` | `/dev/fb0` | evdev (`/dev/input/event*`) | Konsole ohne Grafikserver; kommt GonWare (Framebuffer + Rohereignisse) am nächsten und testet damit dessen Pfad |

Beispiel (CMake, wie GonWare):

```sh
cmake -S . -B build-linux   -G Ninja -DGEOS_HOST=linux -DGEOS_LINUX_DISPLAY=sdl3
cmake -S . -B build-fb      -G Ninja -DGEOS_HOST=linux -DGEOS_LINUX_DISPLAY=fbdev
cmake -S . -B build-gw-a64  -G Ninja -DGEOS_HOST=gonware \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-gonware-aarch64.cmake
cmake -S . -B build-dos32   -G Ninja -DGEOS_HOST=dos32 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-dos32-hx.cmake
```

Mehrere Anzeige-Backends unter Linux gleichzeitig (z. B. als ladbare Module, Auswahl per Umgebungsvariable) sind später möglich, aber nicht nötig. Die Grundregel bleibt: **ein Host pro Build**.

#### Schnittstellen der GHAL

Die GHAL ist eine schmale C-API (Präfix `ghal_`) in eigenen Headern. Jede Gruppe hat pro Host eine Implementierung.

| Gruppe | Inhalt | `linux` | `gonware` | `dos32` |
|---|---|---|---|---|
| `ghal_mem` | Seiten/Blöcke anfordern und freigeben | `mmap` | mlibc/Syscalls | DPMI-Speicher (`int 31h`) |
| `ghal_thread` | Threads, Mutex (rekursiv), Cond, Semaphore, TLS, Prioritäten | pthreads | Ring-3-pthreads | **eigener Scheduler** (DOS hat keine Threads): Kontextwechsel + IRQ0-Timer, wie der alte GEOS-Kernel |
| `ghal_time` | monotone Uhr, Wanduhr, Timer | `clock_gettime`, `timerfd` | `clock_gettime` | PIT/RTC |
| `ghal_file` | Dateien, Verzeichnisse, **Attribute** (langer Name, Token, Protokoll) | POSIX + xattrs `user.geos.*` | POSIX + NSS-Name-Spaces | `int 21h` mit LFN (71xxh) + Sidecar-Datei für Attribute |
| `ghal_module` | Bibliotheken/Apps nachladen, Symbole auflösen | `dlopen` (ELF `.so`) | `dlopen` (ELF `.DLL`) | PE-DLLs über HX `DPMILD32`, oder DXE3 bei DJGPP, oder statisch gelinkt |
| `ghal_display` | Modi, Oberfläche (Pointer, Pitch, Format), Flush von Rechtecken, optional 2D-Operationen | SDL3 / X11-SHM / fbdev | **GWVIDEO Tier 1** | VESA VBE 2/3 mit linearem Framebuffer über DPMI |
| `ghal_input` | Ereignisse im **openinput-Format** `{page, usage, value, source}` | SDL/X11/evdev → HID-Usages | openinput direkt | openinput (läuft laut GonWare-Plan auch unter DOS) bzw. PS/2, `int 33h` |
| `ghal_audio` | PCM-Ausgabe | SDL3-Audio oder OSS/ALSA | GWSOUND (OSS) | Sound Blaster / AC'97 (niedrige Priorität) |
| `ghal_net` | TCP/UDP-Sockets, Namensauflösung | BSD-Sockets | BSD-Sockets (+ IPX) | Packet-Driver + Watt-32 oder lwIP; optional |
| `ghal_tls` | TLS-Verbindungen | mbed TLS | mbed TLS | mbed TLS (optional) |
| `ghal_log` | Debug-Ausgabe, Assertions, Absturzbericht | stderr, `backtrace` | serielle/Konsole | serielle Schnittstelle, Datei |

**Zwei Formate werden für alle Hosts festgelegt**, damit die Schichten darüber nichts übersetzen müssen:

- **Eingabe:** das openinput-Format (HID-Usages). Linux-Backends übersetzen dorthin, GonWare liefert es direkt, unter DOS liefert es openinput selbst.
- **Anzeige:** die 2D-Operationen von GWVIDEO Tier 1 (die ja selbst aus dem GEOS/VidCom-Modell stammen). Hat ein Host sie nicht (SDL, X11, fbdev, VBE), macht ein gemeinsamer Software-Renderer in der GHAL die Arbeit auf dem Framebuffer.

#### Regeln

1. **`#ifdef GEOS_HOST_*` nur innerhalb von `ghal/`.** Ein Host-`#ifdef` außerhalb ist ein Build-Fehler (Prüfung per Skript in der CI).
2. **Keine Host-Header außerhalb von `ghal/`**: kein `<unistd.h>`, `<SDL3/...>`, `<dos.h>`, keine GonWare-Syscall-Header.
3. Die **Architektur** (x86-64, i686, aarch64) ist unabhängig vom Host und wird nur über Typen (`uintptr_t`, feste Breiten) behandelt, nicht über `#ifdef`.
4. Fähigkeiten, die ein Host nicht hat, werden **abgefragt** (`ghal_caps()`, z. B. „Audio“, „Netz“, „dynamisches Laden“), nicht über den Host-Namen erraten.
5. Jeder Host hat einen **Testlauf in der CI**: `linux` voll mit Grafik-Tests über ein Offscreen-Backend; `gonware` in QEMU wie die GonWare-Boot-Tests; `dos32` in QEMU oder DOSBox-X mit FreeDOS + HDPMI32.

#### Verzeichnisaufbau (Vorschlag)

```
Port32/ bzw. später eigenes Verzeichnis im Repo
  ghal/
    include/ghal/*.h          gemeinsame API
    common/                  Software-Renderer, openinput-Tabellen, Hilfen
    linux/                   mem, thread, file, module, time, net ...
    linux/display-sdl3/  linux/display-x11/  linux/display-fbdev/
    gonware/                 mlibc, GWVIDEO, openinput, NSS-Attribute
    dos32/                   DPMI, Scheduler, VBE, int 21h, PS/2
  cmake/
    toolchain-gonware-{x86_64,i686,aarch64}.cmake
    toolchain-dos32-hx.cmake   toolchain-dos32-djgpp.cmake
```

#### Besonderheiten von `dos32`

- **Threads:** DOS hat keine. Die GHAL bringt einen kleinen eigenen Scheduler mit, präemptiv über den Timer-Interrupt. Das ist der einzige Host, auf dem die GHAL mehr ist als eine dünne Hülle.
- **DOS ist nicht reentrant:** Dateizugriffe aus mehreren GEOS-Threads müssen in der GHAL serialisiert werden (eine DOS-Sperre, wie im alten GEOS-Kernel).
- **Nur i686.** Kein 64-Bit-DOS-Ziel, weil der Code 64-Bit-sauber ist, kann es später über Dos64-stub kommen.
- **Lizenz:** HX ist Freeware mit Quellen, DJGPP und Open Watcom haben freie Lizenzen; nichts davon wird in den Apache-2.0-Code gemischt, die GHAL-`dos32`-Teile bleiben eigener Code.

## 5. Abbildung auf GonWare-Mechanismen

| GEOS | GonWare-Gegenstück | Anmerkung |
|---|---|---|
| Geode (`.geo`) mit `.gp`-Exporten und Protokollnummer | **Ring-3-ELF-Shared-Object** (`.DLL`, wie `LIBC.DLL`) | Das Versionsprinzip ist dasselbe wie beim GLM-ABI (Export mit Versionsnummer, Modul nennt die nötige API-Version). GEOS-Protokollnummern lassen sich darauf 1:1 abbilden. **Keine GLMs.** |
| GEOS-Thread mit Event-Queue | Ring-3-pthread (über `LIBC.DLL`, GonWare-Phase 6) | Das Thread-Modell von GEOS bleibt. Prioritäten und Affinität gibt es in GonWare schon (Interface 1.21/1.22) |
| Semaphoren, `ThreadGrabThreadLock` | `pthread_mutex` (rekursiv), `sem_t` | Die rekursive Mutex fehlt im Ring-0-POSIX-Layer noch (`PTHREAD_MUTEX_NORMAL`-only), wird aber für Ring 3 ohnehin gebraucht |
| Ganzes GEOS-System | **ein** Ring-3-Prozess (Session) | GEOS kennt intern keinen Speicherschutz. Ein Absturz trifft die Session, nicht den Server |
| Video-Treiber-Strategie (`DR_VID_*`) + `VidCom` | **GWVIDEO Tier 1** (Modi, linearer Framebuffer, 2D-Primitive) | Die Operationen sind nach dem GEOS/VidCom-Modell entworfen und passen deshalb fast 1:1. Der Zugang aus Ring 3 ist noch offen (Abschnitt 7) |
| Sound-Treiber (`Driver/Sound`) | GWSOUND über die OSS-API | `/dev/dsp` + `SNDCTL_*` aus Ring 3 |
| `IM` (Input Manager) | openinput-Eventstrom an den Ring-3-Prozess | HID-Usages → GEOS `KeyboardEvent`/`PtrEvent`. Die Tastaturlayouts kommen aus openinput statt aus `Driver/Keyboard/*` |
| GCN, Clipboard, Drag & Drop | bleiben GEOS-intern | Die Session ist ein Prozess, eine Verbindung zum Host ist erst später nötig |
| `.ini` | Datei im GonWare-Dateisystem, später eventuell GDS-Attribute pro Benutzer | |

## 6. Technische Kernpunkte für den nativen Port

Die Punkte gelten für alle drei GonWare-Architekturen.

### 6.1 Speicher- und Handle-Modell

Das ist die „Schicht für das GEOS-Speichermodell“ aus CONCEPT.md. Ohne Kompatibilitätszwang kann es deutlich einfacher werden:

- **Handles: 32 Bit**, Indizes in eine Tabelle. Keine 64-k-Grenze mehr.
- **`optr` = 64 Bit (Handle 32 : Chunk 32).** Das Konzept bleibt (Objekte über IDs ansprechen, Messages über Threads hinweg, Speichern in Dokumenten), nur breiter.
- **Blöcke müssen nicht mehr wandern.** Das Verschieben und Komprimieren des Heaps war eine Folge von 640 KB und Real Mode. Mit virtuellem Speicher kann ein Block an seiner Adresse bleiben. `MemLock`/`MemUnlock` bleiben als API (Quelltextkompatibilität), werden aber billig: Lock liefert den festen Pointer, Unlock zählt nur noch für Debug-Prüfungen. Swapping (`Driver/Swap`) entfällt ganz.
- **LMem-Chunks** dürfen innerhalb ihres Blocks weiter wandern, weil Objektblöcke davon leben (Chunks wachsen, schrumpfen). ChunkHandles werden 32-Bit-Indizes. Die Regel „Pointer nach `LMemAlloc` neu holen“ bleibt. Alternativ (größerer Bruch): jeder Chunk eine eigene Allokation; dann entfällt die Regel, kostet aber Speicher und Lokalität. **Empfehlung:** beim Block-Modell bleiben.

### 6.2 Typen und Makros sofort

Wie CONCEPT.md verlangt, braucht es ein zentrales Header-Set, etwa `CInclude/geosport.h`:

- `GEOS_FAR`/`_far`/`_pascal` werden zu leeren Makros.
- `SegPtr` und `MemLock` werden zu nativen Pointern.
- `word`/`dword` bleiben feste Breiten, **Adressen nie in `word`/`dword`**. Das passt zur GonWare-Regel „`uintptr_t` statt fester Größen“.

### 6.3 Datenformate auf Platte

Die alten Formate (gepackte 16-Bit-Strukturen) werden **nicht** übernommen. Neue Formate:

- **VM-Dateien (Dokumente):** neues Format mit festen Feldbreiten (32/64 Bit, little-endian) und Versionskopf. Es muss zwischen i686, x86-64 und ARM64 austauschbar sein, deshalb **trotzdem** Trennung zwischen Speicherstruktur und Plattenformat über Accessoren (`memcpy`/`le32toh`). Kein direktes Casten auf Plattenstrukturen, auch wegen Alignment und des schwächeren Speichermodells auf ARM64 (GonWare-Regel „saubere C11-Atomics und Barrieren“).
- **Fonts:** nur noch TrueType/OpenType über FreeType. Die GEOS-eigenen Formate (Nimbus, Bitstream, Bitmap-Fonts in `FontData/`) und `Driver/Font/Nimbus`, `Bitstream` entfallen.
- **Grafik-Metafiles (GStrings):** neues, breiteres Format. Koordinaten dürfen größer als 16 Bit werden (GEOS-Grafik ist heute auf ±32 k bzw. `WWFixed` begrenzt; für große Bildschirme und Dokumente lohnt sich hier der Bruch).
- **Konfiguration:** `.ini`-Syntax kann bleiben, die Schlüssel werden neu geordnet. Pro Benutzer im Benutzerverzeichnis, später eventuell GDS-Attribute.
- **Text:** intern Unicode statt SBCS/DBCS (siehe 6.7).

### 6.4 Fließkomma

Ein Befund aus dem Piloten:

- Auf x86-64 und i686 ist `long double` das x87-Extended-Format und damit bitgleich zu GEOS-80.
- Auf **ARM64 ist `long double` ein 128-Bit-Quad** (in Software). Der `memcpy`-Trick aus `fpstack.c` funktioniert dort nicht.

Weil keine Bitgleichheit zum alten GEOS mehr nötig ist, gibt es zwei saubere Wege:

| Variante | Vorteil | Nachteil |
|---|---|---|
| **`double` (IEEE binary64)** überall | auf allen drei Architekturen in Hardware, schnell, einfach; Ergebnisse überall gleich | 15–17 statt 18–19 signifikanter Stellen |
| SoftFloat `extF80` | gleiche Genauigkeit wie GEOS-80 | langsamer, mehr Code |

**Empfehlung: `double`.** Die GEOS-Formatierung zeigt ohnehin nur `DECIMAL_PRECISION` = **15** Stellen an (`CInclude/math.h`). Die zusätzliche Genauigkeit von GEOS-80 war also nach außen nie sichtbar. Der Float-*Stack* als API (Forth-artig, `FloatPush`/`FloatAdd` …) kann für die Quelltextkompatibilität von Spreadsheet/Parse/Chart bleiben, rechnet intern aber mit `double`.

### 6.5 Messages

- Statt der Register-ABI (`ax`/`cx`/`dx`/`bp`, Carry) bekommen Handler eine C-Signatur, der Rückgabewert ersetzt den Carry.
- Der Dispatch über Klassentabellen bleibt.
- Message-Nummern: 32 statt 16 Bit (Klassen-Basis + Offset), damit die heutige Vergabe über Klassenebenen nicht mehr eng wird.
- Asm-Sonderwege (Handler, die Register durchreichen, `@callsuper` mit manipulierten Registern) entfallen; die GOC-Seite ist davon kaum betroffen, weil sie schon heute C-Signaturen benutzt.

### 6.6 Toolchain

GonWare nutzt Clang/lld, und `tools/gdos-cc.sh` gibt es schon.

| Werkzeug | Änderung |
|---|---|
| `goc` | neues Backend neben `COM_WATCOM`/Borland/MSC/HighC (`Tools/goc/symbol.c`), Ziel Clang |
| `uic` | Ausgabe als C-Daten statt asm-`.rdef` (betrifft ~460 k Zeilen `.ui`) |
| `esp`, `glue` | entfallen |
| `swat` | wird ersetzt durch gdb, unter GonWare über den GDB-Stub, unter Linux nativ |

### 6.7 Text und Zeichensatz

- **Intern Unicode.** SBCS mit Code-Pages und die DBCS-Varianten fallen weg.
- Die DBCS-Zweige im Quelltext (`*DBCS`-Verzeichnisse, `#ifdef DO_DBCS`) zeigen, wo heute 16-Bit-Zeichen verarbeitet werden. Sie sind die beste Vorlage für die Umstellung.
- Wahl zwischen **UTF-16** (nah an DBCS, wenig Umbau) und **UTF-32** (einfaches Indexieren, mehr Speicher); UTF-8 nur an den Rändern (Dateinamen, Netz, Import/Export).

### 6.8 Was sonst wegfallen kann

Mit dem Bruch lassen sich Altlasten streichen, statt sie mitzuportieren:

- Swapping (`Driver/Swap`), EMS/XMS, Task-Switcher (`Driver/Task`), DOS-Dateisystem-Treiber.
- Alle Video-Treiber außer dem einen Framebuffer-Treiber; alle Druckertreiber außer der Ausgabe an PAPPL/CUPS.
- Mehrere SpecUI-Varianten: zunächst nur **Motif** (bzw. CommonUI + ein Look); ISUI, NewUI, PM, BBoxUI später oder gar nicht.
- Hardware-spezifische Teile (Pen-Mäuse, Zoomer/Palmtop-Sonderfälle, `GDI`-Library für Win32-Produkte).

## 7. Was GonWare für GonDesk liefern muss

Diese Punkte kommen in PLAN.md noch nicht explizit vor:

1. **GWVIDEO für Ring 3:** Die Treiberseite ist geplant (GWVIDEO, siehe 4.4). Es fehlt noch der **Zugang aus Ring 3**: Modus setzen, den linearen Framebuffer (oder eine Offscreen-Fläche plus Flush) in den Prozess mappen und eventuell die Tier-1-Primitive per Aufruf nutzen. Dazu eine Übergabe der Anzeige zwischen Kernel-Konsole (`fbcon`) und Ring-3-Programm, wie der Bildschirm-Wechsler mit Ctrl+Esc. „LVGL on the framebuffer“ (Phase 6) braucht dieselbe Schnittstelle, man kann sie also gemeinsam definieren.
2. **Eingabe-Events für Ring 3:** den openinput-Strom (`{page, usage, value, source}`) an den Vordergrund-Prozess weiterleiten.
3. **Ring-3-pthreads mit rekursiver Mutex und Timed-Waits** (Phase 6, „user libc must grow pthreads“).
4. **Ring-3-ELF-Loader mit Shared Libraries** (Phase 6). GEOS lädt Libraries und Apps dynamisch, also braucht es `dlopen`.
5. **xattrs bzw. NSS-Name-Spaces aus Ring 3** für die GEOS-Dateiattribute.
6. **Sound:** GWSOUND mit OSS-API ist geplant. Für GEOS reicht `/dev/dsp` aus Ring 3. Nicht dringend, GEOS kommt auch ohne Sound aus.
7. **Grafikbeschleunigung:** nicht nötig. GEOS zeichnet in Software und ist für 2D gebaut. GWVIDEO Tier 2 (3D) betrifft GonDesk nicht.

Daraus ergibt sich die Abhängigkeit: **Ein nativer GonDesk auf GonWare ist erst nach GonWare-Phase 6 möglich.**

## 8. Lizenzen

| Teil | Lizenz | Beziehung |
|---|---|---|
| GonWare-Kernel, GLMs | GPLv2-only | getrennt durch die Ring-3-Grenze (Syscalls) |
| OpenGEOS (`pcgeos`) | Apache 2.0 | eigenständiges Programm |
| mlibc (`LIBC.DLL`) | MIT | darf gegen Apache 2.0 gelinkt werden |
| FreeType | FTL (BSD-artig) oder GPLv2 | **FTL wählen** |
| SoftFloat | BSD-3 | unkritisch |
| SDL3 (Linux-Entwicklungs-Backend) | zlib | unkritisch |

**Wichtig:** Apache 2.0 ist mit GPLv2-only **nicht** kompatibel, nur mit GPLv3. GEOS-Code darf also weder in einen GLM noch in eine GPLv2-Bibliothek gelinkt werden, und umgekehrt darf kein GPLv2-Code in den Apache-2.0-Teil. CONCEPT.md legt das richtig fest („not a GLM in the kernel address space“). Für die **Host-Schicht** heißt das: Die GonWare-Seite (Syscalls) bleibt GPL im Kernel, die GEOS-Seite (Backend in Ring 3) ist Apache 2.0 und spricht nur über Syscalls bzw. mlibc mit dem Kernel.

## 9. Phasen

Abgestimmt auf PLAN.md:

| GEOS-Phase | Inhalt | braucht von GonWare |
|---|---|---|
| **N0** Toolchain + GHAL-Gerüst | `goc`-Backend für Clang/GCC, `uic` → C, `geosport.h`, CInclude 64-Bit-sauber; CMake mit `GEOS_HOST`, GHAL-Header, Host `linux` (sdl3) als erster | nichts |
| **N1** Laufzeitkern nativ unter **Linux** | Heap/LMem/VMem/Object/Geodes/Thread/File (~135 k) | nichts |
| **N2** Grafik unter Linux | Graphics/Win/IM + FreeType; `ghal_display` für sdl3, dann fbdev und x11 | nichts |
| **N3** UI unter Linux | User + CommonUI + Motif → erster nativer GEOS-Desktop, `Appl/SDK_C` läuft | nichts |
| **N4** Host `gonware` | GHAL für GonWare: mlibc, GWVIDEO, openinput, NSS-Attribute | Phase 6 vollständig |
| **N4b** Host `dos32` | GHAL für DOS/DPMI: Scheduler, VBE, int 21h, PS/2 bzw. openinput | nichts (jederzeit nach N3 möglich) |
| **N5** Text, GOC-Apps | Library/Text, Neubau der Breadbox-Apps usw. | – |
| **N6** Komponenten und asm-Apps | GrObj, Spreadsheet, Chart ... FileMgr, GeoWrite | – |
| **N7** NetWare/GDS-Integration | NSS-Volumes, GDS-Login, Drucken über PAPPL | Phase 5/7 |

**Die Arbeit lässt sich parallelisieren:** N0 bis N3 hängen nicht von GonWare ab und können sofort laufen, während GonWare die Phasen 2 bis 6 durchläuft. Bei GonWare ist im Moment Phase 2 erreicht (SMP, Scheduler), Ring 3 kommt in Phase 6.

**Größenordnung** (wie für die anderen Varianten genannt):

- Bis N3, dem sichtbaren nativen Desktop: ~0,75 Mio. asm-Zeilen zu übersetzen, also mehrere Personenjahre.
- Bis N6 kommt man auf 10+ Personenjahre.
- ARM64 und i686 kosten **kaum Zusatzaufwand**, wenn 6.2 bis 6.4 von Anfang an eingehalten werden.

## 10. Offene Entscheidungen

1. **Wie weit Quelltextkompatibilität?** Vorschlag: GOC/C-API möglichst stabil halten; Brüche nur mit Umstellungsskript.
2. **LMem:** Block-Modell mit wandernden Chunks behalten (Empfehlung) oder Einzelallokationen?
3. **Unicode:** UTF-16 oder UTF-32 intern?
4. **Float:** `double` (Empfehlung) oder `extF80`?
5. **Import alter Dokumente:** ganz weglassen oder später als eigenes Werkzeug?
6. **Multi-Session:** Eine GEOS-Session pro GDS-Benutzer, etwa über Remote-Zugang? Bei einem Server-Betriebssystem liegt das nahe.
7. **Erste Ziel-Hardware für GonDesk:** QEMU, ein PC mit GOP oder ein ARM64-Board (Raspberry Pi über Device Tree).

## 11. Bezug zum Pilot `Port32/Math`

- Die Methode ist bestätigt: Struktur 1:1 übernehmen und Hilfsroutinen durch einen Shim ersetzen. Für den ersten Durchgang bleibt das sinnvoll, weil man so gegen das Original prüfen kann.
- **Mit dem Kompatibilitätsbruch ändern sich drei Dinge:**
  - `fpstack.c` rechnet mit `double` statt mit dem x87-`memcpy`-Trick (6.4). Damit läuft er auch auf ARM64.
  - Die bewusst erhaltenen **QUIRKs** (Abschneiden nach dem Runden, Flag-Überschreiben beim Fallback auf wissenschaftliche Notation, negativer Exponent bei `DONT_USE_SCIENTIFIC`) dürfen jetzt behoben werden.
  - Die ABI-Frage der Puffer-Offsets (`FFA_decimalPoint == 0` mehrdeutig) wird frei gelöst, z. B. mit Pointern oder `-1` für „nicht vorhanden“.
- Der Pilot ist C11 mit `_Thread_local` und baut damit auch mit Clang für GonWare/mlibc, sobald Ring-3-TLS existiert.
