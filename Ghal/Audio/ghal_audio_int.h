/*
 * ghal_audio_int.h -- between ghal_audio.c (the ring, the interrupt, the
 * null backend) and the backends of ghal_audio.
 */
#ifndef GHAL_AUDIO_INT_H
#define GHAL_AUDIO_INT_H

#include "ghal.h"

typedef struct ghal_audio_be {
    const char *name;
    /* the device opened for a->rate and a->channels, paused */
    ghal_status (*open)(ghal_audio *a, void **data);
    void        (*close)(ghal_audio *a, void *data);
    void        (*pause)(ghal_audio *a, void *data, int paused);
} ghal_audio_be;

/* For a backend, on its own thread: n frames out of the ring into dst
 * (silence for those missing), the tap told, the interrupt raised if the
 * ring is half empty. */
void ghal_audio_pull(ghal_audio *a, int16_t *dst, size_t n);
unsigned ghal_audio_rate_of(const ghal_audio *a);
unsigned ghal_audio_channels_of(const ghal_audio *a);

#ifdef GHAL_HAVE_PIPEWIRE
extern const ghal_audio_be ghal_audio_pipewire;
#endif

#endif /* GHAL_AUDIO_INT_H */
