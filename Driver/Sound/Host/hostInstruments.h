/*
 * hostInstruments.h -- the instruments of the host sound driver: the
 * Sound Blaster driver's InstrumentTable (soundblasterVoice.asm), made
 * C by sbigen (Tools/sbigen) at build time.  Index: the InstrumentPatch.
 */
#ifndef HOSTINSTRUMENTS_H
#define HOSTINSTRUMENTS_H

/* SBIEnvelopeFormat (Include/Internal/soundFmt.def) */
enum {
    SBI_MOD_TIMBRE, SBI_CAR_TIMBRE,	/* AM VIB EGT KSR MULT:4 */
    SBI_MOD_SCALING, SBI_CAR_SCALING,	/* KSL:2 TL:6 */
    SBI_MOD_ATTACK, SBI_CAR_ATTACK,	/* AR:4 DR:4 */
    SBI_MOD_SUSTAIN, SBI_CAR_SUSTAIN,	/* SL:4 RR:4 */
    SBI_MOD_WAVE, SBI_CAR_WAVE,		/* WS:2 */
    SBI_FEEDBACK,			/* FB:3 CON:1 */
    SBI_SIZE
};

typedef unsigned char HostSBI[SBI_SIZE];

extern const HostSBI hostInstruments[];
extern const unsigned hostInstrumentCount;

#endif
