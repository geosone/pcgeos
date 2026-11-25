/*
 * ghal_audio.c -- PCM output for POSIX hosts (ghal.h, ghal_audio): the
 * ring of frames, the thread that raises GHAL_IRQ_AUDIO, the tap, and
 * the backend "null", which takes the frames in real time and plays them
 * nowhere.  The backends (ghal_audio_pipewire.c) take the frames with
 * ghal_audio_pull on threads of their own.
 *
 * Locks: the ring has a mutex of its own, held only to copy; the backend
 * never takes ghal_crit.  ghal_audio_write is called inside ghal_crit
 * (from the interrupt handlers), so the order is ghal_crit, then the
 * ring's mutex.
 */
#define _GNU_SOURCE 1

#include "ghal.h"
#include "ghal_internal.h"
#include "ghal_audio_int.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct ghal_audio {
    unsigned        rate, channels;
    int16_t        *ring;
    size_t          cap;            /* frames */
    uint64_t        rd, wr;         /* frames taken, frames written */
    uint64_t        underruns;
    int             paused;
    pthread_mutex_t lock;           /* the ring, the tap, the flags */
    ghal_audio_tap  tap;
    void           *tapCtx;
    /* the thread that raises the interrupt */
    pthread_t       raiser;
    pthread_cond_t  wake;
    int             want, stop;
    /* the backend */
    const ghal_audio_be *be;
    void           *beData;
};

static ghal_audio *theAudio;        /* one stream at a time */
static pthread_mutex_t openLock = PTHREAD_MUTEX_INITIALIZER;

unsigned ghal_audio_rate_of(const ghal_audio *a)
{
    return a->rate;
}

unsigned ghal_audio_channels_of(const ghal_audio *a)
{
    return a->channels;
}

static void *raiser_main(void *arg)
{
    ghal_audio *a = arg;

    pthread_mutex_lock(&a->lock);
    for (;;) {
        while (!a->want && !a->stop)
            pthread_cond_wait(&a->wake, &a->lock);
        if (a->stop)
            break;
        a->want = 0;
        pthread_mutex_unlock(&a->lock);
        ghal_irq_raise(GHAL_IRQ_AUDIO);
        pthread_mutex_lock(&a->lock);
    }
    pthread_mutex_unlock(&a->lock);
    return NULL;
}

void ghal_audio_pull(ghal_audio *a, int16_t *dst, size_t n)
{
    size_t have, take, i, at;
    ghal_audio_tap tap;
    void *tapCtx;

    pthread_mutex_lock(&a->lock);
    have = (size_t)(a->wr - a->rd);
    take = have < n ? have : n;
    for (i = 0; i < take; i++) {
        at = (size_t)((a->rd + i) % a->cap);
        memcpy(dst + i * a->channels, a->ring + at * a->channels,
               a->channels * sizeof(int16_t));
    }
    a->rd += take;
    if (take < n) {
        memset(dst + take * a->channels, 0,
               (n - take) * a->channels * sizeof(int16_t));
        a->underruns += n - take;
    }
    if (!a->paused && (a->wr - a->rd) * 2 <= a->cap && !a->want) {
        a->want = 1;
        pthread_cond_signal(&a->wake);
    }
    tap = a->tap;
    tapCtx = a->tapCtx;
    pthread_mutex_unlock(&a->lock);
    if (tap)
        tap(tapCtx, dst, n);
}

/* ------------------------------------------------------------- null */

typedef struct {
    pthread_t       thread;
    pthread_mutex_t lock;
    pthread_cond_t  cv;
    int             paused, stop;
} null_data;

#define NULL_PERIOD_MS 10

static void *null_main(void *arg)
{
    ghal_audio *a = arg;
    null_data *d = a->beData;
    struct timespec start, now;
    uint64_t done = 0, due;
    int16_t *buf;
    size_t max = (size_t)a->rate * NULL_PERIOD_MS * 4 / 1000 + 1;

    buf = malloc(max * a->channels * sizeof(int16_t));
    if (!buf)
        return NULL;
    pthread_mutex_lock(&d->lock);
    for (;;) {
        struct timespec until;

        while (d->paused && !d->stop)
            pthread_cond_wait(&d->cv, &d->lock);
        if (d->stop)
            break;
        /* real time from the (re)start on */
        clock_gettime(CLOCK_MONOTONIC, &start);
        done = 0;
        while (!d->paused && !d->stop) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            due = (uint64_t)((now.tv_sec - start.tv_sec) * 1000000000LL +
                             (now.tv_nsec - start.tv_nsec)) *
                  a->rate / 1000000000ULL;
            while (due > done) {
                size_t n = due - done > max ? max : (size_t)(due - done);
                pthread_mutex_unlock(&d->lock);
                ghal_audio_pull(a, buf, n);
                pthread_mutex_lock(&d->lock);
                done += n;
            }
            clock_gettime(CLOCK_REALTIME, &until);
            until.tv_nsec += NULL_PERIOD_MS * 1000000L;
            if (until.tv_nsec >= 1000000000L) {
                until.tv_nsec -= 1000000000L;
                until.tv_sec++;
            }
            pthread_cond_timedwait(&d->cv, &d->lock, &until);
        }
    }
    pthread_mutex_unlock(&d->lock);
    free(buf);
    return NULL;
}

static ghal_status null_open(ghal_audio *a, void **data)
{
    null_data *d = calloc(1, sizeof *d);

    if (!d)
        return GHAL_ERR_NOMEM;
    pthread_mutex_init(&d->lock, NULL);
    pthread_cond_init(&d->cv, NULL);
    d->paused = 1;
    a->beData = d;
    if (pthread_create(&d->thread, NULL, null_main, a) != 0) {
        free(d);
        return GHAL_ERR_HOST;
    }
    *data = d;
    return GHAL_OK;
}

static void null_close(ghal_audio *a, void *data)
{
    null_data *d = data;

    (void)a;
    pthread_mutex_lock(&d->lock);
    d->stop = 1;
    pthread_cond_signal(&d->cv);
    pthread_mutex_unlock(&d->lock);
    pthread_join(d->thread, NULL);
    pthread_cond_destroy(&d->cv);
    pthread_mutex_destroy(&d->lock);
    free(d);
}

static void null_pause(ghal_audio *a, void *data, int paused)
{
    null_data *d = data;

    (void)a;
    pthread_mutex_lock(&d->lock);
    d->paused = paused;
    pthread_cond_signal(&d->cv);
    pthread_mutex_unlock(&d->lock);
}

static const ghal_audio_be ghal_audio_null = {
    "null", null_open, null_close, null_pause
};

/* ------------------------------------------------------------- API */

static const ghal_audio_be *choose_backend(void)
{
    const char *e = getenv("GEOS_AUDIO");

    if (e && !strcmp(e, "null"))
        return &ghal_audio_null;
#ifdef GHAL_HAVE_PIPEWIRE
    return &ghal_audio_pipewire;
#else
    return &ghal_audio_null;
#endif
}

ghal_status ghal_audio_open(unsigned rate, unsigned channels,
                            size_t frames, ghal_audio **out)
{
    ghal_audio *a;
    ghal_status st;

    if (!out)
        return GHAL_ERR_INVAL;
    *out = NULL;
    if (rate < 4000 || rate > 192000 || channels < 1 || channels > 2)
        return GHAL_ERR_INVAL;
    if (!frames)
        frames = rate / 10;
    if (frames < 64)
        frames = 64;
    pthread_mutex_lock(&openLock);
    if (theAudio) {
        pthread_mutex_unlock(&openLock);
        return GHAL_ERR_ACCESS;
    }
    a = calloc(1, sizeof *a);
    if (a)
        a->ring = malloc(frames * channels * sizeof(int16_t));
    if (!a || !a->ring) {
        if (a)
            free(a);
        pthread_mutex_unlock(&openLock);
        return GHAL_ERR_NOMEM;
    }
    a->rate = rate;
    a->channels = channels;
    a->cap = frames;
    a->paused = 1;
    pthread_mutex_init(&a->lock, NULL);
    pthread_cond_init(&a->wake, NULL);
    if (pthread_create(&a->raiser, NULL, raiser_main, a) != 0) {
        st = GHAL_ERR_HOST;
        goto fail;
    }
    a->be = choose_backend();
    st = a->be->open(a, &a->beData);
    if (st != GHAL_OK && a->be != &ghal_audio_null) {
        /* no sound server: keep the time anyway */
        a->be = &ghal_audio_null;
        st = a->be->open(a, &a->beData);
    }
    if (st != GHAL_OK) {
        pthread_mutex_lock(&a->lock);
        a->stop = 1;
        pthread_cond_signal(&a->wake);
        pthread_mutex_unlock(&a->lock);
        pthread_join(a->raiser, NULL);
        goto fail;
    }
    theAudio = a;
    pthread_mutex_unlock(&openLock);
    *out = a;
    return GHAL_OK;
fail:
    pthread_cond_destroy(&a->wake);
    pthread_mutex_destroy(&a->lock);
    free(a->ring);
    free(a);
    pthread_mutex_unlock(&openLock);
    return st;
}

/* Not from a GHAL_IRQ_AUDIO handler (it waits for the raiser). */
void ghal_audio_close(ghal_audio *a)
{
    if (!a)
        return;
    pthread_mutex_lock(&openLock);
    a->be->close(a, a->beData);
    pthread_mutex_lock(&a->lock);
    a->stop = 1;
    pthread_cond_signal(&a->wake);
    pthread_mutex_unlock(&a->lock);
    pthread_join(a->raiser, NULL);
    pthread_cond_destroy(&a->wake);
    pthread_mutex_destroy(&a->lock);
    free(a->ring);
    if (theAudio == a)
        theAudio = NULL;
    free(a);
    pthread_mutex_unlock(&openLock);
}

void ghal_audio_pause(ghal_audio *a, int paused)
{
    if (!a)
        return;
    pthread_mutex_lock(&a->lock);
    a->paused = paused != 0;
    if (!a->paused && (a->wr - a->rd) * 2 <= a->cap) {
        a->want = 1;            /* fill it before the device starts */
        pthread_cond_signal(&a->wake);
    }
    pthread_mutex_unlock(&a->lock);
    a->be->pause(a, a->beData, paused != 0);
}

size_t ghal_audio_room(ghal_audio *a)
{
    size_t room;

    if (!a)
        return 0;
    pthread_mutex_lock(&a->lock);
    room = a->cap - (size_t)(a->wr - a->rd);
    pthread_mutex_unlock(&a->lock);
    return room;
}

size_t ghal_audio_write(ghal_audio *a, const int16_t *frames, size_t n)
{
    size_t room, i, at;

    if (!a || !frames)
        return 0;
    pthread_mutex_lock(&a->lock);
    room = a->cap - (size_t)(a->wr - a->rd);
    if (n > room)
        n = room;
    for (i = 0; i < n; i++) {
        at = (size_t)((a->wr + i) % a->cap);
        memcpy(a->ring + at * a->channels, frames + i * a->channels,
               a->channels * sizeof(int16_t));
    }
    a->wr += n;
    pthread_mutex_unlock(&a->lock);
    return n;
}

uint64_t ghal_audio_played(ghal_audio *a)
{
    uint64_t v;

    if (!a)
        return 0;
    pthread_mutex_lock(&a->lock);
    v = a->rd;
    pthread_mutex_unlock(&a->lock);
    return v;
}

uint64_t ghal_audio_underruns(ghal_audio *a)
{
    uint64_t v;

    if (!a)
        return 0;
    pthread_mutex_lock(&a->lock);
    v = a->underruns;
    pthread_mutex_unlock(&a->lock);
    return v;
}

void ghal_audio_set_tap(ghal_audio *a, ghal_audio_tap fn, void *ctx)
{
    if (!a)
        return;
    pthread_mutex_lock(&a->lock);
    a->tap = fn;
    a->tapCtx = ctx;
    pthread_mutex_unlock(&a->lock);
}

const char *ghal_audio_backend(ghal_audio *a)
{
    return a ? a->be->name : "none";
}
