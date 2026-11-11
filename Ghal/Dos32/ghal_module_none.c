/*
 * ghal_module_none.c -- ghal_module for a dos32 build without a module
 * loader (Open Watcom: geodes are DXE3 modules, which only the DJGPP build
 * loads).  Loading fails with GHAL_ERR_UNSUPPORTED, so a kernel built
 * with Watcom links but loads no geodes.
 */
#include "ghal.h"

ghal_status ghal_module_load(const void *image, size_t size,
                             ghal_module_import import, void *ctx,
                             ghal_module **out)
{
    (void)image; (void)size; (void)import; (void)ctx;
    *out = NULL;
    return GHAL_ERR_UNSUPPORTED;
}

void *ghal_module_symbol(const ghal_module *m, const char *name)
{
    (void)m; (void)name;
    return NULL;
}

void ghal_module_unload(ghal_module *m)
{
    (void)m;
}

const char *ghal_module_format(void)
{
    return "none";
}
