/*
 * test_ghal.c -- host-side tests of the GHAL core (Port32/ghal).
 */
#define _GNU_SOURCE 1

#include "ghal.h"

#include <pthread.h>
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

static int all_zero(const unsigned char *p, size_t n)
{
    while (n--)
        if (*p++)
            return 0;
    return 1;
}

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

/* Returns the signal that ended a child writing to p, or 0. */
static int write_in_child(volatile unsigned char *p)
{
    int status;
    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGSEGV, SIG_DFL);   /* sanitizers install their own */
        signal(SIGBUS, SIG_DFL);
        p[0] = 1;
        _exit(0);
    }
    if (pid < 0 || waitpid(pid, &status, 0) != pid)
        return -1;
    return WIFSIGNALED(status) ? WTERMSIG(status) : 0;
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
    memset(c, 0xc3, 5000);                 /* "ret" */
    CHECK(write_in_child(c) == 0);
    CHECK(ghal_mem_protect(c, GHAL_PROT_RX) == GHAL_OK);
    CHECK(c[4999] == 0xc3);                /* still readable */
    CHECK(write_in_child(c) == SIGSEGV);   /* no longer writable */
    c = ghal_mem_resize(c, 9000);          /* back to RW, content kept */
    CHECK(c != NULL && c[4999] == 0xc3 && ghal_mem_size(c) == 9000);
    CHECK(write_in_child(c) == 0);
    CHECK(ghal_mem_protect(c, 8) == GHAL_ERR_INVAL);
    ghal_mem_free(c);
}

static void test_time(void)
{
    ghal_datetime dt;
    uint64_t t0 = ghal_time_monotonic_ns(), t1;
    usleep(20000);
    t1 = ghal_time_monotonic_ns();
    CHECK(t1 >= t0 + 15000000u);
    CHECK(ghal_time_get_local(&dt) == GHAL_OK);
    CHECK(dt.year >= 2024 && dt.month >= 1 && dt.month <= 12);
    CHECK(dt.day >= 1 && dt.day <= 31 && dt.hour < 24 && dt.weekday < 7);
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
    ghal_hostlock_enter();
    ghal_hostlock_leave();
}

static void test_caps(void)
{
    ghal_irq_source *src = (ghal_irq_source *)1;
    CHECK(ghal_host_name() && ghal_host_name()[0]);
    CHECK(ghal_caps() & GHAL_CAP_THREADS);
    CHECK(ghal_irq_attach(GHAL_IRQ_TICK, NULL, NULL, &src) == GHAL_ERR_UNSUPPORTED);
    CHECK(src == NULL);
}

int main(void)
{
    test_mem_plain();
    test_mem_paged();
    test_time();
    test_crit();
    test_caps();
    printf("%s: %s\n", ghal_host_name(), failures ? "FAILED" : "all GHAL tests passed");
    return failures ? 1 : 0;
}
