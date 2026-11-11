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
    GHAL_ERR_HOST,          /* host call failed for another reason */
    GHAL_ERR_FORMAT,        /* not a module of this host's format */
    GHAL_ERR_UNRESOLVED,    /* a module imports a name nobody provides */
    GHAL_ERR_NOTFOUND,      /* no such file or directory */
    GHAL_ERR_EXISTS,        /* it exists already */
    GHAL_ERR_ACCESS,        /* access denied, read-only, in use */
    GHAL_ERR_NOSPACE,       /* the disk is full */
    GHAL_ERR_NOTEMPTY,      /* directory not empty */
    GHAL_ERR_ISDIR,         /* a directory where a file was expected */
    GHAL_ERR_NOTDIR,        /* a file where a directory was expected */
    GHAL_ERR_END            /* no more entries (ghal_fs_readdir) */
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
/* The same for the pages [offset, offset + len) of a paged block; offset
 * must be a multiple of the page size, len is rounded up. */
ghal_status ghal_mem_protect_range(void *block, size_t offset, size_t len,
                                   unsigned prot);
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
 * ghal_ctx -- execution contexts for GEOS threads (GHAL-Concept.md 4.1).
 *
 * The GEOS scheduler stays in the kernel; the GHAL only runs contexts.
 * Exactly one context runs at a time: ghal_ctx_switch stops the caller
 * and runs the target.  dos32: own stacks and a context switch.  Hosts
 * with threads: one host thread per context, handed a baton.
 *
 * ghal_ctx_switch may be called inside ghal_crit.  The nesting of
 * ghal_crit belongs to the context: the target continues with the depth
 * it had when it switched away (a new context starts with depth 0), the
 * caller gets its own back when it runs again.
 * ------------------------------------------------------------------- */

typedef struct ghal_ctx ghal_ctx;
typedef void (*ghal_ctx_entry)(void *arg);

/* The running context.  The first call makes one for the program's own
 * thread.  Not from a ghal_irq handler. */
ghal_ctx   *ghal_ctx_self(void);
/* A new context that starts in fn(arg) when it is first switched to.
 * fn must not return; it ends with ghal_ctx_exit.  stack_size 0: the
 * default (64K at least). */
ghal_status ghal_ctx_create(ghal_ctx_entry fn, void *arg, size_t stack_size,
                            ghal_ctx **out);
/* Stop the caller and run `to' (not the caller).  Returns when another
 * context switches back. */
void        ghal_ctx_switch(ghal_ctx *to);
/* End the calling context and run `to'.  Another context releases the
 * ended one with ghal_ctx_destroy. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void        ghal_ctx_exit(ghal_ctx *to);
/* Release a context that has ended or never ran (not the running one). */
void        ghal_ctx_destroy(ghal_ctx *c);

/* Called inside ghal_crit when no GEOS thread can run: wait for the next
 * asynchronous event (a ghal_irq handler) with the critical section
 * released, return inside it.  May return early. */
void        ghal_idle(void);

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
/* Non-zero while the caller runs a ghal_irq handler (the kernel must
 * not switch contexts there; it reschedules when the handler is done). */
int         ghal_irq_active(void);

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
 * ghal_module -- load code (geodes).  GHAL-Concept.md section 4.4.
 *
 * The kernel reads the file and passes its contents; the GHAL lays it out
 * in memory, resolves its imports through `import' and relocates it.  The
 * format is the host's own:
 *   linux, gonware: ELF shared object (ET_DYN), one per architecture;
 *   dos32:          DXE3, the module format of DJGPP (dxe3gen).
 * Names are C names on every host (the leading underscore of DJGPP's
 * COFF symbols is not part of them).  A module has no host C library:
 * all it imports comes from `import' (the kernel and other geodes).
 * ------------------------------------------------------------------- */

typedef struct ghal_module ghal_module;
/* Address of an imported name, or NULL if unknown. */
typedef void *(*ghal_module_import)(void *ctx, const char *name);

/* Load a module from image[0..size); the image may be freed afterwards.
 * Runs the module's initializers.  On GHAL_ERR_UNRESOLVED the missing
 * name is logged (ghal_log). */
ghal_status ghal_module_load(const void *image, size_t size,
                             ghal_module_import import, void *ctx,
                             ghal_module **out);
/* Address of a name the module exports, or NULL. */
void       *ghal_module_symbol(const ghal_module *m, const char *name);
/* Run the module's finalizers and release it. */
void        ghal_module_unload(ghal_module *m);
/* "elf" or "dxe3" */
const char *ghal_module_format(void);

/* ---------------------------------------------------------------------
 * ghal_fs -- files and directories of the host, for the HostFS file
 * system driver (GHAL-Concept.md section 4).  Paths are host paths:
 * '/'-separated on POSIX, DOS paths ('\\' or '/') on dos32, where names
 * are 8.3.  Calls may block the calling context; they are made outside
 * ghal_crit.
 * ------------------------------------------------------------------- */
typedef struct ghal_file ghal_file;
typedef struct ghal_dir ghal_dir;

/* ghal_fs_open flags */
#define GHAL_FS_READ        0x0001u
#define GHAL_FS_WRITE       0x0002u
#define GHAL_FS_CREATE      0x0004u /* create if missing */
#define GHAL_FS_TRUNCATE    0x0008u /* make empty */
#define GHAL_FS_EXCL        0x0010u /* with CREATE: fail if it exists */

/* ghal_fs_info attrs (the DOS attribute bits) */
#define GHAL_FS_ATTR_READONLY 0x01u
#define GHAL_FS_ATTR_HIDDEN   0x02u
#define GHAL_FS_ATTR_SYSTEM   0x04u
#define GHAL_FS_ATTR_DIR      0x10u
#define GHAL_FS_ATTR_ARCHIVE  0x20u

typedef struct ghal_fs_info {
    uint64_t      size;
    ghal_datetime mtime;            /* local time */
    unsigned      attrs;            /* GHAL_FS_ATTR_* */
} ghal_fs_info;

/* seek origins */
#define GHAL_FS_SEEK_SET    0
#define GHAL_FS_SEEK_CUR    1
#define GHAL_FS_SEEK_END    2

ghal_status ghal_fs_open(const char *path, unsigned flags, ghal_file **out);
ghal_status ghal_fs_close(ghal_file *f);
/* *done: bytes read (less at the end of the file) or written */
ghal_status ghal_fs_read(ghal_file *f, void *buf, size_t n, size_t *done);
ghal_status ghal_fs_write(ghal_file *f, const void *buf, size_t n,
                          size_t *done);
ghal_status ghal_fs_seek(ghal_file *f, int64_t offset, int whence,
                         uint64_t *pos);
ghal_status ghal_fs_truncate(ghal_file *f, uint64_t size);
ghal_status ghal_fs_sync(ghal_file *f);
ghal_status ghal_fs_fstat(ghal_file *f, ghal_fs_info *info);

ghal_status ghal_fs_stat(const char *path, ghal_fs_info *info);
/* the attributes GHAL_FS_ATTR_READONLY, HIDDEN, SYSTEM, ARCHIVE; on
 * POSIX only READONLY means something */
ghal_status ghal_fs_set_attrs(const char *path, unsigned attrs);
ghal_status ghal_fs_set_mtime(const char *path, const ghal_datetime *dt);
ghal_status ghal_fs_delete(const char *path);
ghal_status ghal_fs_rename(const char *from, const char *to);
ghal_status ghal_fs_mkdir(const char *path);
ghal_status ghal_fs_rmdir(const char *path);

/* the entries of a directory, without "." and "..";
 * ghal_fs_readdir returns GHAL_ERR_END after the last one */
ghal_status ghal_fs_opendir(const char *path, ghal_dir **out);
ghal_status ghal_fs_readdir(ghal_dir *d, char *name, size_t size,
                            ghal_fs_info *info);
void        ghal_fs_closedir(ghal_dir *d);

/* Named attributes of a file or directory (POSIX: the extended attribute
 * "user.geos.<name>"); GHAL_ERR_UNSUPPORTED where the host has none
 * (dos32, file systems without xattrs).  ghal_fs_xattr_get: *size in:
 * the buffer, out: the attribute's size; GHAL_ERR_NOTFOUND if it is not
 * set. */
ghal_status ghal_fs_xattr_get(const char *path, const char *name, void *buf,
                              size_t *size);
ghal_status ghal_fs_xattr_set(const char *path, const char *name,
                              const void *buf, size_t size);
ghal_status ghal_fs_xattr_remove(const char *path, const char *name);

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
