/*
 * test_ghal_posix.c -- GHAL tests that need POSIX (fork, threads, signals):
 * enforced code protection, ghal_crit against other threads, ghal_fault.
 * The host-independent tests are in test_ghal_core.c.
 */
#define _GNU_SOURCE 1

#include "ghal.h"

#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;

#define CHECK(cond) do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

/* Exit code of a child that hits a memory protection fault.  The child
 * catches the fault itself, so no real crash (and no core dump) happens. */
#define FAULT_EXIT 99

static void fault_exit(int sig)
{
    (void)sig;
    _exit(FAULT_EXIT);
}

/* Returns 1 if writing to p faults, 0 if it works, -1 on error. */
static int write_faults(volatile unsigned char *p)
{
    int status;
    pid_t pid = fork();
    if (pid == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = fault_exit;
        sigaction(SIGSEGV, &sa, NULL);  /* replaces sanitizer handlers too */
        sigaction(SIGBUS, &sa, NULL);
        p[0] = 1;
        _exit(0);
    }
    if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
        return -1;
    return WEXITSTATUS(status) == FAULT_EXIT ? 1
         : WEXITSTATUS(status) == 0 ? 0 : -1;
}

static void test_mem_protect(void)
{
    unsigned char *c = ghal_mem_alloc(5000, GHAL_MEM_PAGED);
    CHECK(c != NULL);
    CHECK(ghal_caps() & GHAL_CAP_EXEC_PROTECT);
    memset(c, 0xc3, 5000);                 /* "ret" */
    CHECK(write_faults(c) == 0);
    CHECK(ghal_mem_protect(c, GHAL_PROT_RX) == GHAL_OK);
    CHECK(c[4999] == 0xc3);                /* still readable */
    CHECK(write_faults(c) == 1);           /* no longer writable */
    c = ghal_mem_resize(c, 9000);          /* back to RW, content kept */
    CHECK(c != NULL && c[4999] == 0xc3 && ghal_mem_size(c) == 9000);
    CHECK(write_faults(c) == 0);
    ghal_mem_free(c);
}

static volatile int shared_counter;

static void *crit_worker(void *arg)
{
    (void)arg;
    ghal_crit_enter();
    shared_counter++;
    ghal_crit_leave();
    return NULL;
}

static void test_crit(void)
{
    pthread_t t;
    ghal_crit_enter();
    ghal_crit_enter();                     /* nestable */
    CHECK(pthread_create(&t, NULL, crit_worker, NULL) == 0);
    usleep(20000);
    CHECK(shared_counter == 0);            /* worker is held off */
    ghal_crit_leave();
    usleep(20000);
    CHECK(shared_counter == 0);            /* still nested once */
    ghal_crit_leave();
    pthread_join(t, NULL);
    CHECK(shared_counter == 1);
}

/* ---- ghal_fault ---- */

static sigjmp_buf fault_jmp;
static ghal_fault_info last_fault;

static void on_fault(const ghal_fault_info *info)
{
    last_fault = *info;
    siglongjmp(fault_jmp, 1);
}

#if defined(__x86_64__) || defined(__i386__)
#if defined(__GNUC__) || defined(__clang__)
__attribute__((no_sanitize("undefined")))
#endif
static int divide(volatile int a, volatile int b)
{
    return a / b;
}
#endif

static void test_fault(void)
{
    volatile unsigned char *code;
    CHECK(ghal_fault_set_handler(on_fault) == GHAL_OK);

    /* write to a read/execute block */
    code = ghal_mem_alloc(100, GHAL_MEM_PAGED);
    CHECK(code && ghal_mem_protect((void *)code, GHAL_PROT_RX) == GHAL_OK);
    memset(&last_fault, 0, sizeof last_fault);
    if (sigsetjmp(fault_jmp, 1) == 0) {
        code[5] = 1;
        CHECK(!"write to RX block did not fault");
    }
    CHECK(last_fault.kind == GHAL_FAULT_ACCESS);
    CHECK(last_fault.address == (void *)(code + 5));
#if defined(__x86_64__) || defined(__i386__) || defined(__aarch64__) || defined(__arm__)
    CHECK(last_fault.pc != NULL);
#endif
    ghal_mem_free((void *)code);

#if defined(__x86_64__) || defined(__i386__)
    /* integer divide error (ARM has no such trap) */
    memset(&last_fault, 0, sizeof last_fault);
    if (sigsetjmp(fault_jmp, 1) == 0) {
        volatile int r = divide(1, 0);
        (void)r;
        CHECK(!"divide by zero did not fault");
    }
    CHECK(last_fault.kind == GHAL_FAULT_DIVIDE);
#endif

    /* breakpoint */
    memset(&last_fault, 0, sizeof last_fault);
    if (sigsetjmp(fault_jmp, 1) == 0) {
        raise(SIGTRAP);
        CHECK(!"SIGTRAP did not reach the handler");
    }
    CHECK(last_fault.kind == GHAL_FAULT_BREAKPOINT);

    /* the handler survives repeated faults */
    if (sigsetjmp(fault_jmp, 1) == 0)
        *(volatile int *)16 = 0;
    CHECK(last_fault.kind == GHAL_FAULT_ACCESS && last_fault.address == (void *)16);
}

int main(void)
{
    CHECK(ghal_caps() & GHAL_CAP_THREADS);
    test_mem_protect();
    test_crit();
    test_fault();
    printf("%s: %s\n", ghal_host_name(), failures ? "FAILED" : "all POSIX GHAL tests passed");
    return failures ? 1 : 0;
}
