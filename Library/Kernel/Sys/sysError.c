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

static const char *const names[KFE_COUNT] = {
    "KFE_NONE",
    "ILLEGAL_HANDLE",
    "HANDLE_TABLE_FULL",
    "TOO_MANY_LOCKS",
    "BAD_UNLOCK",
    "HANDLE_FREE",
    "NOT_MEM_HANDLE",
    "ALLOC_ZERO",
    "ALLOC_TOO_LARGE",
    "REALLOC_TOO_LARGE",
    "ALLOC_BAD_FLAGS",
    "MEM_GET_INFO_BAD_PARAMETER",
    "FREEING_FREE_HANDLE",
    "MEM_REFERENCE_COUNT_ILLEGAL_INITIAL_VALUE",
    "MEM_REFERENCE_COUNT_OVERFLOW",
    "MEM_REFERENCE_COUNT_UNDERFLOW",
    "CANNOT_CHANGE_OWNER",
    "HAF_NO_ERR_ALLOC_FAILED",
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
