/*
 * test_ghal_audio.c -- ghal_audio (Ghal/Audio) with the backend of
 * $GEOS_AUDIO: ctest ghal.audio "null"; ghal.audio.pipewire "pipewire"
 * with PIPEWIRE_PROPS that link the stream to no sink (the server's
 * dummy driver drives it: nothing is heard):
 *   - the device takes frames in real time, starting paused;
 *   - GHAL_IRQ_AUDIO comes while the ring is half empty, as an interrupt
 *     (ghal_irq_active, inside ghal_crit), and the handler refills it;
 *   - what the device got (the tap) is what the handler wrote, in order,
 *     without silence once it plays;
 *   - paused: nothing taken, no interrupt; resumed; closed; open again.
 * On dos32 (no ghal_audio): GHAL_ERR_UNSUPPORTED.
 */
#define _POSIX_C_SOURCE 200809L   /* setenv */
#include "ghal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

#define RATE 22050
#define CH 2

static ghal_audio *au;
static volatile unsigned irqs, notActive;
static unsigned next;              /* the next frame number written */
static volatile unsigned tapped, tapBad, tapSilent, tapStarted;
static unsigned expect;

/* frame k: left k & 0x7f, right a 441 Hz triangle of +-100 (quiet) */
static void Frame(unsigned k, int16_t *f)
{
    unsigned ph = k % 50;          /* RATE / 441 */

    f[0] = (int16_t)(k & 0x7f);
    f[1] = (int16_t)((ph < 25 ? (int)ph : 50 - (int)ph) * 8 - 100);   /* never 0 */
}

static void Handler(void *ctx)
{
    int16_t buf[256 * CH];
    size_t room, n, i;

    (void)ctx;
    irqs++;
    if (!ghal_irq_active())
        notActive++;
    while ((room = ghal_audio_room(au)) > 0) {
        n = room > 256 ? 256 : room;
        for (i = 0; i < n; i++)
            Frame(next + (unsigned)i, buf + i * CH);
        n = ghal_audio_write(au, buf, n);
        next += (unsigned)n;
    }
}

/* silence (an underrun) is a right channel of 0, which the triangle
 * never has; before the first frame there may be some */
static void Tap(void *ctx, const int16_t *f, size_t n)
{
    size_t i;
    int16_t want[CH];

    (void)ctx;
    for (i = 0; i < n; i++, f += CH) {
        if (f[1] == 0) {
            if (tapStarted)
                tapSilent++;
            continue;
        }
        tapStarted = 1;
        Frame(expect, want);
        if (f[0] != want[0] || f[1] != want[1])
            tapBad++;
        expect++;
        tapped++;
    }
}

static void Sleep(unsigned ms)
{
#ifdef __linux__
    struct timespec t;

    t.tv_sec = (time_t)(ms / 1000);
    t.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&t, NULL);
#else
    (void)ms;                       /* (no ghal_audio there) */
#endif
}

int main(void)
{
    ghal_irq_source *src;
    ghal_status st;
    uint64_t p1, p2;
    unsigned i1;

    setvbuf(stdout, NULL, _IONBF, 0);
    /* never heard, however it is run: a PipeWire stream linked to no
     * sink, driven by the server's dummy driver */
#ifdef __linux__
    setenv("PIPEWIRE_PROPS",
           "{ node.autoconnect = false node.always-process = true }", 1);
#endif
    st = ghal_audio_open(RATE, CH, 0, &au);
    if (st == GHAL_ERR_UNSUPPORTED) {
        CHECK(au == NULL);
        printf("ghal_audio: none on %s, ok\n", ghal_host_name());
        return fails != 0;
    }
    CHECK(st == GHAL_OK && au);
    if (!au)
        return 1;
    printf("backend %s\n", ghal_audio_backend(au));
    {
        ghal_audio *second;
        CHECK(ghal_audio_open(RATE, CH, 0, &second) == GHAL_ERR_ACCESS);
    }                           /* one at a time */
    CHECK(ghal_audio_room(au) == RATE / 10);
    CHECK(ghal_irq_attach(GHAL_IRQ_AUDIO, Handler, NULL, &src) == GHAL_OK);
    ghal_audio_set_tap(au, Tap, NULL);

    /* paused: nothing */
    Sleep(100);
    CHECK(irqs == 0 && ghal_audio_played(au) == 0);

    /* playing for half a second */
    ghal_audio_pause(au, 0);
    Sleep(500);
    p1 = ghal_audio_played(au);
    printf("played %llu, irqs %u, underruns %llu\n",
           (unsigned long long)p1, irqs,
           (unsigned long long)ghal_audio_underruns(au));
    CHECK(irqs >= 3 && notActive == 0);
    CHECK(p1 > RATE / 2 * 7 / 10 && p1 < RATE / 2 * 13 / 10);
    CHECK(tapStarted && tapped > RATE / 4 && tapBad == 0 && tapSilent == 0);
    CHECK(next >= p1 && next - p1 <= RATE / 10);

    /* paused: no more */
    ghal_audio_pause(au, 1);
    Sleep(60);
    p1 = ghal_audio_played(au);
    i1 = irqs;
    Sleep(150);
    CHECK(ghal_audio_played(au) == p1 && irqs == i1);
    /* and on again */
    ghal_audio_pause(au, 0);
    Sleep(200);
    p2 = ghal_audio_played(au);
    CHECK(p2 > p1 + RATE / 10 && irqs > i1 && tapBad == 0);

    ghal_audio_pause(au, 1);
    CHECK(ghal_irq_detach(src) == GHAL_OK);
    ghal_audio_close(au);
    au = NULL;
    /* open again, mono */
    CHECK(ghal_audio_open(8000, 1, 512, &au) == GHAL_OK && au);
    CHECK(ghal_audio_room(au) == 512);
    {
        int16_t s[600] = { 0 };
        CHECK(ghal_audio_write(au, s, 600) == 512 && ghal_audio_room(au) == 0);
    }
    ghal_audio_close(au);
    CHECK(ghal_audio_open(1000, 1, 0, &au) == GHAL_ERR_INVAL);
    printf("%s\n", fails ? "FAILED" : "ghal_audio: ok");
    return fails != 0;
}
