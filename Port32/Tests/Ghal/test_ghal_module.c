/*
 * test_ghal_module.c -- ghal_module: load a geode, resolve its imports,
 * call it, unload it (Port32/GHAL-Concept.md section 4.4).
 *
 * The geode is Geode/testgeode.c, built in the host's module format (ELF
 * shared object, or DXE3 on dos32) and embedded in this program, so no
 * file system is needed.  This program plays the kernel and exports
 * KernelAdd, kernelCounter and kernelName (on dos32 also the frame
 * registration of its libgcc, on ARM raise; see cmake/GeosGeode.cmake).
 */
#include "ghal.h"

#include <stdio.h>
#include <string.h>

extern const unsigned char testgeode_image[];
extern const unsigned long testgeode_image_size;

int kernelCounter;
const char kernelName[] = "test kernel";
int KernelAdd(int a, int b) { return a + b; }

#ifdef __DJGPP__
/* DXE3 modules with unwind tables register them with the kernel's libgcc */
extern void __register_frame_info(const void *begin, void *ob);
extern void *__deregister_frame_info(const void *begin);
#endif

#ifdef __arm__
/* libgcc on ARM: division by zero calls raise(SIGFPE); the kernel
 * provides it (a divide fault) */
static int TestRaise(int sig) { (void)sig; return 0; }
#endif

static int fails;
static int imports;
static const char *refuse;      /* import to leave unresolved */

#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static void *Import(void *ctx, const char *name)
{
    (void)ctx;
    imports++;
    if (refuse && strcmp(name, refuse) == 0)
        return NULL;
    if (strcmp(name, "KernelAdd") == 0)
        return (void *)KernelAdd;
    if (strcmp(name, "kernelCounter") == 0)
        return &kernelCounter;
    if (strcmp(name, "kernelName") == 0)
        return (void *)kernelName;
#ifdef __arm__
    if (strcmp(name, "raise") == 0)
        return (void *)TestRaise;
#endif
#ifdef __DJGPP__
    if (strcmp(name, "__register_frame_info") == 0)
        return (void *)__register_frame_info;
    if (strcmp(name, "__deregister_frame_info") == 0)
        return (void *)__deregister_frame_info;
#endif
    return NULL;
}

typedef int (*IntFn)(void);
typedef int (*Int2Fn)(int, int);
typedef int (*Int1Fn)(int);
typedef const char *(*StrFn)(int);
typedef const char *(*Str0Fn)(void);
typedef int *(*PtrFn)(void);
typedef unsigned long long (*DivFn)(unsigned long long, unsigned long long);

#define SYM(m, type, name) ((type)ghal_module_symbol(m, name))

int main(void)
{
    ghal_module *m, *m2;
    ghal_status st;
    unsigned char *image;

    printf("module format: %s, geode %lu bytes\n", ghal_module_format(),
           testgeode_image_size);
    /* the kernel reads a geode into a heap block: aligned */
    image = ghal_mem_alloc(testgeode_image_size, 0);
    CHECK(image != NULL);
    if (!image)
        return 1;
    memcpy(image, testgeode_image, testgeode_image_size);

    st = ghal_module_load(image, testgeode_image_size, Import, NULL, &m);
    CHECK(st == GHAL_OK);
    if (st != GHAL_OK)
        return 1;
    CHECK(imports >= 3);
    CHECK(kernelCounter == 100);                        /* initializer */
    CHECK(SYM(m, IntFn, "GeodeInitRan")() == 1);
    CHECK(SYM(m, Int2Fn, "GeodeCall")(3, 4) == 9);      /* call an import */
    CHECK(strcmp(SYM(m, StrFn, "GeodeString")(1), "beta") == 0);
    CHECK(SYM(m, Int1Fn, "GeodeTwice")(21) == 42);      /* function pointer */
    CHECK(SYM(m, PtrFn, "GeodeCounterPtr")() == &kernelCounter);
    CHECK(SYM(m, Str0Fn, "GeodeKernelName")() == kernelName);
    CHECK(SYM(m, IntFn, "GeodeBssSum")() == 0);
    CHECK(SYM(m, DivFn, "GeodeDiv")(1000000000000ULL, 1000ULL) ==
          1000000000ULL);
    CHECK(SYM(m, IntFn, "GeodeData")() == 42);
    CHECK(SYM(m, IntFn, "GeodeData")() == 43);
    CHECK(*SYM(m, int *, "dataValue") == 44);           /* exported data */
    CHECK(ghal_module_symbol(m, "Twice") == NULL);      /* static */
    CHECK(ghal_module_symbol(m, "NoSuchName") == NULL);

    /* a second instance has its own data */
    CHECK(ghal_module_load(image, testgeode_image_size, Import, NULL, &m2)
          == GHAL_OK);
    CHECK(kernelCounter == 200);
    CHECK(SYM(m2, IntFn, "GeodeData")() == 42);
    CHECK(SYM(m, IntFn, "GeodeData")() == 44);
    ghal_module_unload(m2);
    CHECK(kernelCounter == 1200);                       /* finalizer */
    ghal_module_unload(m);
    CHECK(kernelCounter == 2200);

    /* an import nobody provides */
    refuse = "KernelAdd";
    CHECK(ghal_module_load(image, testgeode_image_size, Import, NULL, &m)
          == GHAL_ERR_UNRESOLVED);
    CHECK(m == NULL);
    refuse = NULL;

    /* not a module */
    image[0] ^= 0xff;
    CHECK(ghal_module_load(image, testgeode_image_size, Import, NULL, &m)
          == GHAL_ERR_FORMAT);
    CHECK(ghal_module_load(image, 8, Import, NULL, &m) != GHAL_OK);
    ghal_mem_free(image);

    printf("%s: %d failure(s)\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
