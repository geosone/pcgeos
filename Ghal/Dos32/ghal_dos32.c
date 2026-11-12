/*
 * ghal_dos32.c -- GHAL backend for 32-bit protected mode DOS (dos32).
 *
 * Two compilers, one backend:
 *   DJGPP gcc   (the port's compiler for dos32, Port32/ABI.md section 5):
 *               CWSDPMI or another DPMI host; conventional memory through
 *               _dos_ds; CPU exceptions arrive as signals.
 *   Open Watcom 386, flat zero-based model under DOS/32A; conventional
 *               memory addressable directly; own DPMI exception stubs.
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
 *   ghal_fault     DPMI exception handlers (0203h); no fault address.
 */
#include "ghal.h"
#include "ghal_internal.h"

#include <dos.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __DJGPP__
#include <crt0.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/exceptn.h>
#include <sys/farptr.h>
#include <unistd.h>    /* _exit */
#else
#include <conio.h>
#include <i86.h>
#include <process.h>   /* _exit */
#endif

#define PIT_HZ        1193182u
#define PIT_CMD       0x43
#define PIT_CH0       0x40
#define PIC1_CMD      0x20
#define PIC_EOI       0x20
#define PIC_READ_IRR  0x0a
#define TICKS_PER_DAY 0x1800b0u

#ifdef __DJGPP__
/* GEOS is the whole system: keep all its memory locked (interrupt
 * handlers and what they call must never be paged out) and the data
 * segment base fixed (the IRQ0 handler runs with our DS).  Long file
 * names are used where DOS has them (Windows 9x, DOSLFN), as by the MSLF
 * driver of DOS16 GEOS (Driver/IFS/HostFS).  readdir gives 8.3 names as DOS
 * has them (upper case), not in lower case. */
int _crt0_startup_flags = _CRT0_FLAG_LOCK_MEMORY | _CRT0_FLAG_NONMOVE_SBRK |
                          _CRT0_FLAG_PRESERVE_FILENAME_CASE;

#define outp(port, v) outportb((port), (unsigned char)(v))
#define inp(port)     inportb(port)
#define bios_ticks()  _farpeekl(_dos_ds, 0x46c)

static unsigned long get_eflags(void)
{
    unsigned long f;
    __asm__ volatile ("pushfl; popl %0" : "=r" (f));
    return f;
}
/* <dos.h> of DJGPP maps _disable/_enable to disable/enable */
#else
#define bios_ticks()  (*(volatile uint32_t *)0x46c)
#endif

/* ------------------------------------------------------------------ */
/* CPU flags                                                          */
/* ------------------------------------------------------------------ */

#ifndef __DJGPP__
unsigned long get_eflags(void);
#pragma aux get_eflags = \
    "pushfd"             \
    "pop eax"            \
    value [eax];
#endif

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

ghal_status ghal_mem_protect_range(void *block, size_t offset, size_t len,
                                   unsigned prot)
{
    mem_header *h = header_of(block);
    (void)len;
    if (!h || !(h->flags & GHAL_MEM_PAGED) || offset % PAGE_SIZE != 0 ||
        offset > h->size ||
        (prot & ~(unsigned)(GHAL_PROT_R | GHAL_PROT_W | GHAL_PROT_X)))
        return GHAL_ERR_INVAL;
    return GHAL_OK;             /* not enforced, see file header */
}

ghal_status ghal_mem_get_info(ghal_mem_info *info)
{
#ifdef __DJGPP__
    __dpmi_free_mem_info fm;
    int fail;
    if (!info)
        return GHAL_ERR_INVAL;
    memset(&fm, 0xff, sizeof fm);
    fail = __dpmi_get_free_memory_information(&fm) != 0;
    info->page_size = PAGE_SIZE;
    info->available = fail ? 0 : fm.largest_available_free_block_in_bytes;
    info->total = (fail || fm.total_number_of_physical_pages == 0xffffffffu) ? 0
                : (uint64_t)fm.total_number_of_physical_pages * PAGE_SIZE;
    return GHAL_OK;
#else
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
#endif
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
/* ghal_ctx: own stacks and a context switch                          */
/*                                                                    */
/* ctx_swap saves the callee-saved registers on the current stack,    */
/* stores the stack pointer, loads the target's and pops its          */
/* registers.  The interrupt flag and the ghal_crit nesting belong to */
/* the context: ghal_ctx_switch saves them with interrupts off and    */
/* the target restores its own (ctx_resume).  A new stack is laid out */
/* as if ctx_swap had been called from ctx_start.  Open Watcom checks */
/* the stack against _STACKLOW; it is switched with the context.      */
/* ------------------------------------------------------------------ */

#define CTX_MIN_STACK (64u * 1024u)

struct ghal_ctx {
    void          *sp;          /* saved stack pointer */
    void          *stack;       /* own stack (NULL: the program's) */
    ghal_ctx_entry fn;
    void          *arg;
    unsigned       crit_depth;  /* saved ghal_crit state */
    int            crit_restore_if;
    int            if_on;       /* interrupts were on */
    int            ended;
#ifndef __DJGPP__
    unsigned       stacklow;    /* _STACKLOW of the context */
#endif
};

static ghal_ctx *ctx_cur;

#ifdef __DJGPP__
void ghal_dos32_ctx_swap(void **save_sp, void *new_sp);
__asm__(
    ".text\n"
    ".globl _ghal_dos32_ctx_swap\n"
    "_ghal_dos32_ctx_swap:\n"
    "    movl 4(%esp), %eax\n"
    "    movl 8(%esp), %edx\n"
    "    pushl %ebp\n"
    "    pushl %ebx\n"
    "    pushl %esi\n"
    "    pushl %edi\n"
    "    movl %esp, (%eax)\n"
    "    movl %edx, %esp\n"
    "    popl %edi\n"
    "    popl %esi\n"
    "    popl %ebx\n"
    "    popl %ebp\n"
    "    ret\n");
#define ctx_swap ghal_dos32_ctx_swap
#define SWITCH_STACKLOW(from, to) ((void)0)
#else
extern unsigned _STACKLOW;              /* Watcom stack check limit */
#define SWITCH_STACKLOW(from, to) \
    ((from)->stacklow = _STACKLOW, _STACKLOW = (to)->stacklow)
static void __declspec(naked) __cdecl ctx_swap(void **save_sp, void *new_sp)
{
    (void)save_sp;
    (void)new_sp;
    _asm {
        mov eax, [esp + 4]
        mov edx, [esp + 8]
        push ebp
        push ebx
        push esi
        push edi
        mov [eax], esp
        mov esp, edx
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
#endif

static void ctx_resume(ghal_ctx *c)
{
    crit_depth = c->crit_depth;
    crit_restore_if = c->crit_restore_if;
    if (c->if_on)
        _enable();
}

static void ctx_start(void)
{
    ghal_ctx *c = ctx_cur;
    ctx_resume(c);
    c->fn(c->arg);
    ghal_log(GHAL_LOG_ERROR, "ghal_ctx: entry function returned");
    abort();
}

ghal_ctx *ghal_ctx_self(void)
{
    if (!ctx_cur) {
        ctx_cur = calloc(1, sizeof(*ctx_cur));
        if (!ctx_cur)
            abort();
    }
    return ctx_cur;
}

ghal_status ghal_ctx_create(ghal_ctx_entry fn, void *arg, size_t stack_size,
                            ghal_ctx **out)
{
    ghal_ctx *c;
    uint32_t *sp;

    if (!fn || !out)
        return GHAL_ERR_INVAL;
    *out = NULL;
    if (stack_size < CTX_MIN_STACK)
        stack_size = CTX_MIN_STACK;
    c = calloc(1, sizeof(*c));
    if (!c)
        return GHAL_ERR_NOMEM;
    c->stack = malloc(stack_size);
    if (!c->stack) {
        free(c);
        return GHAL_ERR_NOMEM;
    }
    c->fn = fn;
    c->arg = arg;
    c->if_on = 1;
#ifndef __DJGPP__
    c->stacklow = (unsigned)(uintptr_t)c->stack + 256;
#endif
    /* top aligned to 16; ctx_start is entered like a call (esp + 4
     * aligned to 16, as gcc expects) */
    sp = (uint32_t *)(((uintptr_t)c->stack + stack_size) & ~(uintptr_t)15);
    *--sp = 0;                          /* return address of ctx_start */
    *--sp = (uint32_t)(uintptr_t)ctx_start;
    *--sp = 0;                          /* ebp */
    *--sp = 0;                          /* ebx */
    *--sp = 0;                          /* esi */
    *--sp = 0;                          /* edi */
    c->sp = sp;
    *out = c;
    return GHAL_OK;
}

void ghal_ctx_switch(ghal_ctx *to)
{
    ghal_ctx *self = ghal_ctx_self();
    unsigned long f;

    if (!to || to == self || to->ended)
        return;
    f = get_eflags();
    _disable();
    self->crit_depth = crit_depth;
    self->crit_restore_if = crit_restore_if;
    self->if_on = (f & IF_FLAG) != 0;
    ctx_cur = to;
    SWITCH_STACKLOW(self, to);
    ctx_swap(&self->sp, to->sp);
    ctx_resume(self);
}

void ghal_ctx_exit(ghal_ctx *to)
{
    ghal_ctx *self = ghal_ctx_self();
    void *dummy;

    _disable();
    self->ended = 1;
    if (!to || to == self) {
        ghal_log(GHAL_LOG_ERROR, "ghal_ctx_exit: nothing to run");
        abort();
    }
    ctx_cur = to;
    SWITCH_STACKLOW(self, to);
    ctx_swap(&dummy, to->sp);
    abort();                            /* not reached */
}

void ghal_ctx_destroy(ghal_ctx *c)
{
    if (!c || c == ctx_cur)
        return;
    free(c->stack);
    free(c);
}

/* No GEOS thread can run: let interrupts in for a moment.  (hlt is
 * privileged under DPMI.) */
void ghal_idle(void)
{
    volatile int i;
    unsigned d = crit_depth;
    int rif = crit_restore_if;

    crit_depth = 0;
    _enable();
    for (i = 0; i < 1000; i++)
        ;
    _disable();
    crit_depth = d;
    crit_restore_if = rif;
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
static struct ghal_irq_source *volatile input_list; /* GHAL_IRQ_INPUT */

static unsigned          tick_hz = 60;
static volatile uint32_t pit_reload = 65536;   /* counts per IRQ0 */
static volatile uint32_t pit_acc;              /* counts since last BIOS tick */
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

/* Runs the tick handlers; returns nonzero when the BIOS handler is due
 * (it counts its tick and sends the EOI). */
static int irq_depth;

int ghal_irq_active(void)
{
    return irq_depth != 0;
}

static int irq0_work(void)
{
    struct ghal_irq_source *s;
    crit_depth++;                   /* we run with interrupts off */
    irq_depth++;
    for (s = tick_list; s; s = s->next)
        s->fn(s->ctx);
    irq_depth--;
    crit_depth--;
    pit_acc += pit_reload;
    if (pit_acc >= 65536u) {
        pit_acc -= 65536u;
        return 1;
    }
    return 0;
}

#ifdef __DJGPP__
static _go32_dpmi_seginfo bios_irq0, our_irq0;
static struct { uint32_t off; uint16_t sel; } __attribute__((packed)) bios_irq0_far;

/* called through the iret wrapper of libc (own stack, our DS) */
static void irq0_handler(void)
{
    if (irq0_work()) {
        /* simulate an interrupt into the previous handler: it returns
         * with IRET */
        __asm__ volatile ("pushfl\n\tlcall *%0" : : "m" (bios_irq0_far)
                          : "memory", "cc");
        return;
    }
    outp(PIC1_CMD, PIC_EOI);
}

static void set_irq0(int ours)
{
    _go32_dpmi_set_protected_mode_interrupt_vector(8, ours ? &our_irq0 : &bios_irq0);
}

static int get_irq0(void)
{
    _go32_dpmi_get_protected_mode_interrupt_vector(8, &bios_irq0);
    bios_irq0_far.off = bios_irq0.pm_offset;
    bios_irq0_far.sel = bios_irq0.pm_selector;
    if (!our_irq0.pm_offset) {
        our_irq0.pm_offset = (unsigned long)irq0_handler;
        our_irq0.pm_selector = _go32_my_cs();
        if (_go32_dpmi_allocate_iret_wrapper(&our_irq0) != 0) {
            our_irq0.pm_offset = 0;
            return 0;
        }
    }
    return 1;
}
#else
static void (__interrupt __far *bios_irq0)(void);

#pragma off (check_stack)
static void __interrupt __far irq0_handler(void)
{
    if (irq0_work())
        _chain_intr(bios_irq0);     /* BIOS counts its tick and sends EOI */
    outp(PIC1_CMD, PIC_EOI);
}
#pragma on (check_stack)

static void set_irq0(int ours)
{
    _dos_setvect(8, ours ? irq0_handler : bios_irq0);
}

static int get_irq0(void)
{
    bios_irq0 = _dos_getvect(8);
    return 1;
}
#endif

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
    set_irq0(0);
    irq0_hooked = 0;
    ghal_crit_leave();
}

/* At exit: first our IRQ0 vector, then the PIT mode.  Never leave DOS
 * with our vector installed or the PIT in a mode the BIOS does not use. */
static void fault_uninstall(void);

static void dos32_cleanup(void)
{
    fault_uninstall();
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

static int irq0_hook(void)
{
    if (irq0_hooked)
        return 1;
    ensure_cleanup();
    ghal_crit_enter();
    if (!get_irq0()) {
        ghal_crit_leave();
        return 0;
    }
    set_irq0(1);
    pit_acc = 0;
    pit_reload = reload_for(tick_hz);
    pit_program(pit_reload);
    irq0_hooked = 1;
    ghal_crit_leave();
    return 1;
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
    if (source != GHAL_IRQ_TICK && source != GHAL_IRQ_INPUT)
        return GHAL_ERR_UNSUPPORTED;
    ghal_crit_enter();
    for (i = 0; i < MAX_SOURCES; i++)
        if (!sources[i].fn) {
            s = &sources[i];
            break;
        }
    if (s && source == GHAL_IRQ_INPUT) {
        s->fn = fn;
        s->ctx = ctx;
        s->next = input_list;
        input_list = s;
        ghal_crit_leave();
        *out = s;
        return GHAL_OK;
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
    if (!irq0_hook()) {
        ghal_irq_detach(s);
        return GHAL_ERR_HOST;
    }
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
    for (pp = (struct ghal_irq_source **)&input_list; *pp; pp = &(*pp)->next)
        if (*pp == src) {
            *pp = src->next;
            src->fn = NULL;
            ghal_crit_leave();
            return GHAL_OK;
        }
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

/* GHAL_IRQ_INPUT: run its handlers like an interrupt */
void ghal_irq_raise(int source)
{
    struct ghal_irq_source *s;
    if (source != GHAL_IRQ_INPUT)
        return;
    ghal_crit_enter();
    irq_depth++;
    for (s = input_list; s; s = s->next)
        s->fn(s->ctx);
    irq_depth--;
    ghal_crit_leave();
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
    ticks = bios_ticks();
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
/* ghal_fault                                                         */
/* ------------------------------------------------------------------ */

static ghal_fault_handler fault_handler;

static ghal_fault_kind fault_kind(uint32_t n)
{
    switch (n) {
    case 0: case 4: return GHAL_FAULT_DIVIDE;
    case 3:         return GHAL_FAULT_BREAKPOINT;
    case 6:         return GHAL_FAULT_ILLEGAL;
    case 16:        return GHAL_FAULT_FPU;
    default:        return GHAL_FAULT_ACCESS;
    }
}

/* the fault was not handled (fault_handler returned or is not set) */
static void fault_fatal(uint32_t n, uint32_t err, void *pc)
{
    fprintf(stderr, "geos[error]: unhandled exception %lu (error code %lx) at pc %p\n",
            (unsigned long)n, (unsigned long)err, pc);
    dos32_cleanup();
    fflush(NULL);
    _exit(3);
}

#ifdef __DJGPP__
/* DJGPP: libc turns CPU exceptions into signals and keeps the exception
 * number and the faulting EIP in __djgpp_exception_state; a handler may
 * longjmp.  DPMI 0.9 gives no fault address (CR2). */
static const int fault_signals[] = { SIGSEGV, SIGFPE, SIGILL, SIGTRAP };
#define NSIG_FAULT (sizeof fault_signals / sizeof fault_signals[0])
static int exc_installed;

static void fault_signal(int sig)
{
    ghal_fault_info info;
    uint32_t n = __djgpp_exception_state_ptr ? (uint32_t)__djgpp_exception_state->__signum : 13;
    switch (sig) {
    case SIGILL:  n = 6; break;
    case SIGTRAP: n = 3; break;
    case SIGFPE:  if (n != 0 && n != 4) n = 16; break;
    default:      break;
    }
    info.kind = fault_kind(n);
    info.address = NULL;
    info.pc = __djgpp_exception_state_ptr ? (void *)__djgpp_exception_state->__eip : NULL;
    if (fault_handler)
        fault_handler(&info);
    fault_fatal(n, 0, info.pc);
}

static void fault_uninstall(void)
{
    size_t i;
    if (!exc_installed)
        return;
    for (i = 0; i < NSIG_FAULT; i++)
        signal(fault_signals[i], SIG_DFL);
    exc_installed = 0;
}

ghal_status ghal_fault_set_handler(ghal_fault_handler fn)
{
    size_t i;
    fault_handler = fn;
    if (exc_installed)
        return GHAL_OK;
    ensure_cleanup();
    for (i = 0; i < NSIG_FAULT; i++)
        if (signal(fault_signals[i], fault_signal) == SIG_ERR) {
            while (i--)
                signal(fault_signals[i], SIG_DFL);
            return GHAL_ERR_HOST;
        }
    exc_installed = 1;
    return GHAL_OK;
}

#else /* Open Watcom: DPMI exception handlers (0202h/0203h) */
/* ------------------------------------------------------------------ */
/*                                                                    */
/* The DPMI host calls a handler with a 32-bit frame on a locked      */
/* stack:  [esp] ret EIP, ret CS, error code, EIP, CS, EFLAGS, ESP,   */
/* SS.  Our stub records the exception, sets the frame's EIP to       */
/* fault_trampoline and returns (RETF): the host resumes the faulting */
/* context there, outside the exception, where the C handler may run  */
/* and longjmp.  DPMI 0.9 gives no fault address (CR2).               */
/* ------------------------------------------------------------------ */

static volatile uint32_t exc_no, exc_err, exc_eip;
static uint32_t tramp_addr;
static unsigned short saved_ds;

/* exceptions we take; 1 (debug) and 7 (no coprocessor, FPU emulators)
 * stay with the host */
static const unsigned char exc_list[] = { 0, 3, 4, 5, 6, 10, 11, 12, 13, 14, 16 };
#define NEXC (sizeof exc_list / sizeof exc_list[0])
static struct { unsigned short sel; uint32_t off; } exc_old[NEXC];
static int exc_installed;

#define EXC_STUB(n)                                                     \
static void __declspec(naked) exc_stub_##n(void)                        \
{                                                                       \
    _asm { push ds }                                                    \
    _asm { push eax }                                                   \
    _asm { mov ds, word ptr cs:saved_ds }                               \
    _asm { mov dword ptr exc_no, n }                                    \
    _asm { mov eax, [esp + 16] }    /* error code */                    \
    _asm { mov exc_err, eax }                                           \
    _asm { mov eax, [esp + 20] }    /* faulting EIP */                  \
    _asm { mov exc_eip, eax }                                           \
    _asm { mov eax, tramp_addr }                                        \
    _asm { mov [esp + 20], eax }    /* resume in fault_trampoline */    \
    _asm { pop eax }                                                    \
    _asm { pop ds }                                                     \
    _asm { retf }                                                       \
}
EXC_STUB(0)  EXC_STUB(3)  EXC_STUB(4)  EXC_STUB(5)  EXC_STUB(6)
EXC_STUB(10) EXC_STUB(11) EXC_STUB(12) EXC_STUB(13) EXC_STUB(14)
EXC_STUB(16)

static void (*const exc_stubs[NEXC])(void) = {
    exc_stub_0, exc_stub_3, exc_stub_4, exc_stub_5, exc_stub_6,
    exc_stub_10, exc_stub_11, exc_stub_12, exc_stub_13, exc_stub_14,
    exc_stub_16
};

/* Entered by "returning" from the exception: no caller, never returns. */
static void fault_trampoline(void)
{
    ghal_fault_info info;
    info.kind = fault_kind(exc_no);
    info.address = NULL;
    info.pc = (void *)exc_eip;
    if (fault_handler)
        fault_handler(&info);
    fault_fatal(exc_no, exc_err, info.pc);
}

static int dpmi_get_exc(unsigned n, unsigned short *sel, uint32_t *off)
{
    union REGS r;
    memset(&r, 0, sizeof r);
    r.x.eax = 0x0202;
    r.h.bl = (unsigned char)n;
    int386(0x31, &r, &r);
    *sel = (unsigned short)r.x.ecx;
    *off = r.x.edx;
    return !r.x.cflag;
}

static int dpmi_set_exc(unsigned n, unsigned short sel, uint32_t off)
{
    union REGS r;
    memset(&r, 0, sizeof r);
    r.x.eax = 0x0203;
    r.h.bl = (unsigned char)n;
    r.x.ecx = sel;
    r.x.edx = off;
    int386(0x31, &r, &r);
    return !r.x.cflag;
}

static void fault_uninstall(void)
{
    size_t i;
    if (!exc_installed)
        return;
    for (i = 0; i < NEXC; i++)
        dpmi_set_exc(exc_list[i], exc_old[i].sel, exc_old[i].off);
    exc_installed = 0;
}

ghal_status ghal_fault_set_handler(ghal_fault_handler fn)
{
    struct SREGS sr;
    size_t i;
    fault_handler = fn;
    if (exc_installed)
        return GHAL_OK;
    segread(&sr);
    saved_ds = sr.ds;
    tramp_addr = (uint32_t)fault_trampoline;
    for (i = 0; i < NEXC; i++)
        if (!dpmi_get_exc(exc_list[i], &exc_old[i].sel, &exc_old[i].off))
            return GHAL_ERR_HOST;
    ensure_cleanup();
    for (i = 0; i < NEXC; i++) {
        if (!dpmi_set_exc(exc_list[i], sr.cs, (uint32_t)exc_stubs[i])) {
            while (i--)
                dpmi_set_exc(exc_list[i], exc_old[i].sel, exc_old[i].off);
            return GHAL_ERR_HOST;
        }
    }
    exc_installed = 1;
    return GHAL_OK;
}
#endif /* __DJGPP__ */

/* ------------------------------------------------------------------ */
/* ghal_log, ghal_exit, ghal_caps                                     */
/* ------------------------------------------------------------------ */

static ghal_log_sink *log_sink;

void ghal_log_set_sink(ghal_log_sink *sink)
{
    log_sink = sink;
}

void ghal_log(int level, const char *fmt, ...)
{
    static const char *const names[] = { "debug", "info", "warn", "error" };
    va_list ap;
    if (level < GHAL_LOG_DEBUG || level > GHAL_LOG_ERROR)
        level = GHAL_LOG_ERROR;
    if (log_sink) {
        char msg[1024];
        va_start(ap, fmt);
        vsnprintf(msg, sizeof msg, fmt, ap);
        va_end(ap);
        log_sink(level, msg);
        return;
    }
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
