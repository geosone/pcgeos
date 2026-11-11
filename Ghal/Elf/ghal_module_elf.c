/*
 * ghal_module_elf.c -- ghal_module for ELF hosts (linux; later gonware).
 *
 * A geode is an ELF shared object (ET_DYN) of the host architecture,
 * linked with -shared -nostdlib -z now (cmake/GeosGeode.cmake).  The
 * loader lays out its PT_LOAD segments in one paged GHAL block, resolves
 * the dynamic relocations (imports through the kernel's import function,
 * never through the host's dynamic linker), protects the segments and
 * runs DT_INIT_ARRAY.  Only the relocation types a static linker leaves
 * in a shared object are handled:
 *
 *   amd64    R_X86_64_64, GLOB_DAT, JUMP_SLOT, RELATIVE
 *   i386     R_386_32, PC32, GLOB_DAT, JMP_SLOT, RELATIVE
 *   arm64    R_AARCH64_ABS64, GLOB_DAT, JUMP_SLOT, RELATIVE
 *   armhf    R_ARM_ABS32, REL32, GLOB_DAT, JUMP_SLOT, RELATIVE
 *
 * No TLS, no IFUNC, no DT_NEEDED (a geode does not use host libraries).
 * The ELF structures are defined here, so the file needs no <elf.h>.
 * Port32/GHAL-Concept.md section 4.4.
 */
#include "ghal.h"

#include <stdint.h>
#include <string.h>

/* --- ELF definitions (System V gABI) ---------------------------------- */

#if UINTPTR_MAX > 0xffffffffu
#define ELF_CLASS 2                     /* ELFCLASS64 */
typedef uint64_t Addr;
typedef uint64_t Off;
typedef uint64_t Xword;
typedef int64_t Sxword;
typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    Addr e_entry;
    Off e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Ehdr;
typedef struct {
    uint32_t p_type, p_flags;
    Off p_offset;
    Addr p_vaddr, p_paddr;
    Xword p_filesz, p_memsz, p_align;
} Phdr;
typedef struct {
    uint32_t st_name;
    unsigned char st_info, st_other;
    uint16_t st_shndx;
    Addr st_value;
    Xword st_size;
} Sym;
typedef struct { Sxword d_tag; Xword d_val; } Dyn;
typedef struct { Addr r_offset; Xword r_info; } Rel;
typedef struct { Addr r_offset; Xword r_info; Sxword r_addend; } Rela;
#define R_SYM(i)  ((uint32_t)((i) >> 32))
#define R_TYPE(i) ((uint32_t)(i))
#else
#define ELF_CLASS 1                     /* ELFCLASS32 */
typedef uint32_t Addr;
typedef uint32_t Off;
typedef uint32_t Xword;
typedef int32_t Sxword;
typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    Addr e_entry;
    Off e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Ehdr;
typedef struct {
    uint32_t p_type;
    Off p_offset;
    Addr p_vaddr, p_paddr;
    uint32_t p_filesz, p_memsz, p_flags, p_align;
} Phdr;
typedef struct {
    uint32_t st_name;
    Addr st_value;
    uint32_t st_size;
    unsigned char st_info, st_other;
    uint16_t st_shndx;
} Sym;
typedef struct { Sxword d_tag; Xword d_val; } Dyn;
typedef struct { Addr r_offset; Xword r_info; } Rel;
typedef struct { Addr r_offset; Xword r_info; Sxword r_addend; } Rela;
#define R_SYM(i)  ((i) >> 8)
#define R_TYPE(i) ((unsigned char)(i))
#endif

#define ET_DYN 3
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_TLS 7
#define PT_GNU_RELRO 0x6474e552
#define PF_X 1
#define PF_W 2
#define PF_R 4
#define SHN_UNDEF 0
#define STB_LOCAL 0
#define STB_WEAK 2
#define ST_BIND(i) ((i) >> 4)

enum {
    DT_NULL = 0, DT_NEEDED = 1, DT_PLTRELSZ = 2, DT_HASH = 4, DT_STRTAB = 5,
    DT_SYMTAB = 6, DT_RELA = 7, DT_RELASZ = 8, DT_RELAENT = 9, DT_SYMENT = 11,
    DT_REL = 17, DT_RELSZ = 18, DT_RELENT = 19, DT_PLTREL = 20,
    DT_JMPREL = 23, DT_INIT_ARRAY = 25, DT_FINI_ARRAY = 26,
    DT_INIT_ARRAYSZ = 27, DT_FINI_ARRAYSZ = 28
};
#define DT_GNU_HASH 0x6ffffef5

#if defined(__x86_64__)
#define ELF_MACHINE 62                  /* EM_X86_64 */
#define ELF_USE_RELA 1
enum { R_ABS = 1, R_PC = -1, R_GLOB_DAT = 6, R_JUMP_SLOT = 7, R_RELATIVE = 8 };
#elif defined(__i386__)
#define ELF_MACHINE 3                   /* EM_386 */
#define ELF_USE_RELA 0
enum { R_ABS = 1, R_PC = 2, R_GLOB_DAT = 6, R_JUMP_SLOT = 7, R_RELATIVE = 8 };
#elif defined(__aarch64__)
#define ELF_MACHINE 183                 /* EM_AARCH64 */
#define ELF_USE_RELA 1
enum { R_ABS = 257, R_PC = -1, R_GLOB_DAT = 1025, R_JUMP_SLOT = 1026,
       R_RELATIVE = 1027 };
#elif defined(__arm__)
#define ELF_MACHINE 40                  /* EM_ARM */
#define ELF_USE_RELA 0
enum { R_ABS = 2, R_PC = 3, R_GLOB_DAT = 21, R_JUMP_SLOT = 22,
       R_RELATIVE = 23 };
#else
#error "ghal_module_elf: unknown architecture"
#endif

/* --- the module ------------------------------------------------------- */

struct ghal_module {
    unsigned char *base;        /* paged GHAL block; ELF address 0 */
    size_t         span;        /* bytes of the block in use */
    const Sym     *symtab;
    const char    *strtab;
    uint32_t       nsyms;
    Addr           fini_array;
    size_t         fini_count;
};

typedef void (*init_fn)(void);

static const Dyn *dyn_find(const Dyn *dyn, Sxword tag)
{
    for (; dyn->d_tag != DT_NULL; dyn++)
        if (dyn->d_tag == tag)
            return dyn;
    return NULL;
}

/* number of dynamic symbols: DT_HASH has it, DT_GNU_HASH only implies it */
static uint32_t count_syms(const unsigned char *base, const Dyn *dyn)
{
    const Dyn *d = dyn_find(dyn, DT_HASH);
    const uint32_t *gh, *buckets, *chain;
    uint32_t nbuckets, symoffset, bloom, i, max = 0;

    if (d)
        return ((const uint32_t *)(base + d->d_val))[1];
    d = dyn_find(dyn, DT_GNU_HASH);
    if (!d)
        return 0;
    gh = (const uint32_t *)(base + d->d_val);
    nbuckets = gh[0];
    symoffset = gh[1];
    bloom = gh[2];
    buckets = (const uint32_t *)((const unsigned char *)(gh + 4) +
                                 bloom * sizeof(Addr));
    for (i = 0; i < nbuckets; i++)
        if (buckets[i] > max)
            max = buckets[i];
    if (max < symoffset)
        return symoffset;
    chain = buckets + nbuckets;
    while (!(chain[max - symoffset] & 1))
        max++;
    return max + 1;
}

static int sym_defined(const Sym *s)
{
    return s->st_shndx != SHN_UNDEF && ST_BIND(s->st_info) != STB_LOCAL;
}

/* value of symbol `index' for a relocation; 0 and *ok = 0 if unresolved */
static uintptr_t sym_value(ghal_module *m, uint32_t index,
                           ghal_module_import import, void *ctx, int *ok)
{
    const Sym *s;
    const char *name;
    void *p;

    if (index == 0)
        return 0;
    s = &m->symtab[index];
    if (s->st_shndx != SHN_UNDEF)
        return (uintptr_t)m->base + s->st_value;
    name = m->strtab + s->st_name;
    p = import ? import(ctx, name) : NULL;
    if (!p && ST_BIND(s->st_info) != STB_WEAK) {
        ghal_log(GHAL_LOG_ERROR, "ghal_module: unresolved import %s", name);
        *ok = 0;
    }
    return (uintptr_t)p;
}

static int relocate(ghal_module *m, Addr off, Xword info, Sxword addend,
                    int has_addend, ghal_module_import import, void *ctx)
{
    uintptr_t *where;
    uintptr_t s;
    uint32_t type = R_TYPE(info);
    int ok = 1;

    if (off > m->span - sizeof(uintptr_t))
        return 0;
    where = (uintptr_t *)(m->base + off);
    if (!has_addend)
        addend = (Sxword)*where;        /* REL: the addend is in place */
    if (type == 0)
        return 1;                       /* R_*_NONE */
    if ((int)type == R_RELATIVE) {
        *where = (uintptr_t)m->base + (uintptr_t)addend;
        return 1;
    }
    s = sym_value(m, R_SYM(info), import, ctx, &ok);
    if (!ok)
        return 0;
    if ((int)type == R_GLOB_DAT || (int)type == R_JUMP_SLOT)
        *where = s;                     /* the addend is ignored */
    else if ((int)type == R_ABS)
        *where = s + (uintptr_t)addend;
    else if ((int)type == R_PC)
        *where = s + (uintptr_t)addend - (uintptr_t)where;
    else {
        ghal_log(GHAL_LOG_ERROR, "ghal_module: relocation type %u",
                 (unsigned)type);
        return 0;
    }
    return 1;
}

static int relocate_table(ghal_module *m, Addr table, Xword size, int rela,
                          ghal_module_import import, void *ctx)
{
    Xword i, n;

    if (table == 0 || size == 0)
        return 1;
    if (table > m->span || size > m->span - table)
        return 0;
    if (rela) {
        const Rela *r = (const Rela *)(m->base + table);
        for (i = 0, n = size / sizeof(Rela); i < n; i++)
            if (!relocate(m, r[i].r_offset, r[i].r_info, r[i].r_addend, 1,
                          import, ctx))
                return 0;
    } else {
        const Rel *r = (const Rel *)(m->base + table);
        for (i = 0, n = size / sizeof(Rel); i < n; i++)
            if (!relocate(m, r[i].r_offset, r[i].r_info, 0, 0, import, ctx))
                return 0;
    }
    return 1;
}

static Xword dyn_val(const Dyn *dyn, Sxword tag)
{
    const Dyn *d = dyn_find(dyn, tag);
    return d ? d->d_val : 0;
}

static unsigned prot_of(uint32_t pf)
{
    return ((pf & PF_R) ? GHAL_PROT_R : 0) | ((pf & PF_W) ? GHAL_PROT_W : 0) |
           ((pf & PF_X) ? GHAL_PROT_X : 0);
}

/* protect each page with the union of the segments on it, RELRO pages
 * read-only; returns 0 on failure */
static int protect(ghal_module *m, const Phdr *ph, unsigned phnum,
                   size_t page)
{
    size_t npages = (m->span + page - 1) / page, p, run;
    unsigned char prot[1024];
    unsigned i;

    if (npages > sizeof(prot))
        return 1;           /* very large module: leave it RW (rare) */
    memset(prot, 0, npages);
    for (i = 0; i < phnum; i++) {
        size_t first, last;
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0)
            continue;
        first = ph[i].p_vaddr / page;
        last = (ph[i].p_vaddr + ph[i].p_memsz - 1) / page;
        for (p = first; p <= last && p < npages; p++)
            prot[p] |= (unsigned char)prot_of(ph[i].p_flags);
    }
    for (i = 0; i < phnum; i++) {
        size_t first, end;
        if (ph[i].p_type != PT_GNU_RELRO)
            continue;
        first = (ph[i].p_vaddr + page - 1) / page;
        end = (ph[i].p_vaddr + ph[i].p_memsz) / page;
        for (p = first; p < end && p < npages; p++)
            prot[p] &= (unsigned char)~GHAL_PROT_W;
    }
    for (p = 0; p < npages; p = run) {
        for (run = p + 1; run < npages && prot[run] == prot[p]; run++)
            ;
        if (ghal_mem_protect_range(m->base, p * page, (run - p) * page,
                                   prot[p]) != GHAL_OK)
            return 0;
    }
    return 1;
}

ghal_status ghal_module_load(const void *image, size_t size,
                             ghal_module_import import, void *ctx,
                             ghal_module **out)
{
    const unsigned char *img = image;
    const Ehdr *eh = image;
    const Phdr *ph;
    const Dyn *dyn = NULL;
    ghal_module *m;
    ghal_mem_info info;
    size_t span = 0, page;
    Xword init_arr, init_n;
    unsigned i;

    if (!image || !out || size < sizeof(Ehdr))
        return GHAL_ERR_INVAL;
    *out = NULL;
    if (memcmp(eh->e_ident, "\177ELF", 4) != 0 ||
        eh->e_ident[4] != ELF_CLASS || eh->e_type != ET_DYN ||
        eh->e_machine != ELF_MACHINE || eh->e_phentsize != sizeof(Phdr) ||
        eh->e_phoff > size || eh->e_phnum > (size - eh->e_phoff) / sizeof(Phdr))
        return GHAL_ERR_FORMAT;
    ph = (const Phdr *)(img + eh->e_phoff);

    /* extent of the image; segments must fit the file */
    for (i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_TLS)
            return GHAL_ERR_FORMAT;
        if (ph[i].p_type != PT_LOAD)
            continue;
        if (ph[i].p_filesz > ph[i].p_memsz || ph[i].p_offset > size ||
            ph[i].p_filesz > size - ph[i].p_offset ||
            ph[i].p_vaddr + ph[i].p_memsz < ph[i].p_vaddr)
            return GHAL_ERR_FORMAT;
        if (ph[i].p_vaddr + ph[i].p_memsz > span)
            span = (size_t)(ph[i].p_vaddr + ph[i].p_memsz);
    }
    if (span == 0)
        return GHAL_ERR_FORMAT;

    m = ghal_mem_alloc(sizeof(*m), GHAL_MEM_ZERO);
    if (!m)
        return GHAL_ERR_NOMEM;
    m->base = ghal_mem_alloc(span, GHAL_MEM_PAGED | GHAL_MEM_ZERO);
    if (!m->base) {
        ghal_mem_free(m);
        return GHAL_ERR_NOMEM;
    }
    m->span = span;
    for (i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_LOAD)
            memcpy(m->base + ph[i].p_vaddr, img + ph[i].p_offset,
                   ph[i].p_filesz);
        else if (ph[i].p_type == PT_DYNAMIC && ph[i].p_vaddr < span)
            dyn = (const Dyn *)(m->base + ph[i].p_vaddr);
    }
    if (!dyn || dyn_find(dyn, DT_NEEDED))
        goto bad_format;
    m->symtab = (const Sym *)(m->base + dyn_val(dyn, DT_SYMTAB));
    m->strtab = (const char *)(m->base + dyn_val(dyn, DT_STRTAB));
    m->nsyms = count_syms(m->base, dyn);

    if (!relocate_table(m, dyn_val(dyn, DT_REL), dyn_val(dyn, DT_RELSZ), 0,
                        import, ctx) ||
        !relocate_table(m, dyn_val(dyn, DT_RELA), dyn_val(dyn, DT_RELASZ), 1,
                        import, ctx) ||
        !relocate_table(m, dyn_val(dyn, DT_JMPREL),
                        dyn_val(dyn, DT_PLTRELSZ),
                        dyn_val(dyn, DT_PLTREL) == DT_RELA, import, ctx)) {
        ghal_mem_free(m->base);
        ghal_mem_free(m);
        return GHAL_ERR_UNRESOLVED;
    }

#if defined(__aarch64__) || defined(__arm__)
    __builtin___clear_cache((char *)m->base, (char *)m->base + span);
#endif
    page = (ghal_mem_get_info(&info) == GHAL_OK && info.page_size)
               ? info.page_size : 4096;
    if (!protect(m, ph, eh->e_phnum, page)) {
        ghal_mem_free(m->base);
        ghal_mem_free(m);
        return GHAL_ERR_HOST;
    }

    m->fini_array = dyn_val(dyn, DT_FINI_ARRAY);
    m->fini_count = dyn_val(dyn, DT_FINI_ARRAYSZ) / sizeof(Addr);
    init_arr = dyn_val(dyn, DT_INIT_ARRAY);
    init_n = dyn_val(dyn, DT_INIT_ARRAYSZ) / sizeof(Addr);
    for (i = 0; init_arr && i < init_n; i++) {
        init_fn f = ((const init_fn *)(m->base + init_arr))[i];
        if (f && (uintptr_t)f != (uintptr_t)-1)
            f();
    }
    *out = m;
    return GHAL_OK;

bad_format:
    ghal_mem_free(m->base);
    ghal_mem_free(m);
    return GHAL_ERR_FORMAT;
}

void *ghal_module_symbol(const ghal_module *m, const char *name)
{
    uint32_t i;

    if (!m || !name)
        return NULL;
    for (i = 1; i < m->nsyms; i++) {
        const Sym *s = &m->symtab[i];
        if (sym_defined(s) && strcmp(m->strtab + s->st_name, name) == 0)
            return m->base + s->st_value;
    }
    return NULL;
}

void ghal_module_unload(ghal_module *m)
{
    size_t i;

    if (!m)
        return;
    for (i = m->fini_count; m->fini_array && i > 0; i--) {
        init_fn f = ((const init_fn *)(m->base + m->fini_array))[i - 1];
        if (f && (uintptr_t)f != (uintptr_t)-1)
            f();
    }
    ghal_mem_free(m->base);
    ghal_mem_free(m);
}

const char *ghal_module_format(void)
{
    return "elf";
}
