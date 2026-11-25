/*
 * soundDrv.h -- the functions of a sound driver (the C form of
 * Include/Internal/soundDrv.def), for the sound library (Library/Sound)
 * and the sound drivers in C (Driver/Sound/Host).
 *
 * A sound driver is a DRIVER_TYPE_SOUND driver with a strategy routine
 * of geosdriver.h; the registers are those of soundDrv.def:
 *
 *   DRE_SOUND_QUERY_DEVICE_CAPABILITY
 *	out: DR_ax FM voices, DR_dx DACs, DR_bx SupportedInstrumentFormat,
 *	     DR_cx SoundDriverCapability, DR_di SoundDriverDACCapability,
 *	     DR_farPtr the stream strategy to use (NULL: the stream driver's)
 *   DRE_SOUND_VOICE_ON		DR_ax frequency (Hz), DR_bx volume,
 *				DR_cx voice
 *   DRE_SOUND_VOICE_OFF	DR_cx voice (released, the envelope goes on)
 *   DRE_SOUND_VOICE_SILENCE	DR_cx voice (silent at once)
 *   DRE_SOUND_SET_ENVELOPE	DR_cx voice, DR_bx:DR_si the instrument:
 *				table (IT_STANDARD_TABLE) and patch
 *   DRE_SOUND_DAC_ATTACH_TO_STREAM	DR_cx DAC, DR_ax and DR_bx the
 *				stream token; carry: error
 *   DRE_SOUND_DAC_DETTACH_FROM_STREAM	DR_cx DAC
 *   DRE_SOUND_DAC_SET_SAMPLE_RATE_AND_FORMAT,
 *   DRE_SOUND_DAC_CHECK_SAMPLE_RATE_AND_FORMAT
 *	in: DR_cx DAC, DR_ax ManufacturerID, DR_bx SampleFormat, DR_dx rate
 *	out: DR_dx rate (0: not supported), DR_cx stream size for the rate
 *   DRE_SOUND_DAC_RESET_REFERENCE_BYTE, DRE_SOUND_DAC_FLUSH_DAC
 *				DR_cx DAC
 *   DRE_SOUND_DAC_IS_DAC_EMPTY	DR_cx DAC; carry: not empty
 *   DRE_SOUND_MIXER_*	al/ah: DR_ax low and high byte (MixerLevel of
 *			the left and right channel), as soundDrv.def says
 *
 * Port32: the FM functions may be called from interrupt code (the
 * sound library's timer routine) inside ghal_crit; the driver must not
 * block or allocate there.
 */
#ifndef SOUNDDRV_H
#define SOUNDDRV_H

#include <driver.h>
#include <geosdriver.h>

#define SOUND_PROTO_MAJOR	(DRIVER_PROTO_MAJOR + 1)
#define SOUND_PROTO_MINOR	(DRIVER_PROTO_MINOR + 0)

/* SoundDriverFunction (DriverExtendedFunction + 2 ...) */
#define DRE_SOUND_QUERY_DEVICE_CAPABILITY		12
#define DRE_SOUND_VOICE_ON				14
#define DRE_SOUND_VOICE_OFF				16
#define DRE_SOUND_VOICE_SILENCE				18
#define DRE_SOUND_SET_ENVELOPE				20
#define DRE_SOUND_DAC_ATTACH_TO_STREAM			22
#define DRE_SOUND_DAC_DETTACH_FROM_STREAM		24
#define DRE_SOUND_DAC_SET_SAMPLE_RATE_AND_FORMAT	26
#define DRE_SOUND_DAC_CHECK_SAMPLE_RATE_AND_FORMAT	28
#define DRE_SOUND_DAC_RESET_REFERENCE_BYTE		30
#define DRE_SOUND_DAC_FLUSH_DAC				32
#define DRE_SOUND_DAC_IS_DAC_EMPTY			34
#define DRE_SOUND_MIXER_GET_MASTER_VOLUME		36
#define DRE_SOUND_MIXER_SET_MASTER_VOLUME		38
#define DRE_SOUND_MIXER_GET_CAPABILITIES		40
#define DRE_SOUND_MIXER_MAP_SOURCE			42
#define DRE_SOUND_MIXER_MAP_EFFECT			44
#define DRE_SOUND_MIXER_GET_SOURCE_VOLUME		46
#define DRE_SOUND_MIXER_SET_SOURCE_VOLUME		48
#define DRE_SOUND_MIXER_GET_EFFECT_LEVEL		50
#define DRE_SOUND_MIXER_SET_EFFECT_LEVEL		52

/* SoundWordOfData (the word after the driver's info, for preferences) */
#define SWOD_USES_TIMER_2	0x8000
#define SWOD_IS_SYNTH_DEVICE	0x4000
#define SWOD_IS_SAMPLE_DEVICE	0x2000

/* SoundDriverDACCapability */
typedef WordFlags SoundDriverDACCapability;
#define SDDACC_DMA		0x8000
#define SDDACC_INT		0x4000

/* SupportedInstrumentFormat (soundFmt.def; sound.h's
 * SupportedEnvelopeFormat) */
typedef word SupportedInstrumentFormat;

#endif /* SOUNDDRV_H */
