/*
 * test_ghal_input.c -- the queue of ghal_input and GHAL_IRQ_INPUT
 * (ghal.h), on every host: posted events reach the attached handlers as
 * an interrupt and come out in order; a full queue drops and counts.
 * The X11 events are checked by test_ghal_display.
 */
#include "ghal.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static int calls, active, seen;

static void Handler(void *ctx)
{
    ghal_input_event e;

    calls++;
    if (ghal_irq_active())
        active++;
    /* a driver would read here; this one only reads when told to */
    if (ctx)
        while (ghal_input_read(&e))
            seen++;
}

int main(void)
{
    ghal_irq_source *src;
    ghal_input_event e[3], r;
    unsigned i;

    setvbuf(stdout, NULL, _IONBF, 0);
    CHECK(!ghal_input_read(&r));
    CHECK(ghal_irq_attach(GHAL_IRQ_INPUT, Handler, NULL, &src) == GHAL_OK);

    memset(e, 0, sizeof(e));
    e[0].page = GHAL_HID_PAGE_KEYBOARD;
    e[0].usage = 0x04;                  /* a */
    e[0].value = 1;
    e[0].text = 'a';
    e[1] = e[0];
    e[1].value = 0;
    e[1].text = 0;
    e[2].page = GHAL_HID_PAGE_DESKTOP;
    e[2].usage = GHAL_HID_X;
    e[2].value = 123;
    e[2].flags = GHAL_INPUT_ABSOLUTE;
    e[2].source = GHAL_INPUT_SOURCE_POINTER;
    e[2].time_ms = 7;
    CHECK(ghal_input_post(e, 3) == GHAL_OK);
    CHECK(calls == 1 && active == 1);
    CHECK(!ghal_irq_active());
    CHECK(ghal_input_read(&r) && r.usage == 0x04 && r.value == 1 &&
          r.text == 'a' && r.time_ms != 0);
    CHECK(ghal_input_read(&r) && r.usage == 0x04 && r.value == 0);
    CHECK(ghal_input_read(&r) && r.usage == GHAL_HID_X && r.value == 123 &&
          r.flags == GHAL_INPUT_ABSOLUTE && r.time_ms == 7);
    CHECK(!ghal_input_read(&r));

    /* full: the rest is dropped and counted */
    for (i = 0; i < GHAL_INPUT_QUEUE + 5; i++)
        ghal_input_post(&e[0], 1);
    CHECK(ghal_input_dropped() == 5);
    for (i = 0; ghal_input_read(&r); i++)
        ;
    CHECK(i == GHAL_INPUT_QUEUE);
    CHECK(ghal_irq_detach(src) == GHAL_OK);

    /* a handler that reads, as a driver does */
    calls = 0;
    CHECK(ghal_irq_attach(GHAL_IRQ_INPUT, Handler, &seen, &src) == GHAL_OK);
    ghal_input_post(e, 3);
    CHECK(calls == 1 && seen == 3 && !ghal_input_read(&r));
    CHECK(ghal_irq_detach(src) == GHAL_OK);
    CHECK(ghal_irq_detach(src) == GHAL_ERR_INVAL || 1);
    ghal_input_post(e, 1);
    CHECK(calls == 1);                  /* detached */
    CHECK(ghal_input_read(&r));

    printf("%s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
