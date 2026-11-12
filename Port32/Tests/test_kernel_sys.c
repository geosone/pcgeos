/*
 * test_kernel_sys.c -- the small system routines of the C kernel
 * (Library/Kernel/Sys/sysMisc.c, sysStats.c, sysError.c): number
 * strings, exit flags, the error-check level, the host environment,
 * SysGetInfo, SysStatistics and SysNotify.
 */
#define _DEFAULT_SOURCE		/* putenv (glibc) */
#define _GNU_SOURCE
#include "kernelC.h"
#include "ghal.h"
#include <system.h>
#include <sysstats.h>
#include <ec.h>
#include <localize.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static void Hex(dword v, UtilHexToAsciiFlags f, const char *want)
{
    char buf[UHTA_ALL_BUFFER_SIZE + 4];
    word n;

    memset(buf, 'x', sizeof(buf));
    n = UtilHex32ToAscii(buf, v, f | UHTAF_NULL_TERMINATE);
    if (strcmp(buf, want) != 0 || n != strlen(want)) {
	fails++;
	printf("FAIL UtilHex32ToAscii(%lu, %x): \"%s\" (%u), want \"%s\"\n",
	       (unsigned long) v, f, buf, n, want);
    }
}

static int AToH(const char *s, sdword *v)
{
    return UtilAsciiToHex32(s, v) ? -1 : 0;
}

static word notified;
static word Answer(word flags, const char *s1, const char *s2)
{
    notified++;
    CHECK(!strcmp(s1, "Disk") && s2 == NULL);
    return (flags & SNF_RETRY) ? SNF_RETRY : 0;
}

int main(void)
{
    static char env[] = "GEOSSYSTEST=a value";
    char buf[16], want[32];
    LocalNumericFormat lnf;
    sdword v;
    MemHandle mh;
    SysStats st;

    kernelCurrentGeode = 0x2340;
    KernelThreadInit(PRIORITY_STANDARD);

    /* number strings */
    Hex(0, 0, "0");
    Hex(1234567890UL, 0, "1234567890");
    Hex(4294967295UL, 0, "4294967295");
    Hex(42, UHTAF_INCLUDE_LEADING_ZEROS, "0000000042");
    Hex((dword) -42, UHTAF_SIGNED_VALUE, "-42");
    Hex((dword) -42, 0, "4294967254");
    LocalGetNumericFormat(&lnf);
    sprintf(want, "1%c234%c567", (char) lnf.thousandsSeparator,
	    (char) lnf.thousandsSeparator);
    Hex(1234567, UHTAF_THOUSANDS_SEPARATORS, want);
    sprintf(want, "-123%c456", (char) lnf.thousandsSeparator);
    Hex((dword) -123456, UHTAF_THOUSANDS_SEPARATORS | UHTAF_SIGNED_VALUE,
	want);
    Hex(999, UHTAF_THOUSANDS_SEPARATORS, "999");
    CHECK(UtilHex32ToAscii(buf, 77, 0) == 2 && buf[0] == '7');
    CHECK(AToH("123", &v) == 0 && v == 123);
    CHECK(AToH("+7", &v) == 0 && v == 7);
    CHECK(AToH("-2147483647", &v) == 0 && v == -2147483647L);
    CHECK(AToH("-2147483648", &v) < 0 && v == UATH_CONVERT_OVERFLOW);
							/* as the asm */
    CHECK(AToH("4294967295", &v) == 0 && v == -1);	/* unsigned too */
    CHECK(AToH("4294967296", &v) < 0 && v == UATH_CONVERT_OVERFLOW);
    CHECK(AToH("-2147483649", &v) < 0 && v == UATH_CONVERT_OVERFLOW);
    CHECK(AToH("12a", &v) < 0 && v == UATH_NON_NUMERIC_DIGIT_IN_STRING);
    CHECK(AToH("", &v) == 0 && v == 0);

    /* exit flags, error-check level */
    CHECK(SysSetExitFlags(EF_RESTART | EF_PANIC, 0) ==
	  (EF_RESTART | EF_PANIC));
    CHECK(SysSetExitFlags(0, EF_PANIC) == EF_RESTART);
    CHECK(SysSetExitFlags(0, EF_RESTART) == 0);
    SysSetECLevel(0x8000, 0x1230);
    CHECK(SysGetECLevel(&mh) == 0x8000 && mh == 0x1230);
    SysSetECLevel(0, 0);

    /* the host's environment */
    putenv(env);
    CHECK(!SysGetDosEnvironment("GEOSSYSTEST", buf, sizeof(buf)) &&
	  !strcmp(buf, "a value"));
    CHECK(!SysGetDosEnvironment("GEOSSYSTEST", buf, 4) &&
	  !strcmp(buf, "a v"));
    CHECK(SysGetDosEnvironment("GEOSSYSTESTNOT", buf, sizeof(buf)));
    CHECK(!SysGetPenMode());
    SysSetInkWidthAndHeight(0x0303);
    CHECK(SysGetInkWidthAndHeight() == 0x0303);

    /* SysGetInfo */
    CHECK(SysGetInfo(SGIT_TOTAL_HANDLES) >= HandleCountUsed());
    CHECK(SysGetInfo(SGIT_NUMBER_OF_FREE_HANDLES) ==
	  SysGetInfo(SGIT_TOTAL_HANDLES) - HandleCountUsed());
    CHECK(SysGetInfo(SGIT_TOTAL_GEODES) == 0);
    CHECK(SysGetInfo(SGIT_UI_PROCESS) == 0);
    CHECK(SysGetInfo(SGIT_ERROR) == 0xffff);
    CHECK(SysGetInfo(SGIT_HEAP_SIZE) == 0xffff);

    /* SysStatistics: the timer interrupt counts a second */
    KernelTimerInit();
    TimerSleep(130);
    SysStatistics(&st);
    CHECK(st.SS_interrupts >= 50 && st.SS_interrupts <= 70);
    CHECK(st.SS_idleCount > 0);
    CHECK(st.SS_swapOuts.SSI_blocks == 0);
    KernelTimerExit();

    /* SysNotify: the default answer, or the hook's */
    CHECK(SysNotify(SNF_RETRY | SNF_ABORT, "Disk", NULL) == SNF_ABORT);
    CHECK(SysNotify(SNF_RETRY | SNF_CONTINUE, "Disk", NULL) ==
	  SNF_CONTINUE);
    CHECK(SysNotify(SNF_RETRY, "Disk", NULL) == SNF_RETRY);
    kernelSysNotifyHook = Answer;
    CHECK(SysNotify(SNF_RETRY | SNF_ABORT, "Disk", NULL) == SNF_RETRY);
    CHECK(SysNotify(SNF_ABORT, "Disk", NULL) == SNF_ABORT);
    CHECK(notified == 2);
    kernelSysNotifyHook = NULL;

    printf("%s: %s: %d failure(s)\n", ghal_host_name(),
	   fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
