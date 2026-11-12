/*
 * sysError.c -- fatal errors of the C kernel (after sysError.asm).
 *
 * The asm kernel shows a fatal error in a system error box and offers to
 * reboot or enter the debugger.  The C kernel does not have its screen
 * yet: it logs the error by name through the GHAL, stops in the host
 * debugger if one is attached and ends GEOS.  Tests catch errors with
 * kernelFatalHook.
 *
 * SysNotify has no box to show either: it logs both strings and picks an
 * answer that needs no user (kernelSysNotifyHook may answer instead):
 * abort, else continue, else retry, else exit, else reboot.  Exit asks
 * for a forced clean shutdown and returns SNF_EXIT; reboot ends GEOS and
 * does not return (only the boot thread comes back).
 */
#include "kernelC.h"
#include "ghal.h"
#include <system.h>

KernelFatalHook *kernelFatalHook;

#define KFE_NAME(name) #name,
static const char *const names[KFE_COUNT] = {
    "KFE_NONE",
    KERNEL_FATAL_ERRORS(KFE_NAME)
};

const char *KernelFatalErrorName(KernelFatalError code)
{
    return (unsigned) code < KFE_COUNT ? names[code] : "?";
}

void KernelFatal(KernelFatalError code)
{
    if (kernelFatalHook)
	kernelFatalHook(code);
    ghal_log(GHAL_LOG_ERROR, "GEOS fatal error %s (%d)",
	     KernelFatalErrorName(code), (int) code);
    ghal_debug_break();
    ghal_exit(GHAL_EXIT_ERROR);
}

KernelSysNotifyHook *kernelSysNotifyHook;

word SysNotify(SysNotifyFlags flags, const char *string1, const char *string2)
{
    static const word order[] = {
	SNF_ABORT, SNF_CONTINUE, SNF_RETRY, SNF_EXIT, SNF_REBOOT
    };
    word answer = 0, i;

    ghal_log(GHAL_LOG_WARN, "GEOS: %s%s%s", string1 ? string1 : "",
	     string1 && string2 ? " " : "", string2 ? string2 : "");
    if (kernelSysNotifyHook)
	answer = kernelSysNotifyHook(flags, string1, string2);
    for (i = 0; !answer && i < sizeof(order) / sizeof(order[0]); i++)
	if (flags & order[i])
	    answer = order[i];
    if (answer == SNF_EXIT)
	(void) SysShutdown(SST_CLEAN_FORCED);
    else if (answer == SNF_REBOOT)
	(void) SysShutdown(SST_REBOOT);
    return answer;
}
