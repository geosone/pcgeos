/*
 * soundCtl.h -- the control block of a sound (Port32; the C form of the
 * SoundControl of Include/sound.def), for the sound library
 * (Library/Sound) and the UI, which frees the blocks of finished sounds
 * (MSG_USER_FREE_SOUND_HANDLE).
 *
 * Every sound is a block: a SoundControl, then for an FM sound one
 * SoundVoiceStatus per voice of the song (SC_voice), and for a note the
 * song itself.  The token of a sound is the handle of the block.
 *
 * Port32: the pointers are pointers (the song, the buffer of a stream
 * write); a stream is its token (Internal/strDrInt.h); the semaphores in
 * the structure are the kernel's (Internal/semInt.h).  SBS_slot is new:
 * a timer routine and a stream notification get a word of data, not a
 * pointer, so the library keeps its sounds in a table and passes the
 * place in it.
 */
#ifndef SOUNDCTL_H
#define SOUNDCTL_H

#include <geos.h>
#include <sound.h>
#include <heap.h>
#include <streamC.h>
#include <Internal/semInt.h>

#define SOUND_ID	22393		/* SBS_ID of a sound block (the
					 * asm's 022393 is decimal) */

/* SoundType */
#define ST_SIMPLE_FM	0
#define ST_STREAM_FM	2
#define ST_SIMPLE_DAC	4
#define ST_STREAM_DAC	6

/* EndOfSongFlags beyond sound.h's: the song is a locked LMem chunk */
#define EOSF_LMEM_UNLOCK	0x0020

#define NO_VOICE	0x00ff		/* SVS_physicalVoice: none */

typedef struct {
    MemHandle		SBS_blockHandle;
    word		SBS_ID;		/* SOUND_ID */
    SemaphoreHandle	SBS_mutExSem;
    word		SBS_type;	/* SoundType */
    word		SBS_priority;	/* SoundPriority */
    EndOfSongFlags	SBS_EOS;
    word		SBS_slot;	/* Port32: the library's table */
} SoundBasicStatus;

typedef struct {
    TimerHandle		SFMS_timerHandle;
    word		SFMS_timerID;
    word		SFMS_timeRemaining;	/* 65535 ms periods left */
    word		SFMS_tempo;		/* ms per 64th note */
    word		SFMS_voicesUsed;
} SoundFMStatus;

typedef struct {
    word		SDACS_rate;
    SampleFormat	SDACS_format;
    word		SDACS_manufactID;
} SoundDACStatus;

typedef union {
    SoundFMStatus	SFS_fm;
    SoundDACStatus	SFS_dac;
} SoundFormatStatus;

/* SoundStreamState (the asm's SSS_active ...; SSS_ACTIVE is input.h's) */
#define SSST_ACTIVE	0x80		/* a reader is on */
#define SSST_DESTROYING	0x40
#define SSST_LOCKED	0x20		/* the reader locked the block */

typedef struct {
    const byte		*SSS_songBuffer;	/* the song */
    const byte		*SSS_songPointer;	/* the next event */
    optr		SSS_songHandle;		/* the song's chunk (LMem) */
} SoundSimpleStatus;

typedef struct {
    StreamToken		SSS_streamToken;
    Semaphore		SSS_dataSem;		/* all data on the stream */
    Semaphore		SSS_activeReaderSem;	/* the stream drained */
    Semaphore		SSS_writerSem;		/* one writer */
    const byte		*SSS_buffer;		/* what is left to write */
    word		SSS_dataRemaining;
    word		SSS_dataOnStream;	/* events (FM) */
    byte		SSS_streamState;
} SoundStreamStatus;

typedef union {
    SoundSimpleStatus	SSS_simple;
    SoundStreamStatus	SSS_stream;
} SoundPositionStatus;

typedef struct {
    word		SVS_physicalVoice;	/* NO_VOICE: none */
    dword		SVS_instrument;		/* table << 16 | patch */
} SoundVoiceStatus;

typedef struct {
    SoundBasicStatus	SC_status;
    SoundFormatStatus	SC_format;
    SoundPositionStatus	SC_position;
    SoundVoiceStatus	SC_voice[1];		/* SFMS_voicesUsed of them */
} SoundControl;

/* the size of a control block with n voices */
#define SOUND_CONTROL_SIZE(n) \
    ((word) (offsetof(SoundControl, SC_voice) + \
	     (n) * sizeof(SoundVoiceStatus)))

#endif /* SOUNDCTL_H */
