/*
 * ghal_posix.c -- GHAL backend for POSIX hosts (linux; later other Unix).
 *
 * Implements ghal_mem, ghal_time, ghal_crit, ghal_hostlock, ghal_log,
 * ghal_exit and ghal_caps.  ghal_irq and ghal_fault are not implemented yet
 * and return GHAL_ERR_UNSUPPORTED.
 */
#define _GNU_SOURCE 1   /* MAP_ANONYMOUS, _SC_AVPHYS_PAGES */

#include "ghal.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* ghal_mem                                                           */
/*                                                                    */
/* Every block is preceded by a header.  Plain blocks come from       */
/* malloc.  Paged blocks are mmap'ed: the header sits alone in the    */
/* first page, so the user pages can be protected without touching    */
/* it.                                                                */
/* ------------------------------------------------------------------ */

#define MEM_MAGIC 0x47484d42u   /* "GHMB" */

typedef struct mem_header {
    size_t   size;              /* user size */
    size_t   map_len;           /* paged: total mapping length, else 0 */
    uint32_t magic;
    uint32_t flags;
} mem_header;

/* keep user data aligned like malloc does */
#define HDR_SPACE (((sizeof(mem_header) + 15) / 16) * 16)

static size_t page_size(void)
{
    static size_t ps;
    if (!ps) {
        long v = sysconf(_SC_PAGESIZE);
        ps = v > 0 ? (size_t)v : 4096;
    }
    return ps;
}

static size_t round_up(size_t n, size_t to)
{
    return (n + to - 1) / to * to;
}

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

static void *alloc_paged(size_t size)
{
    size_t ps = page_size();
    size_t len = ps + round_up(size, ps);
    char *base = mmap(NULL, len, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    mem_header *h;
    if (base == MAP_FAILED)
        return NULL;
    h = (mem_header *)(base + ps - HDR_SPACE);
    h->size = size;
    h->map_len = len;
    h->magic = MEM_MAGIC;
    h->flags = GHAL_MEM_PAGED;
    return base + ps;               /* mmap memory is already zero */
}

void *ghal_mem_alloc(size_t size, unsigned flags)
{
    mem_header *h;
    if (size == 0 || size > SIZE_MAX - HDR_SPACE - 2 * page_size())
        return NULL;
    if (flags & GHAL_MEM_PAGED)
        return alloc_paged(size);
    h = (flags & GHAL_MEM_ZERO) ? calloc(1, HDR_SPACE + size)
                                : malloc(HDR_SPACE + size);
    if (!h)
        return NULL;
    h->size = size;
    h->map_len = 0;
    h->magic = MEM_MAGIC;
    h->flags = 0;
    return (char *)h + HDR_SPACE;
}

void *ghal_mem_resize(void *block, size_t size)
{
    mem_header *h = header_of(block);
    void *nb;
    if (!h || size == 0 || size > SIZE_MAX - HDR_SPACE - 2 * page_size())
        return NULL;
    if (h->map_len) {
        nb = alloc_paged(size);
        if (!nb)
            return NULL;
        /* the old block may be RX or read-only: reading is enough */
        if (mprotect(block, h->map_len - page_size(), PROT_READ) != 0) {
            ghal_mem_free(nb);
            return NULL;
        }
        memcpy(nb, block, h->size < size ? h->size : size);
        ghal_mem_free(block);
        return nb;
    }
    h->magic = 0;
    nb = realloc(h, HDR_SPACE + size);
    if (!nb) {
        h->magic = MEM_MAGIC;
        return NULL;
    }
    h = nb;
    h->magic = MEM_MAGIC;
    h->size = size;
    return (char *)h + HDR_SPACE;
}

void ghal_mem_free(void *block)
{
    mem_header *h = header_of(block);
    if (!h)
        return;
    h->magic = 0;
    if (h->map_len)
        munmap((char *)block - page_size(), h->map_len);
    else
        free(h);
}

size_t ghal_mem_size(const void *block)
{
    mem_header *h = header_of(block);
    return h ? h->size : 0;
}

ghal_status ghal_mem_protect(void *block, unsigned prot)
{
    mem_header *h = header_of(block);
    int p = 0;
    if (!h || !h->map_len || (prot & ~(unsigned)(GHAL_PROT_R | GHAL_PROT_W | GHAL_PROT_X)))
        return GHAL_ERR_INVAL;
    if (prot & GHAL_PROT_R) p |= PROT_READ;
    if (prot & GHAL_PROT_W) p |= PROT_WRITE;
    if (prot & GHAL_PROT_X) p |= PROT_EXEC;
    if (mprotect(block, h->map_len - page_size(), p) != 0)
        return errno == ENOMEM ? GHAL_ERR_NOMEM : GHAL_ERR_HOST;
    return GHAL_OK;
}

ghal_status ghal_mem_get_info(ghal_mem_info *info)
{
    long pages, avail;
    if (!info)
        return GHAL_ERR_INVAL;
    info->page_size = (uint32_t)page_size();
    pages = sysconf(_SC_PHYS_PAGES);
#ifdef _SC_AVPHYS_PAGES
    avail = sysconf(_SC_AVPHYS_PAGES);
#else
    avail = -1;
#endif
    info->total = pages > 0 ? (uint64_t)pages * info->page_size : 0;
    info->available = avail > 0 ? (uint64_t)avail * info->page_size : 0;
    return GHAL_OK;
}

/* ------------------------------------------------------------------ */
/* ghal_time                                                          */
/* ------------------------------------------------------------------ */

uint64_t ghal_time_monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

ghal_status ghal_time_get_local(ghal_datetime *dt)
{
    time_t now;
    struct tm tm;
    if (!dt)
        return GHAL_ERR_INVAL;
    now = time(NULL);
    if (now == (time_t)-1 || !localtime_r(&now, &tm))
        return GHAL_ERR_HOST;
    dt->year = (uint16_t)(tm.tm_year + 1900);
    dt->month = (uint8_t)(tm.tm_mon + 1);
    dt->day = (uint8_t)tm.tm_mday;
    dt->hour = (uint8_t)tm.tm_hour;
    dt->minute = (uint8_t)tm.tm_min;
    dt->second = (uint8_t)(tm.tm_sec > 59 ? 59 : tm.tm_sec);
    dt->weekday = (uint8_t)tm.tm_wday;
    return GHAL_OK;
}

/* ------------------------------------------------------------------ */
/* ghal_crit, ghal_hostlock: recursive locks                          */
/* ------------------------------------------------------------------ */

static pthread_mutex_t crit_lock, host_lock;
static pthread_once_t locks_once = PTHREAD_ONCE_INIT;

static void init_locks(void)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&crit_lock, &a);
    pthread_mutex_init(&host_lock, &a);
    pthread_mutexattr_destroy(&a);
}

static void lock(pthread_mutex_t *m)
{
    pthread_once(&locks_once, init_locks);
    if (pthread_mutex_lock(m) != 0)
        abort();
}

static void unlock(pthread_mutex_t *m)
{
    if (pthread_mutex_unlock(m) != 0) {
        ghal_log(GHAL_LOG_ERROR, "ghal: leave without enter");
        abort();
    }
}

void ghal_crit_enter(void)     { lock(&crit_lock); }
void ghal_crit_leave(void)     { unlock(&crit_lock); }
void ghal_hostlock_enter(void) { lock(&host_lock); }
void ghal_hostlock_leave(void) { unlock(&host_lock); }

/* ------------------------------------------------------------------ */
/* ghal_irq, ghal_fault: not implemented yet                          */
/* ------------------------------------------------------------------ */

ghal_status ghal_irq_attach(int source, ghal_irq_handler fn, void *ctx,
                            ghal_irq_source **out)
{
    (void)source; (void)fn; (void)ctx;
    if (out)
        *out = NULL;
    return GHAL_ERR_UNSUPPORTED;
}

ghal_status ghal_irq_detach(ghal_irq_source *src)
{
    (void)src;
    return GHAL_ERR_UNSUPPORTED;
}

ghal_status ghal_irq_set_tick_rate(unsigned hz)
{
    (void)hz;
    return GHAL_ERR_UNSUPPORTED;
}

ghal_status ghal_fault_set_handler(ghal_fault_handler fn)
{
    (void)fn;
    return GHAL_ERR_UNSUPPORTED;
}

/* ------------------------------------------------------------------ */
/* ghal_log                                                           */
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

static int being_traced(void)
{
#ifdef __linux__
    char line[256];
    int traced = 0;
    FILE *f = fopen("/proc/self/status", "r");
    if (!f)
        return 0;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "TracerPid:", 10) == 0) {
            traced = atoi(line + 10) != 0;
            break;
        }
    }
    fclose(f);
    return traced;
#else
    return 0;
#endif
}

void ghal_debug_break(void)
{
    if (being_traced())
        raise(SIGTRAP);
}

/* ------------------------------------------------------------------ */
/* ghal_exit                                                          */
/* ------------------------------------------------------------------ */

void ghal_exit(int how)
{
    /* GHAL_EXIT_RESTART: the process exits with status 2; the start
     * script (or ghal_boot, once it exists) runs GEOS again. */
    fflush(NULL);
    exit(how == GHAL_EXIT_NORMAL ? 0 : how == GHAL_EXIT_RESTART ? 2 : 1);
}

/* ------------------------------------------------------------------ */
/* ghal_caps                                                          */
/* ------------------------------------------------------------------ */

const char *ghal_host_name(void)
{
#ifdef __linux__
    return "linux";
#else
    return "posix";
#endif
}

uint32_t ghal_caps(void)
{
    return GHAL_CAP_THREADS | GHAL_CAP_EXEC_PROTECT;
}
