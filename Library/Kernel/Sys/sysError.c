/*
 * sysError.c -- fatal errors of the C kernel (after sysError.asm).
 *
 * The asm kernel shows a fatal error in a system error box and offers to
 * reboot or enter the debugger.  The C kernel does not have its screen
 * yet: it logs the error by name through the GHAL, stops in the host
 * debugger if one is attached and ends GEOS.  Tests catch errors with
 * kernelFatalHook.
 */
#include "kernelC.h"
#include "ghal.h"

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
