/*
 * ghal_audio_none.c -- ghal_audio on hosts without it (dos32: the sound
 * card's own driver plays there).
 */
#include "ghal.h"

ghal_status ghal_audio_open(unsigned rate, unsigned channels,
                            size_t frames, ghal_audio **out)
{
    (void)rate;
    (void)channels;
    (void)frames;
    if (out)
        *out = NULL;
    return GHAL_ERR_UNSUPPORTED;
}

void ghal_audio_close(ghal_audio *a) { (void)a; }
void ghal_audio_pause(ghal_audio *a, int paused) { (void)a; (void)paused; }
size_t ghal_audio_room(ghal_audio *a) { (void)a; return 0; }

size_t ghal_audio_write(ghal_audio *a, const int16_t *frames, size_t n)
{
    (void)a;
    (void)frames;
    (void)n;
    return 0;
}

uint64_t ghal_audio_played(ghal_audio *a) { (void)a; return 0; }
uint64_t ghal_audio_underruns(ghal_audio *a) { (void)a; return 0; }

void ghal_audio_set_tap(ghal_audio *a, ghal_audio_tap fn, void *ctx)
{
    (void)a;
    (void)fn;
    (void)ctx;
}

const char *ghal_audio_backend(ghal_audio *a)
{
    (void)a;
    return "none";
}
