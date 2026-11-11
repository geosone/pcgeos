/*
 * test_geode_hello.c -- load a real geode: Appl/SDK_C/Hello, built by
 * goc -cg and gpgeode.py as a module of the host (ELF, DXE3 on dos32),
 * and check its GEOS data (__geos_module, Port32/include/geosmod.h)
 * against hello.gp.
 *
 * There is no kernel yet: every import gets its own dummy address, so
 * the test can see where the loader put which import (the class pointer
 * of an object in a resource must be the import GenApplicationClass).
 * Nothing in the geode is called.
 */
#include "ghal.h"

#include <stdio.h>
#include <string.h>

/* the checks of the GEOS data, with the GEOS headers (geode_hello_check.c) */
typedef void HelloFail(int line, const char *what);
extern int HelloCheck(const void *module, const void *processClass,
                      const void *appClass, const void *drawText,
                      HelloFail *fail);

extern const unsigned char hello_geode[];
extern const unsigned long hello_geode_size;

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

/* every import gets the address of its own entry */
#define MAX_IMPORTS 64
static struct { char name[48]; } imports[MAX_IMPORTS];
static int nimports;

static void *Import(void *ctx, const char *name)
{
    int i;

    (void)ctx;
    for (i = 0; i < nimports; i++)
        if (strcmp(imports[i].name, name) == 0)
            return &imports[i];
    if (nimports == MAX_IMPORTS || strlen(name) >= sizeof(imports[0].name))
        return NULL;
    strcpy(imports[nimports].name, name);
    return &imports[nimports++];
}

static const void *ImportOf(const char *name)
{
    int i;

    for (i = 0; i < nimports; i++)
        if (strcmp(imports[i].name, name) == 0)
            return &imports[i];
    return NULL;
}

static void Fail(int line, const char *what)
{
    fails++;
    printf("FAIL geode_hello_check.c line %d: %s\n", line, what);
}

int main(void)
{
    ghal_module *m;
    unsigned char *image;
    int i;

    image = ghal_mem_alloc(hello_geode_size, 0);
    if (!image)
        return 1;
    memcpy(image, hello_geode, hello_geode_size);
    CHECK(ghal_module_load(image, hello_geode_size, Import, NULL, &m)
          == GHAL_OK);
    ghal_mem_free(image);
    if (fails)
        return 1;
    printf("hello.geo (%s): %d imports:", ghal_module_format(), nimports);
    for (i = 0; i < nimports; i++)
        printf(" %s", imports[i].name);
    printf("\n");

    CHECK(ImportOf("GenApplicationClass") != NULL);
    CHECK(ImportOf("GrDrawText") != NULL);
    CHECK(ghal_module_symbol(m, "HelloProcessClass") != NULL);
    CHECK(ghal_module_symbol(m, "__geos_module") != NULL);
    if (!fails)
        HelloCheck(ghal_module_symbol(m, "__geos_module"),
                   ghal_module_symbol(m, "HelloProcessClass"),
                   ImportOf("GenApplicationClass"), ImportOf("GrDrawText"),
                   Fail);

    ghal_module_unload(m);
    printf("%s: %d failure(s)\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
