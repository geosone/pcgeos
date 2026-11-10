/*
 * ghal.h -- GEOS Hardware Abstraction Layer, C interface (draft, API 1).
 *
 * The only place where the 32/64-bit GEOS kernel touches its host.  Each
 * host (dos32, linux, win32/win64, later gonware) implements this file in
 * Port32/ghal/<backend>/.  Design: Port32/GHAL-Concept.md, sections 4 and
 * 4.6; the asm side for the 16-bit kernel is Include/Internal/ghal.def.
 *
 * Conventions:
 *   - Functions that can fail return ghal_status (GHAL_OK == 0) or NULL.
 *   - Nothing here is GEOS-specific beyond the needs of the kernel; the
 *     kernel builds handles, threads and the heap on top of it.
 *   - A backend may return GHAL_ERR_UNSUPPORTED for what its host cannot
 *     offer (e.g. ghal_irq sources other than GHAL_IRQ_TICK).
 */
#ifndef GHAL_H
#define GHAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GHAL_API_VERSION 1

typedef enum ghal_status {
    GHAL_OK = 0,
    GHAL_ERR_NOMEM,         /* out of memory */
    GHAL_ERR_INVAL,         /* bad argument */
    GHAL_ERR_UNSUPPORTED,   /* not available on this host */
    GHAL_ERR_HOST           /* host call failed for another reason */
} ghal_status;

/* ---------------------------------------------------------------------
 * ghal_mem -- memory blocks for the kernel heap
 *
 * One host allocation per heap block (as GPMI_CALL_ALLOCATE_BLOCK in the
 * pm16 kernel).  A block never moves unless ghal_mem_resize moves it, so a
 * locked GEOS block is a plain pointer.
 *
 * Code needs GHAL_MEM_PAGED: such blocks are page aligned and are the only
 * ones ghal_mem_protect accepts.  Plain blocks are cheaper (no page
 * rounding) and always readable and writable.
 * ------------------------------------------------------------------- */

enum {
    GHAL_PROT_NONE = 0,
    GHAL_PROT_R    = 1,
    GHAL_PROT_W    = 2,
    GHAL_PROT_X    = 4,
    GHAL_PROT_RW   = GHAL_PROT_R | GHAL_PROT_W,
    GHAL_PROT_RX   = GHAL_PROT_R | GHAL_PROT_X
};

enum {
    GHAL_MEM_ZERO  = 1,     /* clear the block */
    GHAL_MEM_PAGED = 2      /* page aligned; may be passed to ghal_mem_protect */
};

typedef struct ghal_mem_info {
    uint64_t total;         /* memory GEOS may use, bytes (0 = unknown) */
    uint64_t available;     /* currently available, bytes (0 = unknown) */
    uint32_t page_size;     /* host page size */
} ghal_mem_info;

/* Allocate size bytes (size > 0), readable and writable.  NULL on failure. */
void       *ghal_mem_alloc(size_t size, unsigned flags);
/* Change the size; may move the block.  NULL on failure (block unchanged).
 * A paged block stays paged and becomes GHAL_PROT_RW. */
void       *ghal_mem_resize(void *block, size_t size);
/* Release a block from ghal_mem_alloc/ghal_mem_resize.  NULL is ignored. */
void        ghal_mem_free(void *block);
/* Size passed to the last alloc/resize (for error checking). */
size_t      ghal_mem_size(const void *block);
/* Set the protection of a whole paged block (e.g. GHAL_PROT_RX for code
 * after relocation, GPMI_CALL_CONVERT_TO_CODE_BLOCK). */
ghal_status ghal_mem_protect(void *block, unsigned prot);
ghal_status ghal_mem_get_info(ghal_mem_info *info);

/* ---------------------------------------------------------------------
 * ghal_time -- clocks.  The tick source for the scheduler is part of
 * ghal_irq (an event source), not of this group.
 * ------------------------------------------------------------------- */

typedef struct ghal_datetime {
    uint16_t year;          /* e.g. 2026 */
    uint8_t  month;         /* 1..12 */
    uint8_t  day;           /* 1..31 */
    uint8_t  hour, minute, second;
    uint8_t  weekday;       /* 0 = Sunday */
} ghal_datetime;

/* Monotonic time in nanoseconds since an arbitrary start. */
uint64_t    ghal_time_monotonic_ns(void);
/* Local wall clock time (DOS: int 21h/2Ah,2Ch or the RTC). */
ghal_status ghal_time_get_local(ghal_datetime *dt);

/* ---------------------------------------------------------------------
 * ghal_crit -- critical sections; replaces INT_OFF/INT_ON.
 *
 * Nestable.  Excludes the interrupt context (ghal_irq handlers) and, on
 * hosts with threads, every other GEOS thread.  dos32: cli/sti with a
 * nesting count.
 * ------------------------------------------------------------------- */

void        ghal_crit_enter(void);
void        ghal_crit_leave(void);

/* ---------------------------------------------------------------------
 * ghal_hostlock -- serialize calls into non-reentrant host services;
 * replaces SysLockBIOS/SysUnlockBIOS.  Nestable.
 * ------------------------------------------------------------------- */

void        ghal_hostlock_enter(void);
void        ghal_hostlock_leave(void);

/* ---------------------------------------------------------------------
 * ghal_irq -- asynchronous event sources.
 *
 * dos32: hardware IRQ vectors.  Other hosts: a host thread or the event
 * loop calls the handler between ghal_irq_enter/ghal_irq_exit, serialized
 * against ghal_crit like a real interrupt.  SysCatchInterrupt and
 * SysCatchDeviceInterrupt are built on this.
 * ------------------------------------------------------------------- */

typedef void (*ghal_irq_handler)(void *ctx);
typedef struct ghal_irq_source ghal_irq_source;

enum {
    GHAL_IRQ_TICK = 0       /* scheduler tick (dos32: IRQ0 / PIT) */
};

ghal_status ghal_irq_attach(int source, ghal_irq_handler fn, void *ctx,
                            ghal_irq_source **out);
/* Not from inside a handler. */
ghal_status ghal_irq_detach(ghal_irq_source *src);
/* Tick rate of GHAL_IRQ_TICK in Hz (GEOS uses 60). */
ghal_status ghal_irq_set_tick_rate(unsigned hz);

/* ---------------------------------------------------------------------
 * ghal_fault -- CPU exceptions.
 * dos32: DPMI exceptions; linux: SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGTRAP;
 * windows: vectored exception handler.
 * ------------------------------------------------------------------- */

typedef enum ghal_fault_kind {
    GHAL_FAULT_ACCESS,      /* invalid memory access */
    GHAL_FAULT_DIVIDE,      /* integer divide error */
    GHAL_FAULT_ILLEGAL,     /* invalid instruction */
    GHAL_FAULT_BREAKPOINT,  /* breakpoint / trap */
    GHAL_FAULT_FPU          /* floating point exception */
} ghal_fault_kind;

typedef struct ghal_fault_info {
    ghal_fault_kind kind;
    void           *address;    /* faulting data address, if known */
    void           *pc;         /* faulting instruction, if known */
} ghal_fault_info;

/* The handler does not return normally: it reports and ends GEOS, or
 * continues elsewhere (e.g. longjmp).  If it returns, the GHAL ends the
 * process with a crash report. */
typedef void (*ghal_fault_handler)(const ghal_fault_info *info);
ghal_status ghal_fault_set_handler(ghal_fault_handler fn);

/* ---------------------------------------------------------------------
 * ghal_log -- debug output.
 * ------------------------------------------------------------------- */

enum { GHAL_LOG_DEBUG, GHAL_LOG_INFO, GHAL_LOG_WARN, GHAL_LOG_ERROR };

void        ghal_log(int level, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
/* Stop in the host debugger if one is attached (int 3 / SIGTRAP). */
void        ghal_debug_break(void);

/* ---------------------------------------------------------------------
 * ghal_exit -- leave GEOS.
 * ------------------------------------------------------------------- */

enum {
    GHAL_EXIT_NORMAL  = 0,
    GHAL_EXIT_ERROR   = 1,
    GHAL_EXIT_RESTART = 2   /* start GEOS again (GPMI_CALL_RESTART_LOADER) */
};

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void        ghal_exit(int how);

/* ---------------------------------------------------------------------
 * ghal_caps -- what this host offers.
 * ------------------------------------------------------------------- */

enum {
    GHAL_CAP_THREADS      = 1u << 0,  /* GEOS threads are host threads */
    GHAL_CAP_HW_IRQ       = 1u << 1,  /* real hardware interrupts (dos32) */
    GHAL_CAP_EXEC_PROTECT = 1u << 2   /* ghal_mem_protect enforces X */
};

const char *ghal_host_name(void);     /* "dos32", "linux", ... */
uint32_t    ghal_caps(void);

#ifdef __cplusplus
}
#endif

#endif /* GHAL_H */
