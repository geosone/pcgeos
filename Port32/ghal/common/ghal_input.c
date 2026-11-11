/*
 * ghal_input.c -- the input queue of ghal_input (ghal.h), the same on
 * every host.  Backends and callers of ghal_input_post fill it; the
 * handlers of GHAL_IRQ_INPUT empty it.  Guarded by ghal_crit.
 */
#include "ghal.h"
#include "ghal_internal.h"

static ghal_input_event queue[GHAL_INPUT_QUEUE];
static unsigned head, count;
static uint32_t dropped;

int ghal_input_read(ghal_input_event *ev)
{
    int got = 0;

    if (!ev)
        return 0;
    ghal_crit_enter();
    if (count) {
        *ev = queue[head];
        head = (head + 1) % GHAL_INPUT_QUEUE;
        count--;
        got = 1;
    }
    ghal_crit_leave();
    return got;
}

ghal_status ghal_input_post(const ghal_input_event *ev, unsigned n)
{
    uint32_t now;
    unsigned i;

    if (!ev && n)
        return GHAL_ERR_INVAL;
    if (!n)
        return GHAL_OK;
    now = (uint32_t) (ghal_time_monotonic_ns() / 1000000u);
    ghal_crit_enter();
    for (i = 0; i < n; i++) {
        if (count == GHAL_INPUT_QUEUE) {
            dropped++;
            continue;
        }
        queue[(head + count) % GHAL_INPUT_QUEUE] = ev[i];
        if (!ev[i].time_ms)
            queue[(head + count) % GHAL_INPUT_QUEUE].time_ms = now;
        count++;
    }
    ghal_crit_leave();
    ghal_irq_raise(GHAL_IRQ_INPUT);
    return GHAL_OK;
}

uint32_t ghal_input_dropped(void)
{
    return dropped;
}
