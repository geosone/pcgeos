/*
 * test_ghal_core.c -- host-independent tests of the GHAL (Ghal).
 *
 * Runs on every host (linux, dos32, ...): memory blocks, clocks, critical
 * sections, the tick source and the capability query.  Only ghal.h and the
 * C library are used; waiting is done by polling ghal_time_monotonic_ns.
 * POSIX-only tests are in test_ghal_posix.c.
 */
#include "ghal.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond) do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static int all_zero(const unsigned char *p, size_t n)
{
    while (n--)
        if (*p++)
            return 0;
    return 1;
}

static void wait_ms(unsigned ms)
{
    uint64_t end = ghal_time_monotonic_ns() + (uint64_t)ms * 1000000u;
    while (ghal_time_monotonic_ns() < end)
        ;
}

/* Busy loop that needs no clock (inside ghal_crit a host may stop its
 * clock interrupt, e.g. dos32).  Calibrated outside ghal_crit. */
static volatile uint32_t spin_sink;
static uint32_t spins_per_ms;

static void spin(uint32_t n)
{
    while (n--)
        spin_sink++;
}

static void calibrate_spin(void)
{
    uint32_t n = 1000;
    uint64_t t0, dt;
    for (;;) {
        t0 = ghal_time_monotonic_ns();
        spin(n);
        dt = ghal_time_monotonic_ns() - t0;
        if (dt >= 50000000u || n >= 0x40000000u)
            break;
        n *= 2;
    }
    spins_per_ms = (uint32_t)((uint64_t)n * 1000000u / (dt ? dt : 1));
    if (!spins_per_ms)
        spins_per_ms = 1;
}

static void spin_ms(unsigned ms)
{
    spin(spins_per_ms * ms);
}

/* ---- ghal_mem ---- */

static void test_mem_plain(void)
{
    unsigned char *a = ghal_mem_alloc(100, GHAL_MEM_ZERO), *b;
    size_t i;
    CHECK(a != NULL);
    CHECK(ghal_mem_size(a) == 100);
    CHECK(all_zero(a, 100));
    CHECK(((uintptr_t)a & 15) == 0);
    for (i = 0; i < 100; i++)
        a[i] = (unsigned char)i;
    b = ghal_mem_resize(a, 100000);
    CHECK(b != NULL);
    CHECK(ghal_mem_size(b) == 100000);
    for (i = 0; i < 100; i++)
        if (b[i] != (unsigned char)i)
            break;
    CHECK(i == 100);
    b = ghal_mem_resize(b, 10);
    CHECK(b && ghal_mem_size(b) == 10 && b[9] == 9);
    CHECK(ghal_mem_protect(b, GHAL_PROT_RX) == GHAL_ERR_INVAL); /* not paged */
    ghal_mem_free(b);
    ghal_mem_free(NULL);
    CHECK(ghal_mem_alloc(0, 0) == NULL);
}

static void test_mem_paged(void)
{
    ghal_mem_info info;
    unsigned char *c;
    CHECK(ghal_mem_get_info(&info) == GHAL_OK);
    CHECK(info.page_size >= 4096);
    c = ghal_mem_alloc(5000, GHAL_MEM_PAGED);
    CHECK(c != NULL);
    CHECK(((uintptr_t)c % info.page_size) == 0);
    CHECK(all_zero(c, 5000));
    memset(c, 0xc3, 5000);
    CHECK(ghal_mem_protect(c, GHAL_PROT_RX) == GHAL_OK);
    CHECK(c[4999] == 0xc3);                 /* still readable */
    c = ghal_mem_resize(c, 9000);           /* paged, RW, content kept */
    CHECK(c != NULL && c[4999] == 0xc3 && ghal_mem_size(c) == 9000);
    CHECK(((uintptr_t)c % info.page_size) == 0);
    c[0] = 1;                               /* writable again */
    CHECK(ghal_mem_protect(c, 8) == GHAL_ERR_INVAL);
    ghal_mem_free(c);
    printf("memory: total %lu KB, available %lu KB\n",
           (unsigned long)(info.total / 1024), (unsigned long)(info.available / 1024));
}

/* ---- ghal_time ---- */

static void test_time(void)
{
    ghal_datetime dt;
    uint64_t t0, t1, prev;
    int i, backwards = 0;
    t0 = ghal_time_monotonic_ns();
    wait_ms(20);
    t1 = ghal_time_monotonic_ns();
    CHECK(t1 >= t0 + 20000000u);
    CHECK(t1 < t0 + 1000000000u);
    /* resolution better than a BIOS tick (55 ms) and never backwards */
    prev = ghal_time_monotonic_ns();
    for (i = 0; i < 20000; i++) {
        uint64_t t = ghal_time_monotonic_ns();
        if (t < prev)
            backwards++;
        prev = t;
    }
    CHECK(backwards == 0);
    t0 = ghal_time_monotonic_ns();
    while ((t1 = ghal_time_monotonic_ns()) == t0)
        ;
    CHECK(t1 - t0 < 10000000u);             /* steps below 10 ms */
    CHECK(ghal_time_get_local(&dt) == GHAL_OK);
    CHECK(dt.year >= 1980 && dt.month >= 1 && dt.month <= 12);
    CHECK(dt.day >= 1 && dt.day <= 31 && dt.hour < 24 && dt.weekday < 7);
}

/* ---- ghal_crit, ghal_hostlock ---- */

static void test_crit_nesting(void)
{
    ghal_crit_enter();
    ghal_crit_enter();
    ghal_crit_leave();
    ghal_crit_leave();
    ghal_hostlock_enter();
    ghal_hostlock_enter();
    ghal_hostlock_leave();
    ghal_hostlock_leave();
}

/* ---- ghal_irq: tick source ---- */

static volatile int ticks_a, ticks_b;

static void tick_a(void *ctx)
{
    (void)ctx;
    ticks_a++;
}

static void tick_b(void *ctx)
{
    *(volatile int *)ctx += 1;
}

static void test_irq_tick(void)
{
    ghal_irq_source *a, *b;
    int n, held;
    CHECK(ghal_irq_attach(GHAL_IRQ_TICK, NULL, NULL, &a) == GHAL_ERR_INVAL);
    CHECK(a == NULL);
    CHECK(ghal_irq_attach(12345, tick_a, NULL, &a) == GHAL_ERR_UNSUPPORTED);
    CHECK(ghal_irq_set_tick_rate(0) == GHAL_ERR_INVAL);
    CHECK(ghal_irq_set_tick_rate(100) == GHAL_OK);
    CHECK(ghal_irq_attach(GHAL_IRQ_TICK, tick_a, NULL, &a) == GHAL_OK && a);
    CHECK(ghal_irq_attach(GHAL_IRQ_TICK, tick_b, (void *)&ticks_b, &b) == GHAL_OK && b);
    wait_ms(300);
    n = ticks_a;
    printf("tick: %d handler calls in 300 ms at 100 Hz\n", n);
    CHECK(n >= 15 && n <= 45);              /* ~30 */
    CHECK(ticks_b >= 15 && ticks_b <= 45);

    /* time keeps running with a reprogrammed tick */
    {
        uint64_t t0 = ghal_time_monotonic_ns();
        int k0 = ticks_a;
        wait_ms(200);
        n = ticks_a - k0;
        CHECK(ghal_time_monotonic_ns() - t0 < 400000000u);
        CHECK(n >= 10 && n <= 30);          /* ~20 */
    }

    /* handlers are held off while the kernel is in a critical section */
    ghal_crit_enter();
    held = ticks_a;
    spin_ms(100);
    CHECK(ticks_a == held);
    ghal_crit_leave();
    wait_ms(100);
    CHECK(ticks_a > held);

    CHECK(ghal_irq_detach(a) == GHAL_OK);
    CHECK(ghal_irq_detach(a) == GHAL_ERR_INVAL);
    n = ticks_b;
    wait_ms(100);
    CHECK(ticks_b > n);                     /* b still runs */
    CHECK(ghal_irq_detach(b) == GHAL_OK);   /* last one: source stops */
    n = ticks_b;
    wait_ms(100);
    CHECK(ticks_b == n);
    CHECK(ghal_irq_set_tick_rate(60) == GHAL_OK);
}

static void test_caps(void)
{
    uint32_t caps = ghal_caps();
    CHECK(ghal_host_name() && ghal_host_name()[0]);
    printf("host %s, caps%s%s%s\n", ghal_host_name(),
           caps & GHAL_CAP_THREADS ? " threads" : "",
           caps & GHAL_CAP_HW_IRQ ? " hw-irq" : "",
           caps & GHAL_CAP_EXEC_PROTECT ? " exec-protect" : "");
}

/* ghal_ctx: two contexts take turns, keep their ghal_crit depth and
 * their stacks; one ends with ghal_ctx_exit; one is destroyed unrun. */
static ghal_ctx *ctx_main, *ctx_a, *ctx_b;
static char ctx_log[64];
static int ctx_pos;

static void ctx_note(char c)
{
    if (ctx_pos < (int)sizeof(ctx_log) - 1)
        ctx_log[ctx_pos++] = c;
}

/* deep enough to need the stack of its own */
static int ctx_recurse(int n)
{
    volatile char pad[256];
    pad[0] = (char)n;
    return n == 0 ? pad[0] : ctx_recurse(n - 1) + 1;
}

static void ctx_entry_a(void *arg)
{
    int i, local = 1234;
    CHECK(arg == &ctx_pos);
    ghal_crit_enter();                  /* A switches away inside crit */
    for (i = 0; i < 3; i++) {
        ctx_note('a');
        ghal_ctx_switch(ctx_b);
    }
    ghal_crit_leave();
    CHECK(local == 1234);
    CHECK(ctx_recurse(100) == 100);
    ctx_note('A');
    ghal_ctx_exit(ctx_main);
}

static void ctx_entry_b(void *arg)
{
    (void)arg;
    for (;;) {
        ctx_note('b');
        ghal_ctx_switch(ctx_a);
    }
}

static void ctx_entry_never(void *arg)
{
    (void)arg;
    CHECK(!"never runs");
    ghal_ctx_exit(ctx_main);
}

static void test_ctx(void)
{
    ghal_ctx *never;

    ctx_main = ghal_ctx_self();
    CHECK(ctx_main != NULL && ghal_ctx_self() == ctx_main);
    CHECK(ghal_ctx_create(ctx_entry_a, &ctx_pos, 0, &ctx_a) == GHAL_OK);
    CHECK(ghal_ctx_create(ctx_entry_b, NULL, 128 * 1024, &ctx_b) == GHAL_OK);
    CHECK(ghal_ctx_create(ctx_entry_never, NULL, 0, &never) == GHAL_OK);
    ghal_crit_enter();
    ghal_crit_enter();                  /* main keeps depth 2 */
    ghal_ctx_switch(ctx_a);
    ghal_crit_leave();
    ghal_crit_leave();
    ctx_log[ctx_pos] = 0;
    CHECK(strcmp(ctx_log, "abababA") == 0);
    if (strcmp(ctx_log, "abababA") != 0)
        printf("  ctx order: %s\n", ctx_log);
    ghal_ctx_destroy(ctx_a);
    ghal_ctx_destroy(never);
    /* ctx_b waits in ghal_ctx_switch forever; that is allowed */
    CHECK(!ghal_irq_active());
    printf("ghal_ctx: ok\n");
}

int main(void)
{
    test_caps();
    test_mem_plain();
    test_mem_paged();
    test_time();
    calibrate_spin();
    test_crit_nesting();
    test_ctx();
    test_irq_tick();
    printf("%s: %s\n", ghal_host_name(), failures ? "FAILED" : "all GHAL core tests passed");
    return failures ? 1 : 0;
}
