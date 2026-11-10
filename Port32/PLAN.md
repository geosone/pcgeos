# Port32: Arbeitsplan

**Stand:** 2026-11-10. Nur Planung, keine Implementierung.

**Gehört zu:** `Port32/GHAL-Konzept.md` (Architektur). Der Pilot liegt in `Port32/Math/`. `Port32/GonDesk-GEOS-auf-GonWare.md` beschreibt einen späteren Host.

**Ziel:** GEOS wird von festverdrahtetem 16-Bit-Assembler gelöst und in C neu aufgebaut. Darunter liegt eine GEOS Hardware Abstraction Layer (GHAL). Die Hosts kommen in dieser Reihenfolge:

1. `dos32` (DOS muss die ganze Zeit laufen)
2. `linux` (erste Erweiterung)
3. `win32`/`win64`
4. später `gonware`

Die Architekturen sind amd64, i386, arm64 und armhf.

---

## 1. Lage upstream (bluewaysw/pcgeos, Issues und PRs gesichtet am 2026-11-10)

Upstream hat 450 Issues (199 offen) und 811 PRs (723 gemerged, 19 offen). Am aktivsten seit 2025 sind bluewaysw, HubertHuckevoll, nschulux, jirkakunze, mgroeber9110 und rabe-soft.

### 1.1 Was für den Port direkt zählt

| Thema | Issue/PR | Stand | Bedeutung für uns |
|---|---|---|---|
| **Protected Mode** | #665, Branch **`665-protected-mode-2026`** | offen, aktiv (letzter Commit 2026-11-08 `56e18c03`), 32 Commits vor `master`; **356 Dateien, +58 121 / −35 718 Zeilen**, davon Kernel + `Loader32` 81 Dateien (+29 137 / −14 409) | „GEOS32“: GEOS läuft in **16-Bit-Protected-Mode** unter **HDPMI16** (Selektoren statt Segmente), mit neuem **`Loader32`** und **GPMI** (GEOS Protected Mode Interface, Vektortabelle über DPMI: Blöcke, Aliase, Deskriptoren, Interrupt- und Exception-Handler, DMA-Puffer …). Dazu kommen ein `swat`-`Stub32` und Anpassungen in Kernel, User, SpecUI, Text, Spool und Video-Treibern. Präemptives Multitasking läuft seit 2026-11-05 wieder. Ziel ist „6.1 release in the long run“. |
| Fehlerberichte im Loader32 | #1210 | offen | Der Maintainer will **`swat` als zentrales Debug-Werkzeug behalten**. Das ist wichtig für unseren Werkzeug-Plan. |
| Host-Schnittstelle (Basebox) | #829 (erledigt), #149 / PR #176, #1050, PR #1246 (offen, auf dem PM-Branch) | aktiv | `Library/HostIf` + `Driver/Socket/HstTCPIP` + SSL über den Host. Auf dem PM-Branch gibt es neu **`Driver/IFS/HostFS`**, ein Dateisystem des Hosts über Basebox. **Das ist bereits das Muster „Host-Dienste + generische Treiber“ aus dem GHAL-Konzept.** |
| 16-Bit-Variante bleibt | #672, #960 | offen | 8086/8088/V20/286 sollen als eigene Build-Variante bleiben. In #960 heißt es dazu: „requires some concept how the configuration should happen“. **Dort fehlt also ein Konzept für Build-Varianten. Die GHAL-Build-Konfiguration kann das liefern.** |
| Tests auf dem Entwicklungsrechner | #1239 | offen | Gewünscht: Tests mit Standardort, Aufruf, Berichten, „documented API for … substituting GEOS services“. **Das deckt sich genau mit unserem `linux`-Test-Host und dem Math-Pilot.** |
| Toolchain | #983 (neueres Open Watcom), #117/#120/#55 (Werkzeuge unter Linux-64), #72 (ia16-gcc), #28/#162/#393/#1147 (Watcom/glue) | teils offen | Die Toolchain ist ein altes OW-Snapshot von 2020 und wackelig. Für den C-Port brauchen wir **parallel** GCC/Clang, ohne die bestehende OW-Kette zu stören. |
| XMS-Swap-Überlauf | #1209 | offen | ein Beispiel für 16-Bit-Grenzen im Swap-Treiber. Mit 32-Bit-DPMI entfällt die Klasse. |
| CGA behalten | #456 | offen | Gehört zur 16-Bit-Variante, nicht in den Port. |

### 1.2 Folgerungen

1. **Upstream hat sich schon für Protected Mode entschieden**, aber zunächst für 16-Bit-PM. Unser Plan muss **auf diesem Branch aufbauen**, nicht daneben. GPMI ist bereits eine kleine Abstraktion für Speicher, Deskriptoren und Interrupts. Sie wird der DOS-Teil von `ghal_mem`/`ghal_irq`, wir erfinden ihn nicht neu.
2. **Upstream hat drei Varianten**, und die GHAL-Build-Konfiguration muss alle tragen:
   - **real16**: heutiges GEOS, 8086 bis 286, #672
   - **pm16**: GEOS32 aus #665, HDPMI16
   - **c32/c64**: unser Port, `dos32`/`linux`/`win`/`gonware`
3. **HostIf/HostFS** zeigen: Host-Dienste mit generischen Treibern sind upstream akzeptiert. Die GHAL-Host-Dienste setzen das fort, ohne Basebox-Bindung.
4. **`swat` bleibt** für die DOS-Varianten (Maintainer-Wunsch). Für `linux`/`win` kommt gdb dazu. Langfristig ein `swat`-Stub auf GHAL-Basis prüfen.
5. **Die Tests auf dem Entwicklungsrechner (#1239)** sind der natürliche erste Beitrag nach upstream.
6. Der alte Mirror `gitea.disconnected-by-peer.at/bluewaysw/pcgeos` hat den PM-Branch nicht. **Der neue vollständige Mirror `gitea.disconnected-by-peer.at/dbp/pcgeos` hat ihn** (inkl. `665-protected-mode`, PR-Refs). Er ist seit 2026-11-10 als Remote `dbp` im Klon eingetragen; `dbp/665-protected-mode-2026` ist geholt.

---

## 2. Phasen

Jede Phase endet mit einem prüfbaren Ergebnis. **Nach jeder Phase läuft GEOS unter DOS.** Upstream-Bezug steht jeweils dabei.

**Arbeitsweise (Entscheidung 2026-11-10):**
- Gearbeitet wird **nur im Fork `github.com/geosone/pcgeos`**, Branch `ghal` (auf `665-protected-mode-2026`).
- Es gibt **keine Pull Requests zu upstream**. **Ein Issue** bei upstream wird erst erstellt, **sobald etwas lauffähig ist** (frühestens nach P5, z. B. GEOS-Kern in C unter DOS/Linux). Es weist auf den Fork hin, damit upstream sich das ansehen kann. Vorher keine Issues.
- Upstream wird nur **beobachtet und eingeholt**: Neue Commits auf `master` und dem PM-Branch werden regelmäßig in `ghal` übernommen (Merge oder Rebase).
- „Upstream-Bezug“ heißt im Folgenden nur, an welches upstream-Thema eine Phase anknüpft.

### P0: Grundlagen

- ~~PM-Branch verfügbar machen, Fork einrichten~~ **erledigt 2026-11-10**: Remote `geosone` (SSH, weil das globale `insteadOf` `https://github.com` auf Gitea umleitet), PM-Branch in den Fork gepusht, Arbeitsbranch `ghal`.
- Ablauf zum Einholen von upstream festlegen: Wie oft, Merge oder Rebase, wer löst Konflikte im PM-Branch.
- Offene Fragen aus dem GHAL-Konzept (9.) selbst entscheiden: Verhältnis zu GPMI/HostIf/HostFS, Build-Varianten (#960), Tests (#1239), `swat`.
- Konventionen festlegen: C-Standard (C11), Formatierung, Präfixe (`ghal_`), Lizenzkopf Apache 2.0, Sprache der Doku.

**Ergebnis:** Fork und Branch stehen; das Einholen von upstream ist geregelt; die offenen Fragen aus dem GHAL-Konzept (9.) sind beantwortet.

### P1: Tests auf dem Entwicklungsrechner (knüpft an upstream #1239 an)

- Ort, Aufruf und Berichtsformat für Tests auf dem Entwicklungsrechner festlegen (z. B. `Tests/Host/`, `make host-tests` bzw. CTest).
- Minimaler `linux`-GHAL-Kern **ohne Grafik**: `ghal_mem`, `ghal_time`, `ghal_log`, Stub-Dienste.
- **Math-Pilot** (`Port32/Math`) als erster Testfall einbringen. Dafür `fpstack.c` auf `double` umstellen.
- CI auf linux-amd64, i386, arm64 und armhf (ARM über QEMU-User).

**Ergebnis:** Test-Infrastruktur im Fork; der Pilot läuft auf vier Architekturen.

### P2: GHAL im bestehenden Assembler einziehen (Schritt S0)

Die Arbeit läuft auf Basis des PM-Branches.

- **Inventar per Skript** (wiederholbar, in der CI): alle `int 21h/10h/15h/16h/2Fh/67h`, Port-I/O, `SysCatch*Interrupt`, `SysLockBIOS`, `INT_OFF`/`cli` in Kernel und Loader/Loader32. Die Zahlen aus GHAL-Konzept 2.2 sind die Ausgangsbasis.
- GHAL-Einsprungpunkte und -Makros in asm (`GHAL_CRIT_ENTER`, `GHALDosCall`, `GHALCatchIrq` …). **real16** und **pm16** setzen sie auf das bisherige Verhalten bzw. auf GPMI um.
- Kernel (`Sys`, `Timer`, `FSD`, `File`, `Boot`, `Thread`) und `Loader32` darauf umstellen.
- **Abnahme:** Die real16-Variante baut **bytegleich** wie vorher, oder mit dokumentierten Unterschieden. pm16 bootet wie vorher bis zum Desk.
- Build-Varianten-Konzept für #960 (V20/186/286-Optimierungen) mitliefern, auf denselben Mechanismen.

**Ergebnis:** Alle Host-Kontakte von Kernel und Loader liegen an einer Stelle und haben Namen. Commits in kleinen Paketen pro Kernel-Modul, damit das Einholen von upstream beherrschbar bleibt.

### P3: C-Toolchain neben Open Watcom

- `goc`-Backend für GCC/Clang (neben `COM_WATCOM` usw. in `Tools/goc`).
- `uic` → C-Daten statt `.rdef`.
- `geosport.h`: Typen, `_far`/`_pascal` leer, 64-Bit-saubere `CInclude/`. Zuerst als Prüfung: CInclude kompiliert mit `-m64 -Wall`.
- **Geode-Format:** ELF-Shared-Object und ein Ladeformat aus `.gp` (Exporte, Protokollnummern). Spezifikation + Prototyp-Lader als Test auf dem Entwicklungsrechner.
- Build-System: CMake für die C-Ziele, gemeinsam mit pmake für die asm-Varianten (pmake bleibt, solange upstream es nutzt).

**Ergebnis:** Eine GOC-Beispiel-App (`Appl/SDK_C/*`) kompiliert für `linux` (ohne zu laufen); ELF-Geodes laden im Test.

### P4: Laufzeitkern in C

Reihenfolge nach Abhängigkeit, jeweils mit Tests auf dem Entwicklungsrechner unter `linux`:

1. Heap (32-Bit-Handles, Blöcke wandern nicht), LMem (Chunks bleiben), Initfile, Local
2. Thread/Sem/Timer auf `ghal_ctx`/`ghal_time` (GEOS-Scheduler + Kernel-Sperre)
3. Object (Message-Dispatch mit C-Signaturen, 32-Bit-Message-Nummern), Geodes (ELF-Lader aus P3)
4. VMem (neues Dateiformat), File-API, FSD + HostFS-Treiber auf `ghal_fs`

**Ergebnis:** Ein GEOS-Kernel ohne Grafik läuft unter `linux` als Prozess, lädt Geodes, sendet Messages und liest und schreibt VM-Dateien. Alles ist durch Tests abgedeckt.

### P5: Host `dos32`

- GHAL-Kern `dos32` (HDPMI32/HX): Speicher, Scheduler mit Kontextwechsel, IRQ-Stubs, DOS-Sperre. Dabei die Struktur aus GPMI/Loader32 übernehmen.
- Treiber nach C/32-Bit:
  - Video: VESA-LFB aus `VGAlike`/`VidCom`
  - Tastatur, Maus (PS/2, seriell)
  - IFS/DOS mit LFN
  - HostFS für Basebox
- Start, `geos.ini`, Systemverzeichnis.

**Ergebnis:** Der Kern aus P4 läuft unter DOS (FreeDOS + HDPMI32, QEMU und echte Hardware) mit einer Testanzeige. **Ab hier ist DOS auch im C-Zweig die Referenzplattform.**

**Meilenstein „vorzeigbar“:** Ab hier kann das Issue bei upstream erstellt werden (siehe Arbeitsweise), spätestens nach P6 mit dem ersten C-Desktop.

### P6: Grafik, Fenster, UI, Text

- Kernel `Graphics`/`Win`/`IM` (~130 k Zeilen asm) → C. Der generische FB-Videotreiber auf `ghal_display` und der Software-Renderer kommen dazu.
- `Library/User` (~169 k) → C.
- `SpecUI/CommonUI` + Motif (~295 k) → C. Andere SpecUIs später.
- `Library/Text` (~149 k) → C.
- FreeType-Treiber (schon C) anbinden.

**Ergebnis:** **Erster GEOS-Desktop in C, unter `dos32` und `linux` (SDL3).** `Appl/SDK_C`-Beispiele laufen.

### P7: Host `linux` vollständig

- Anzeige: SDL3 → fbdev → X11.
- Eingabe → HID-Usages, HostFS mit xattrs, Sockets (Host-Socket-Treiber nach dem Modell `HstTCPIP`), Audio, Drucken über CUPS.

**Ergebnis:** GEOS als normales Linux-Programm auf amd64, i386, arm64 und armhf.

### P8: Komponenten und Anwendungen

- Komponenten: GrObj, Spreadsheet, Chart, Math (Rest), Parse, Spline, Impex.
- GOC-Apps neu bauen (~0,83 Mio. Zeilen, u. a. Breadbox).
- asm-Apps übersetzen oder ersetzen: FileMgr, GeoWrite, Calendar …

### P9: Host `win32`/`win64`

- GHAL-Kern Windows: Win32-Threads, `VirtualAlloc`, Winsock, Anzeige über DIB oder SDL3, HostFS mit NTFS-Streams.
- Achtung LLP64 (GHAL-Konzept, Build-Regeln).

### P10: Host `gonware`

- Siehe `GonDesk-GEOS-auf-GonWare.md`. Voraussetzung ist Ring 3 in GonWare (dortige Phase 6).

---

## 3. Größenordnung

| Phase | Umfang |
|---|---|
| P0–P3 | Infrastruktur und Toolchain. Monate, nicht Jahre. Das meiste ist schon im 16-Bit-Code nützlich. |
| P4–P6 | ~0,75 Mio. asm-Zeilen bis zum Desktop. Mehrere Personenjahre. |
| P7–P10 | Hosts und Apps. Hosts sind klein, sobald die GHAL steht. Die asm-Apps sind der große Rest (zusammen 10+ Personenjahre bis zum Vollausbau). |

Der Pilot hat für asm-Code zu C ein Verhältnis von ~1,9 : 1 ergeben. Die meiste Zeit kostet das Klären der Semantik, nicht das Tippen.

---

## 4. Risiken

| Risiko | Gegenmaßnahme |
|---|---|
| **Auseinanderlaufen mit upstream** (PM-Branch bewegt sich schnell) | auf dem PM-Branch arbeiten, upstream regelmäßig einholen, kleine Commits, Änderungen an upstream-Dateien möglichst gering halten (neuer Code in eigenen Verzeichnissen) |
| Upstream ändert Dinge, auf denen wir aufbauen (z. B. GPMI, Loader32) | Inventar-Skript (P2) in der CI des Forks, damit neue Host-Kontakte aus upstream sofort auffallen |
| Toolchain (altes OW, Linux-64-Bugs) | C-Port mit GCC/Clang parallel; die OW-Kette bleibt unangetastet |
| `swat` vs. gdb | `swat` für DOS-Varianten behalten; gdb für gehostete Systeme; Symbolformat später angleichen |
| Umfang (3,7 Mio. asm-Zeilen) | Reihenfolge nach Nutzen (Kernel → UI → Text → GOC-Apps); asm-Apps zuletzt oder ersetzen |
| Semantik-Fehler beim Übersetzen | jeder portierte Teil mit Tests auf dem Entwicklungsrechner, Vergleich gegen das 16-Bit-Original, wo es Sinn ergibt (wie im Math-Pilot) |
| Lizenz | alles Apache 2.0; keine GPL-Teile in den GEOS-Code (relevant z. B. bei HX-Teilen und Hilfsbibliotheken) |

---

## 5. Nächste konkrete Schritte

1. ~~PM-Branch holen~~ erledigt (Remote `dbp`). Nächster Teil davon: den PM-Branch durchsehen (GPMI, `Loader32`, Kernel-Änderungen, HostFS) und das Ergebnis hier eintragen.
2. Inventar-Skript für P2 schreiben und gegen `master` und den PM-Branch laufen lassen.
3. Ablauf zum Einholen von upstream festlegen (P0).
4. Den Math-Pilot an #1239 ausrichten (Ort, Aufruf, `double`), als Vorschlag für den ersten PR.

---

## Anhang: Gesichtete Issues/PRs mit Bezug zum Port

**Offen:**

- #665 (Protected Mode)
- #1210 (Loader32-Diagnose)
- #672, #960 (16-Bit-Variante)
- #1239 (Tests auf dem Entwicklungsrechner)
- #983 (neueres OW)
- #117, #55, #85, #16 (Build unter Linux)
- #72 (ia16-gcc)
- #149/PR #176, #1050, PR #1246 (Host-TCP/HostIf)
- #1209 (XMS-Swap)
- #456 (CGA)
- #28, #162, #393, #168, #169, #177, #453, #1089 (glue/esp/Watcom)
- #224 (OW-Debuginfo)
- #421, #573, #306, #1074 (swat)

**Erledigt, als Hintergrund:**

- #829 (Basebox-Schnittstelle)
- #89/#90 (swat unter Windows/Linux)
- #91/#104/#126 (swat über TCP/IPX/seriell)
- PR #51 (Toolchain unter Linux)
- PR #686/#688 (HostIf-Library)
- PR #1226 (Handle-Leck, PM-Branch)
- PR #1140 (DOSBox-Erkennung im Kernel)
