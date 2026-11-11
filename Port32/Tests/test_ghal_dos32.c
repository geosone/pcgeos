/*
 * test_ghal_dos32.c -- GHAL tests for the dos32 host only: CPU exceptions
 * through ghal_fault (DPMI exception handlers).  Open Watcom inline asm.
 */
#include "ghal.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond) do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

static jmp_buf fault_jmp;
static ghal_fault_info last_fault;
static int fault_count;

static void on_fault(const ghal_fault_info *info)
{
    last_fault = *info;
    fault_count++;
    longjmp(fault_jmp, 1);
}

static int divide(volatile int a, volatile int b)
{
    return a / b;
}

static void load_bad_selector(void)
{
    _asm {
        push es
        mov ax, 0fff8h          ; not a valid selector
        mov es, ax              ; -> general protection fault
        pop es
    }
}

static void illegal_opcode(void)
{
    _asm {
        db 0fh, 0bh             ; ud2
    }
}

static void breakpoint(void)
{
    _asm {
        int 3
    }
}

#define EXPECT_FAULT(call, k)                                   \
    do {                                                        \
        memset(&last_fault, 0, sizeof last_fault);              \
        if (setjmp(fault_jmp) == 0) {                           \
            call;                                               \
            CHECK(!#call " did not fault");                     \
        } else {                                                \
            CHECK(last_fault.kind == (k));                      \
            CHECK(last_fault.pc != NULL);                       \
        }                                                       \
    } while (0)

int main(void)
{
    volatile int r;
    CHECK(ghal_fault_set_handler(on_fault) == GHAL_OK);
    EXPECT_FAULT(r = divide(1, 0), GHAL_FAULT_DIVIDE);
    EXPECT_FAULT(load_bad_selector(), GHAL_FAULT_ACCESS);
    EXPECT_FAULT(illegal_opcode(), GHAL_FAULT_ILLEGAL);
    EXPECT_FAULT(breakpoint(), GHAL_FAULT_BREAKPOINT);
    EXPECT_FAULT(r = divide(7, 0), GHAL_FAULT_DIVIDE);  /* handler survives */
    printf("faults caught: %d\n", fault_count);
    CHECK(fault_count == 5);
    printf("%s: %s\n", ghal_host_name(), failures ? "FAILED" : "all dos32 GHAL tests passed");
    return failures ? 1 : 0;
}
