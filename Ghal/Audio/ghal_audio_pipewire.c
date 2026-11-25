/*
 * ghal_audio_pipewire.c -- the backend "pipewire" of ghal_audio: one
 * playback stream of the PipeWire server of the session, connected to
 * its default sink.  Its process callback (PipeWire's realtime thread)
 * takes the frames with ghal_audio_pull, which never waits on
 * ghal_crit.  Without a server the open fails and ghal_audio falls back
 * to "null".
 */
#define _GNU_SOURCE 1   /* locale_t of the spa headers */

#include "ghal.h"
#include "ghal_audio_int.h"

#include <stdio.h>
#include <stdlib.h>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

typedef struct {
    struct pw_thread_loop *loop;
    struct pw_context     *context;
    struct pw_core        *core;
    struct pw_stream      *stream;
    struct spa_hook        listener;
    ghal_audio            *a;
    unsigned               stride;  /* bytes a frame */
} pw_data;

static void on_process(void *userdata)
{
    pw_data *d = userdata;
    struct pw_buffer *b;
    struct spa_buffer *buf;
    int16_t *dst;
    uint32_t n;

    b = pw_stream_dequeue_buffer(d->stream);
    if (!b)
        return;
    buf = b->buffer;
    dst = buf->datas[0].data;
    if (!dst) {
        pw_stream_queue_buffer(d->stream, b);
        return;
    }
    n = buf->datas[0].maxsize / d->stride;
    if (b->requested && b->requested < n)
        n = (uint32_t)b->requested;
    ghal_audio_pull(d->a, dst, n);
    buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->stride = (int32_t)d->stride;
    buf->datas[0].chunk->size = n * d->stride;
    pw_stream_queue_buffer(d->stream, b);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
};

static void pw_close_data(pw_data *d)
{
    if (d->loop)
        pw_thread_loop_stop(d->loop);
    if (d->stream)
        pw_stream_destroy(d->stream);
    if (d->core)
        pw_core_disconnect(d->core);
    if (d->context)
        pw_context_destroy(d->context);
    if (d->loop)
        pw_thread_loop_destroy(d->loop);
    free(d);
}

static ghal_status gpw_open(ghal_audio *a, void **data)
{
    static int inited;
    pw_data *d;
    uint8_t pod[1024];
    struct spa_pod_builder bld = SPA_POD_BUILDER_INIT(pod, sizeof(pod));
    const struct spa_pod *params[1];
    struct spa_audio_info_raw info = { 0 };
    struct pw_properties *props;
    char latency[32], rate[16];
    unsigned r = ghal_audio_rate_of(a), ch = ghal_audio_channels_of(a);

    if (!inited) {
        pw_init(NULL, NULL);
        inited = 1;
    }
    d = calloc(1, sizeof *d);
    if (!d)
        return GHAL_ERR_NOMEM;
    d->a = a;
    d->stride = ch * sizeof(int16_t);
    d->loop = pw_thread_loop_new("geos-audio", NULL);
    if (!d->loop)
        goto fail;
    d->context = pw_context_new(pw_thread_loop_get_loop(d->loop), NULL, 0);
    if (!d->context)
        goto fail;
    if (pw_thread_loop_start(d->loop) < 0)
        goto fail;
    pw_thread_loop_lock(d->loop);
    d->core = pw_context_connect(d->context, NULL, 0);
    if (!d->core) {
        pw_thread_loop_unlock(d->loop);
        goto fail;
    }
    snprintf(latency, sizeof latency, "%u/%u", r / 50, r);   /* 20 ms */
    snprintf(rate, sizeof rate, "1/%u", r);
    props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                              PW_KEY_MEDIA_CATEGORY, "Playback",
                              PW_KEY_MEDIA_ROLE, "Music",
                              PW_KEY_APP_NAME, "GEOS",
                              PW_KEY_NODE_NAME, "geos",
                              PW_KEY_NODE_LATENCY, latency,
                              PW_KEY_NODE_RATE, rate,
                              NULL);
    d->stream = pw_stream_new(d->core, "GEOS", props);
    if (!d->stream) {
        pw_thread_loop_unlock(d->loop);
        goto fail;
    }
    pw_stream_add_listener(d->stream, &d->listener, &stream_events, d);
    info.format = SPA_AUDIO_FORMAT_S16;
    info.rate = r;
    info.channels = ch;
    if (ch == 1) {
        info.position[0] = SPA_AUDIO_CHANNEL_MONO;
    } else {
        info.position[0] = SPA_AUDIO_CHANNEL_FL;
        info.position[1] = SPA_AUDIO_CHANNEL_FR;
    }
    params[0] = spa_format_audio_raw_build(&bld, SPA_PARAM_EnumFormat, &info);
    if (pw_stream_connect(d->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                          PW_STREAM_FLAG_AUTOCONNECT |
                          PW_STREAM_FLAG_MAP_BUFFERS |
                          PW_STREAM_FLAG_RT_PROCESS |
                          PW_STREAM_FLAG_INACTIVE,
                          params, 1) < 0) {
        pw_thread_loop_unlock(d->loop);
        goto fail;
    }
    pw_thread_loop_unlock(d->loop);
    *data = d;
    return GHAL_OK;
fail:
    pw_close_data(d);
    return GHAL_ERR_HOST;
}

static void gpw_close(ghal_audio *a, void *data)
{
    (void)a;
    pw_close_data(data);
}

static void gpw_pause(ghal_audio *a, void *data, int paused)
{
    pw_data *d = data;

    (void)a;
    pw_thread_loop_lock(d->loop);
    pw_stream_set_active(d->stream, !paused);
    pw_thread_loop_unlock(d->loop);
}

const ghal_audio_be ghal_audio_pipewire = {
    "pipewire", gpw_open, gpw_close, gpw_pause
};
