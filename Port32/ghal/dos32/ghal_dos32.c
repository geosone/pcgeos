/*
 * ghal_dos32.c -- GHAL backend for 32-bit protected mode DOS (dos32).
 *
 * Open Watcom 386, flat zero-based model under a DPMI extender (DOS/32A),
 * so conventional memory (BIOS data area) is addressable directly.
 *
 *   ghal_mem       C heap of the extender (DPMI memory).  Protection is
 *                  accepted but not enforced (DPMI 0.9 has no page
 *                  attributes); ghal_caps() has no GHAL_CAP_EXEC_PROTECT.
 *   ghal_time      BIOS tick count + 8253 counter (sub-tick resolution),
 *                  wall clock from DOS.
 *   ghal_crit      cli / sti with nesting.
 *   ghal_hostlock  nesting count only (single CPU, no host threads).
 *   ghal_irq       GHAL_IRQ_TICK on IRQ0: the PIT is reprogrammed to the
 *                  tick rate and the BIOS handler is chained at 18.2 Hz.
 *   ghal_fault     not implemented yet (DPMI exception handlers, 0203h).
 */
#include "ghal.h"

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIT_HZ        1193182u
#define PIT_CMD       0x43
#define PIT_CH0       0x40
#define PIC1_CMD      0x20
#define PIC_EOI       0x20
#define PIC_READ_IRR  0x0a
#define BIOS_TICKS    ((volatile uint32_t *)0x46c)
#define TICKS_PER_DAY 0x1800b0u

/* ------------------------------------------------------------------ */
/* CPU flags                                                          */
/* ------------------------------------------------------------------ */

unsigned long get_eflags(void);
#pragma aux get_eflags = \
    "pushfd"             \
    "pop eax"            \
    value [eax];

#define IF_FLAG 0x200u

/* ------------------------------------------------------------------ */
/* ghal_mem                                                           */
/* ------------------------------------------------------------------ */

#define MEM_MAGIC 0x47484d42u   /* "GHMB" */
#define PAGE_SIZE 4096u

typedef struct mem_header {
    size_t   size;              /* user size */
    void    *raw;               /* pointer returned by malloc */
    uint32_t magic;
    uint32_t flags;
} mem_header;

#define HDR_SPACE (((sizeof(mem_header) + 15) / 16) * 16)

static mem_header *header_of(const void *block)
{
    mem_header *h;
    if (!block)
        return NULL;
    h = (mem_header *)((const char *)block - HDR_SPACE);
    if (h->magic != MEM_MAGIC) {
        ghal_log(GHAL_LOG_ERROR, "ghal_mem: %p is not a GHAL block", block);
        abort();
    }
    return h;
}

void *ghal_mem_alloc(size_t size, unsigned flags)
{
    /* user data aligned to 16 bytes (like the POSIX backend), paged
     * blocks to a page; the C heap of Watcom only guarantees 8 */
    size_t align = (flags & GHAL_MEM_PAGED) ? PAGE_SIZE : 16;
    char *raw, *user;
    mem_header *h;
    if (size == 0 || size > (size_t)-1 - HDR_SPACE - 2 * PAGE_SIZE)
        return NULL;
    raw = malloc(HDR_SPACE + align - 1 + size);
    if (!raw)
        return NULL;
    user = (char *)(((uintptr_t)raw + HDR_SPACE + align - 1) & ~(uintptr_t)(align - 1));
    h = (mem_header *)(user - HDR_SPACE);
    h->size = size;
    h->raw = raw;
    h->magic = MEM_MAGIC;
    h->flags = flags & GHAL_MEM_PAGED;
    if (flags & (GHAL_MEM_ZERO | GHAL_MEM_PAGED))
        memset(user, 0, size);
    return user;
}

void *ghal_mem_resize(void *block, size_t size)
{
    mem_header *h = header_of(block);
    void *nb;
    if (!h || size == 0)
        return NULL;
    nb = ghal_mem_alloc(size, h->flags);
    if (!nb)
        return NULL;
    memcpy(nb, block, h->size < size ? h->size : size);
    ghal_mem_free(block);
    return nb;
}

void ghal_mem_free(void *block)
{
    mem_header *h = header_of(block);
    if (!h)
        return;
    h->magic = 0;
    free(h->raw);
}

size_t ghal_mem_size(const void *block)
{
    mem_header *h = header_of(block);
    return h ? h->size : 0;
}

ghal_status ghal_mem_protect(void *block, unsigned prot)
{
    mem_header *h = header_of(block);
    if (!h || !(h->flags & GHAL_MEM_PAGED) ||
        (prot & ~(unsigned)(GHAL_PROT_R | GHAL_PROT_W | GHAL_PROT_X)))
        return GHAL_ERR_INVAL;
    return GHAL_OK;             /* not enforced, see file header */
}

ghal_status ghal_mem_get_info(ghal_mem_info *info)
{
    /* DPMI 0500h: free memory information (48 bytes) */
    uint32_t buf[12];
    union REGS r;
    struct SREGS sr;
    if (!info)
        return GHAL_ERR_INVAL;
    memset(buf, 0xff, sizeof buf);
    memset(&r, 0, sizeof r);
    segread(&sr);
    sr.es = sr.ds;
    r.x.eax = 0x0500;
    r.x.edi = (unsigned)buf;
    int386x(0x31, &r, &r, &sr);
    info->page_size = PAGE_SIZE;
    info->available = r.x.cflag ? 0 : buf[0];
    info->total = (r.x.cflag || buf[6] == 0xffffffffu) ? 0
                : (uint64_t)buf[6] * PAGE_SIZE;
    return GHAL_OK;
}

/* ------------------------------------------------------------------ */
/* ghal_crit, ghal_hostlock                                           */
/* ------------------------------------------------------------------ */

static unsigned crit_depth;
static int      crit_restore_if;

void ghal_crit_enter(void)
{
    unsigned long f = get_eflags();
    _disable();
    if (crit_depth++ == 0)
        crit_restore_if = (f & IF_FLAG) != 0;
}

void ghal_crit_leave(void)
{
    if (crit_depth == 0) {
        ghal_log(GHAL_LOG_ERROR, "ghal: leave without enter");
        abort();
    }
    if (--crit_depth == 0 && crit_restore_if)
        _enable();
}

static unsigned host_depth;
void ghal_hostlock_enter(void) { host_depth++; }
void ghal_hostlock_leave(void)
{
    if (host_depth == 0) {
        ghal_log(GHAL_LOG_ERROR, "ghal: leave without enter");
        abort();
    }
    host_depth--;
}

/* ------------------------------------------------------------------ */
/* ghal_irq: GHAL_IRQ_TICK on IRQ0                                    */
/* ------------------------------------------------------------------ */

struct ghal_irq_source {
    struct ghal_irq_source *next;
    ghal_irq_handler        fn;
    void                   *ctx;
};

#define MAX_SOURCES 8
static struct ghal_irq_source sources[MAX_SOURCES];
static struct ghal_irq_source *volatile tick_list;

static unsigned          tick_hz = 60;
static volatile uint32_t pit_reload = 65536;   /* counts per IRQ0 */
static volatile uint32_t pit_acc;              /* counts since last BIOS tick */
static void (__interrupt __far *bios_irq0)(void);
static int               irq0_hooked;

/* Mode 2 (rate generator) counts down once per period, so the counter
 * tells the time inside a period.  The BIOS default is mode 3 at 65536. */
#define PIT_MODE2 0x34                          /* ch 0, lo/hi, mode 2 */
#define PIT_MODE3 0x36                          /* ch 0, lo/hi, mode 3 */
static int pit_mode2;

static void pit_program_mode(int mode, uint32_t reload)
{
    outp(PIT_CMD, mode);
    outp(PIT_CH0, reload & 0xff);
    outp(PIT_CH0, (reload >> 8) & 0xff);        /* 65536 -> 0, 0 */
}

static void pit_restore(void)
{
    if (pit_mode2) {
        pit_program_mode(PIT_MODE3, 65536u);
        pit_mode2 = 0;
    }
}

static void ensure_cleanup(void);

static void pit_program(uint32_t reload)
{
    ensure_cleanup();
    pit_program_mode(PIT_MODE2, reload);
    pit_mode2 = 1;
}

#pragma off (check_stack)
static void __interrupt __far irq0_handler(void)
{
    struct ghal_irq_source *s;
    crit_depth++;                   /* we run with interrupts off */
    for (s = tick_list; s; s = s->next)
        s->fn(s->ctx);
    crit_depth--;
    pit_acc += pit_reload;
    if (pit_acc >= 65536u) {
        pit_acc -= 65536u;
        _chain_intr(bios_irq0);     /* BIOS counts its tick and sends EOI */
    }
    outp(PIC1_CMD, PIC_EOI);
}
#pragma on (check_stack)

static uint32_t reload_for(unsigned hz)
{
    uint32_t r = (PIT_HZ + hz / 2) / hz;
    return r > 65536u ? 65536u : r;
}

static void irq0_unhook(void)
{
    if (!irq0_hooked)
        return;
    ghal_crit_enter();
    pit_program(65536u);                        /* BIOS rate, still mode 2 */
    pit_reload = 65536u;
    pit_acc = 0;
    _dos_setvect(8, bios_irq0);
    irq0_hooked = 0;
    ghal_crit_leave();
}

/* At exit: first our IRQ0 vector, then the PIT mode.  Never leave DOS
 * with our vector installed or the PIT in a mode the BIOS does not use. */
static void dos32_cleanup(void)
{
    irq0_unhook();
    pit_restore();
}

static void ensure_cleanup(void)
{
    static int done;
    if (!done) {
        atexit(dos32_cleanup);
        done = 1;
    }
}

static void irq0_hook(void)
{
    if (irq0_hooked)
        return;
    ensure_cleanup();
    ghal_crit_enter();
    bios_irq0 = _dos_getvect(8);
    _dos_setvect(8, irq0_handler);
    pit_acc = 0;
    pit_reload = reload_for(tick_hz);
    pit_program(pit_reload);
    irq0_hooked = 1;
    ghal_crit_leave();
}

ghal_status ghal_irq_attach(int source, ghal_irq_handler fn, void *ctx,
                            ghal_irq_source **out)
{
    struct ghal_irq_source *s = NULL;
    int i;
    if (out)
        *out = NULL;
    if (!fn || !out)
        return GHAL_ERR_INVAL;
    if (source != GHAL_IRQ_TICK)
        return GHAL_ERR_UNSUPPORTED;
    ghal_crit_enter();
    for (i = 0; i < MAX_SOURCES; i++)
        if (!sources[i].fn) {
            s = &sources[i];
            break;
        }
    if (s) {
        s->fn = fn;
        s->ctx = ctx;
        s->next = tick_list;
        tick_list = s;
    }
    ghal_crit_leave();
    if (!s)
        return GHAL_ERR_NOMEM;
    irq0_hook();
    *out = s;
    return GHAL_OK;
}

ghal_status ghal_irq_detach(ghal_irq_source *src)
{
    struct ghal_irq_source **pp;
    int found = 0, last;
    if (!src)
        return GHAL_ERR_INVAL;
    ghal_crit_enter();
    for (pp = (struct ghal_irq_source **)&tick_list; *pp; pp = &(*pp)->next)
        if (*pp == src) {
            *pp = src->next;
            found = 1;
            break;
        }
    if (found)
        src->fn = NULL;
    last = found && !tick_list;
    ghal_crit_leave();
    if (last)
        irq0_unhook();
    return found ? GHAL_OK : GHAL_ERR_INVAL;
}

ghal_status ghal_irq_set_tick_rate(unsigned hz)
{
    if (hz < 1 || hz > 1000)
        return GHAL_ERR_INVAL;
    ghal_crit_enter();
    tick_hz = hz;
    if (irq0_hooked) {
        pit_reload = reload_for(hz);
        pit_program(pit_reload);
    }
    ghal_crit_leave();
    return GHAL_OK;
}

/* ------------------------------------------------------------------ */
/* ghal_time                                                          */
/* ------------------------------------------------------------------ */

/* PIT counts (1/1193182 s) since midnight of the first call's day. */
static uint64_t pit_counts_now(void)
{
    static uint32_t last_ticks;
    static uint64_t day_offset;
    uint32_t ticks, acc, reload, count;
    int pending;
    ghal_crit_enter();
    if (!pit_mode2)
        pit_program(pit_reload);                /* same rate, mode 2 */
    ticks = *BIOS_TICKS;
    acc = pit_acc;
    reload = pit_reload;
    outp(PIT_CMD, 0x00);                        /* latch counter 0 */
    count = inp(PIT_CH0);
    count |= (uint32_t)inp(PIT_CH0) << 8;
    outp(PIC1_CMD, PIC_READ_IRR);
    pending = inp(PIC1_CMD) & 1;                /* IRQ0 not yet served */
    ghal_crit_leave();
    if (count == 0)
        count = 65536u;
    count = count > reload ? reload : count;
    /* An IRQ0 that is pending but not served means the counter wrapped
     * and its period is not in ticks/acc yet.  The wrap can also happen
     * after the latch and before the IRR read; then the latched count is
     * still from the old period (small).  Only a freshly reloaded counter
     * (upper half) belongs to the new period. */
    pending = pending && count > reload / 2;
    if (ticks < last_ticks)                     /* BIOS midnight wrap */
        day_offset += (uint64_t)TICKS_PER_DAY * 65536u;
    last_ticks = ticks;
    return day_offset + (uint64_t)ticks * 65536u + acc
         + (reload - count) + (pending ? reload : 0);
}

uint64_t ghal_time_monotonic_ns(void)
{
    static uint64_t last;
    uint64_t c = pit_counts_now();
    uint64_t ns = c / PIT_HZ * 1000000000u
                + (c % PIT_HZ) * 1000000000u / PIT_HZ;
    if (ns < last)                              /* never go backwards */
        ns = last;
    last = ns;
    return ns;
}

ghal_status ghal_time_get_local(ghal_datetime *dt)
{
    struct dosdate_t d;
    struct dostime_t t;
    if (!dt)
        return GHAL_ERR_INVAL;
    _dos_getdate(&d);
    _dos_gettime(&t);
    dt->year = d.year;
    dt->month = d.month;
    dt->day = d.day;
    dt->weekday = d.dayofweek;
    dt->hour = t.hour;
    dt->minute = t.minute;
    dt->second = t.second;
    return GHAL_OK;
}

/* ------------------------------------------------------------------ */
/* ghal_fault: not implemented yet                                    */
/* ------------------------------------------------------------------ */

ghal_status ghal_fault_set_handler(ghal_fault_handler fn)
{
    (void)fn;
    return GHAL_ERR_UNSUPPORTED;
}

/* ------------------------------------------------------------------ */
/* ghal_log, ghal_exit, ghal_caps                                     */
/* ------------------------------------------------------------------ */

void ghal_log(int level, const char *fmt, ...)
{
    static const char *const names[] = { "debug", "info", "warn", "error" };
    va_list ap;
    if (level < GHAL_LOG_DEBUG || level > GHAL_LOG_ERROR)
        level = GHAL_LOG_ERROR;
    fprintf(stderr, "geos[%s]: ", names[level]);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void ghal_debug_break(void)
{
    /* no way to tell whether a debugger is attached; do nothing */
}

void ghal_exit(int how)
{
    dos32_cleanup();
    fflush(NULL);
    exit(how == GHAL_EXIT_NORMAL ? 0 : how == GHAL_EXIT_RESTART ? 2 : 1);
}

const char *ghal_host_name(void)
{
    return "dos32";
}

uint32_t ghal_caps(void)
{
    return GHAL_CAP_HW_IRQ;
}
