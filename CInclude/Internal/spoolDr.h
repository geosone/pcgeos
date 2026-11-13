/*
 * spoolDr.h -- the spool library's routines for printer drivers
 * (Library/Spool/Lib/libDriver.c): the text modes' fonts and character
 * translation.  Include <Internal/printDr.h> first (it has no include
 * guard, so this header does not include it).
 *
 * Port32 (Port32/ABI.md section 13w): the asm took the PState in es, the
 * device info resource in ds and the driver's info resource handle in
 * dx, and the font in cx/dx/bl; C passes them.
 */
#ifndef SPOOLDR_H
#define SPOOLDR_H

/* The tables of PDI_subISOTable and PDI_asciiTransChars, which were
 * chunks of the driver's info resource. */
typedef struct {
    /* 16 words for each PrinterCountryCode, each (code << 8) | char: char
     * prints as code, and code as a space; 0 ends a country.  NULL, or
     * 0xffff in the first word: none. */
    const word	*STT_subISO;
    /* entries of 4 bytes: a character, the code to print it as, 2
     * unused */
    const byte	*STT_trans;
    word	STT_transSize;		/* in bytes */
} SpoolTransTables;

/* the printer's font closest to fontID / pointSize / pitch (0:
 * proportional), from the FontGeometry table of the device's PrinterInfo
 * (PI_fontGeometries and FG_pitchTab are offsets from the PrinterInfo) */
extern void SpoolMapToPrinterFont(const PrinterInfo *pi, FontID *fontID,
				  word *pointSize, byte *pitch);

/* PS_asciiTrans for PS_curFont's symbol set and the job's country */
extern void SpoolUpdateTranslationTable(PState *ps,
					const SpoolTransTables *tables);

#endif /* SPOOLDR_H */
