# Pilot: `floatFloatToAscii.asm` → C (32/64 Bit)

Messpunkt für die Frage, wie aufwendig eine Übersetzung des PC/GEOS-Assemblers
nach C ist. Übersetzt wurde `Library/Math/Float/floatFloatToAscii.asm` (die
Zahl→Text-Formatierung, die u. a. GeoCalc, Chart und Parse nutzen).

```
make test      # baut für x86-64 und i386 (-m32) und führt beide Tests aus
./test_ffa -v  # alle Testfälle mit Ausgabe
```

## Dateien

| Datei | Inhalt |
|---|---|
| `floatFloatToAscii.c` | Übersetzung aller 21 Routinen (inkl. `FloatFloatToAsciiFixed`, `ConvertDwordToAscii`, `FloatGetNumDigitsInIntegerPart`) |
| `floatFloatToAscii.h` | `FFA_stackFrame` & Konstanten 1:1 aus `CInclude/math.h` |
| `fpstack.c/.h` | Shim für den GEOS-Float-Stack (~30 Routinen aus `floatLow/High/Fixed/Comp.asm`) |
| `test_ffa.c` | 41 Testfälle |

## Ergebnis

| | Assembler | C |
|---|---|---|
| Zeilen gesamt | 2.252 | 603 (+309 Shim) |
| Code ohne Kommentare | ~870 | ~470 (+240 Shim) |

- 41/41 Tests grün auf x86-64 **und** i386.
- Verhältnis Code: ~1,9 : 1 (Gesamtzeilen ~3,7 : 1, die asm-Dateien sind sehr
  kommentarlastig).
- Aufwand: der größte Teil war **nicht** das Übersetzen der Datei selbst,
  sondern das Klären der Semantik der aufgerufenen Hilfsroutinen
  (Rundung in `FloatFloatToDwordNoPop`, `FloatEpsilon`, `FloatTrunc`-Hack,
  Stack-Effekte von `FloatEq0`, Register-/Carry-Konventionen).

## Wichtige Befunde

- **GEOS-80 = x87 Extended** (Bias 0x3fff, explizites Integer-Bit, Inf =
  `7fff:8000…0`). Die Konstanten-Tabelle aus `Float10ToTheX` wird per
  `memcpy` direkt als `long double` verwendet.
  → Der Software-Float-Emulator (~15.000 Zeilen asm) kann auf x87/`long double`
  oder SoftFloat `extF80` abgebildet werden.
  *Achtung:* Open Watcom hat nur 64-Bit `long double`.
- **Struktur blieb erhalten**: Die Funktionen behalten Namen, Reihenfolge und
  den Forth-artigen Stack-Ablauf – damit bleibt der Code gegen das Original
  prüfbar (Review Zeile gegen Zeile).
- **Bewusst erhaltene Eigenheiten (QUIRK im Code markiert)**:
  - Rundung nach `decimalLimit`, danach Abschneiden auf `totalDigits`
    (`123.456`, 5 Stellen, 4 Dez. → `123.45`) – so dokumentiert.
  - Fixed→Scientific-Fallback überschreibt **alle** Format-Flags
    (Kommas/Prozentzeichen gehen dabei verloren).
  - Mit `FFAF_DONT_USE_SCIENTIFIC` bleibt ein negativer Exponent stehen.
  - `+inf` → `"overflow"`, `-inf` → `"underflow"`.
  - `FloatFloatToDword` prüft den Exponenten *vor* dem Runden.

## Offene Punkte

1. **ABI-Entscheidung**: `FFA_startNumber`, `FFA_decimalPoint` usw. waren
   absolute Segment-Offsets (`di`). Jetzt: Offsets relativ zum Puffer.
   `FFA_decimalPoint == 0` heißt weiterhin „kein Dezimalpunkt“ – bei
   `FFAF_NO_LEAD_ZERO` (".25") steht der Punkt aber auf Offset 0 → mehrdeutig.
   Für die echte Portierung: Aufrufer prüfen (GeoCalc, Spreadsheet) und auf
   Pointer oder Offset+1 umstellen.
2. **Referenzvergleich fehlt**: Die erwarteten Strings stammen aus der
   Dokumentation, nicht aus dem 16-Bit-Original. Nächster Schritt: die
   16-Bit-Version (z. B. `Appl/SDK_C/FP2Asc`) im Emulator mit denselben
   Testvektoren laufen lassen und bit-genau vergleichen.
3. `FloatLog` nutzt `log10l` statt der GEOS-Reihenentwicklung; dient nur der
   Exponent-*Schätzung* (Fehler ±1,6 wird vom Algorithmus aufgefangen), kann
   aber an exakten Zehnerpotenzen theoretisch anders verzweigen.
4. Nicht enthalten: DBCS-Variante (mechanisch: `char`→`wchar`),
   Datum/Zeit-Zweig (`FloatFloatToDateTime`, eigene Datei).
