/*
 * tsnddrv.h -- the sound driver of the sound library's test (tsnddrv.c):
 * four FM voices and a DAC that log what they are told.
 */
#ifndef TSNDDRV_H
#define TSNDDRV_H

#include <geos.h>
#include <heap.h>
#include <streamC.h>

#define TSND_VOICES	4
#define TSND_DACS	1
#define TSND_EVENTS	512
#define TSND_DAC_RATE	8000		/* the only rate, 8-bit PCM only */
#define TSND_DAC_STREAM	64		/* the stream size it asks for */

/* a function of its own: DR_farPtr <- the TsndLog */
#define TSND_GET_LOG	0x7e00

typedef struct {
    word	TE_fn;			/* DRE_SOUND_* */
    word	TE_ax, TE_bx, TE_cx, TE_si;
    dword	TE_tick;
} TsndEvent;

typedef struct {
    word	TL_count;
    TsndEvent	TL_ev[TSND_EVENTS];
    StreamToken	TL_dacStream;		/* attached, 0 if none */
    word	TL_left, TL_right;	/* master volume */
} TsndLog;

#endif
