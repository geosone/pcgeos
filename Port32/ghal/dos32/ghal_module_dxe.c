/*
 * ghal_module_dxe.c -- ghal_module for dos32: DXE3 modules.
 *
 * DXE3 is the dynamic module format of DJGPP, written by dxe3gen
 * (cmake/GeosGeode.cmake builds geodes with "dxe3gen -U").  dxe3gen has
 * already linked the module: what is left is one block of code, data and
 * bss, a list of places that need the load address, and per imported name
 * the places that need its absolute or PC-relative address.  This loader
 * reads that from memory; it does not use DJGPP's dlopen (no files, no
 * search path, imports come from the kernel's import function).  Layout
 * (DJGPP include/sys/dxe.h):
 *
 *   header (dxe3_header)
 *   exports    { long offset; char name[]; } [n_exp_syms]
 *   section    code + data (+ bss, not in the file)
 *   deps       { char name[]; } [n_deps]          -- must be empty
 *   unresolved { ushort n_rel, n_abs; char name[];
 *                long rel[n_rel]; long abs[n_abs]; } [n_unres_syms]
 *   relocs     long [nrelocs]                     -- add the load address
 *
 * Names in the file are COFF names with a leading underscore; the API
 * uses C names.  Port32/GHAL-Concept.md section 4.4.
 */
#include "ghal.h"

#include <stdint.h>
#include <string.h>

#define DXE_MAGIC 0x31455844u           /* "DXE1" */

typedef struct {
    uint32_t magic, symbol_offset;
    int32_t  element_size;
    uint32_t nrelocs, hdr_size;
    char     major, minor;
    int16_t  flags;
    uint32_t reloc_table;
    uint32_t n_exp_syms, exp_table, exp_size;
    uint32_t n_deps, dep_table, dep_size;
    uint32_t n_unres_syms, unres_table, unres_size;
    uint32_t sec_size, sec_f_size;
    int32_t  init, fini;
} __attribute__((packed)) dxe3_header;

struct ghal_module {
    unsigned char *section;     /* paged GHAL block */
    unsigned char *exports;     /* copy of the export table */
    uint32_t       exp_size, n_exp;
    int32_t        fini;
};

typedef void (*init_fn)(void);

static int32_t get32(const unsigned char *p)
{
    int32_t v;
    memcpy(&v, p, 4);
    return v;
}

static int fix_ok(const ghal_module *m, uint32_t sec_size, int32_t off)
{
    (void)m;
    return off >= 0 && (uint32_t)off <= sec_size - 4;
}

ghal_status ghal_module_load(const void *image, size_t size,
                             ghal_module_import import, void *ctx,
                             ghal_module **out)
{
    const unsigned char *img = image, *p, *end;
    dxe3_header h;
    ghal_module *m;
    uint32_t i, j;

    if (!image || !out || size < sizeof(h))
        return GHAL_ERR_INVAL;
    *out = NULL;
    memcpy(&h, img, sizeof(h));
    if (h.magic != DXE_MAGIC || h.element_size != -1 || h.major > 1 ||
        h.n_deps != 0 || h.sec_size < 4 || h.sec_f_size > h.sec_size ||
        h.symbol_offset > size || h.sec_f_size > size - h.symbol_offset ||
        h.exp_table > h.symbol_offset ||
        h.exp_size > h.symbol_offset - h.exp_table ||
        h.unres_table > size || h.unres_size > size - h.unres_table ||
        h.reloc_table > size || h.nrelocs > (size - h.reloc_table) / 4)
        return GHAL_ERR_FORMAT;

    m = ghal_mem_alloc(sizeof(*m), GHAL_MEM_ZERO);
    if (!m)
        return GHAL_ERR_NOMEM;
    m->section = ghal_mem_alloc(h.sec_size, GHAL_MEM_PAGED | GHAL_MEM_ZERO);
    m->exports = ghal_mem_alloc(h.exp_size ? h.exp_size : 1, 0);
    if (!m->section || !m->exports)
        goto nomem;
    memcpy(m->section, img + h.symbol_offset, h.sec_f_size);
    memcpy(m->exports, img + h.exp_table, h.exp_size);
    m->exp_size = h.exp_size;
    m->n_exp = h.n_exp_syms;
    m->fini = h.fini;

    /* imports: absolute places get the address added, relative places
     * get the displacement from the end of the field */
    p = img + h.unres_table;
    end = p + h.unres_size;
    for (i = 0; i < h.n_unres_syms; i++) {
        uint16_t n_rel, n_abs;
        const char *name;
        const unsigned char *fix;
        size_t len;
        void *addr;

        if (end - p < 5)
            goto bad_format;
        memcpy(&n_rel, p, 2);
        memcpy(&n_abs, p + 2, 2);
        name = (const char *)p + 4;
        len = strnlen(name, (size_t)(end - p) - 4);
        if (4 + len + 1 + 4 * ((size_t)n_rel + n_abs) > (size_t)(end - p))
            goto bad_format;
        fix = p + 4 + len + 1;
        if (name[0] == '_')
            name++;             /* COFF name -> C name */
        addr = import ? import(ctx, name) : NULL;
        if (!addr) {
            ghal_log(GHAL_LOG_ERROR, "ghal_module: unresolved import %s",
                     name);
            ghal_mem_free(m->exports);
            ghal_mem_free(m->section);
            ghal_mem_free(m);
            return GHAL_ERR_UNRESOLVED;
        }
        for (j = 0; j < (uint32_t)n_rel + n_abs; j++) {
            int32_t off = get32(fix + 4 * j);
            unsigned char *at;
            int32_t v;
            if (!fix_ok(m, h.sec_size, off))
                goto bad_format;
            at = m->section + off;
            if (j < n_rel)
                v = (int32_t)((uintptr_t)addr - (uintptr_t)at - 4);
            else
                v = get32(at) + (int32_t)(uintptr_t)addr;
            memcpy(at, &v, 4);
        }
        p = fix + 4 * ((size_t)n_rel + n_abs);
    }

    /* base relocations */
    for (i = 0; i < h.nrelocs; i++) {
        int32_t off = get32(img + h.reloc_table + 4 * i), v;
        if (!fix_ok(m, h.sec_size, off))
            goto bad_format;
        v = get32(m->section + off) + (int32_t)(uintptr_t)m->section;
        memcpy(m->section + off, &v, 4);
    }

    if (h.init != -1)
        ((init_fn)(m->section + h.init))();
    *out = m;
    return GHAL_OK;

bad_format:
    ghal_mem_free(m->exports);
    ghal_mem_free(m->section);
    ghal_mem_free(m);
    return GHAL_ERR_FORMAT;
nomem:
    ghal_mem_free(m->exports);
    ghal_mem_free(m->section);
    ghal_mem_free(m);
    return GHAL_ERR_NOMEM;
}

void *ghal_module_symbol(const ghal_module *m, const char *name)
{
    const unsigned char *p, *end;
    uint32_t i;

    if (!m || !name)
        return NULL;
    p = m->exports;
    end = p + m->exp_size;
    for (i = 0; i < m->n_exp && end - p > 4; i++) {
        const char *s = (const char *)p + 4;
        size_t len = strnlen(s, (size_t)(end - p) - 4);
        const char *c = s[0] == '_' ? s + 1 : s;
        if (strcmp(c, name) == 0)
            return m->section + get32(p);
        p += 4 + len + 1;
    }
    return NULL;
}

void ghal_module_unload(ghal_module *m)
{
    if (!m)
        return;
    if (m->fini != -1)
        ((init_fn)(m->section + m->fini))();
    ghal_mem_free(m->exports);
    ghal_mem_free(m->section);
    ghal_mem_free(m);
}

const char *ghal_module_format(void)
{
    return "dxe3";
}
