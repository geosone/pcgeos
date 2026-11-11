/*
 * geode_hello_check.c -- checks of the GEOS data of the Hello geode
 * (test_geode_hello.c), compiled with the GEOS headers: __geos_module
 * (CInclude/geosmod.h) against Appl/SDK_C/Hello/hello.gp.
 */
#include <geos.h>
#include <object.h>
#include <geosmod.h>

typedef void HelloFail(int line, const char *what);

#define CHECK(c) do { if (!(c)) { fail(__LINE__, #c); fails++; } } while (0)

static int Same(const char *a, const char *b, int n)
{
    int i;

    for (i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static int Equal(const char *a, const char *b)
{
    for (; *a && *a == *b; a++, b++)
        ;
    return *a == *b;
}

int HelloCheck(const void *module, const void *processClass,
               const void *appClass, const void *drawText, HelloFail *fail)
{
    const GeosModuleInfo *gm = module;
    const GocResource *app, *ui;
    ChunkHandle ch, first;
    int fails = 0;

    (void)drawText;
    CHECK(gm->GM_magic == GEOS_MODULE_MAGIC);
    CHECK(gm->GM_version == GEOS_MODULE_VERSION);
    CHECK(Same(gm->GM_name, "hello   ", GEODE_NAME_SIZE));
    CHECK(Same(gm->GM_ext, "app ", GEODE_NAME_EXT_SIZE));
    CHECK(Equal(gm->GM_longName, "C Hello"));
    CHECK(Same(gm->GM_token.GT_chars, "HELO", 4));
    CHECK(gm->GM_token.GT_manufID == 8);
    CHECK(gm->GM_attrs == (GA_PROCESS | GA_APPLICATION));  /* single */
    CHECK(gm->GM_heapSpace == 3250);
    CHECK(gm->GM_processClass == processClass);
    CHECK(gm->GM_entry == NULL);
    CHECK(gm->GM_libraryCount == 2);
    CHECK(Equal(gm->GM_libraries[0].GML_name, "geos"));
    CHECK(Equal(gm->GM_libraries[1].GML_name, "ui"));
    CHECK(gm->GM_exportCount == 0);
    CHECK(gm->GM_resourceHandles != NULL);

    /* 0 and 1 reserved, then the .gp resources */
    CHECK(gm->GM_resourceCount == 4);
    CHECK(gm->GM_resources[0].GMR_resource == NULL);
    CHECK(gm->GM_resources[1].GMR_resource == NULL);
    CHECK(Equal(gm->GM_resources[2].GMR_name, "AppResource"));
    CHECK(Equal(gm->GM_resources[3].GMR_name, "Interface"));
    CHECK(gm->GM_resources[2].GMR_flags ==
          (RESF_OBJECT | RESF_UI | RESF_LMEM | RESF_SHARED | RESF_SWAPABLE |
           RESF_DISCARDED));                                /* ui-object */
    app = gm->GM_resources[2].GMR_resource;
    ui = gm->GM_resources[3].GMR_resource;
    CHECK(app != NULL && ui != NULL);
    if (fails)
        return fails;
    CHECK(app->GR_number == 2 && ui->GR_number == 3);

    /* appobj: an unrelocated optr to a GenApplicationClass object */
    CHECK(GOCR_IS_UNRELOCATED(gm->GM_appObj));
    CHECK(GOCR_RES_OF(gm->GM_appObj) == 2);
    ch = OptrToChunk(gm->GM_appObj);
    first = (ChunkHandle) GOCR_LMEM_HANDLES(ObjLMemBlockHeader);
    CHECK(ch >= first && (ch - first) % 2 == 0 &&
          (ch - first) / 2 < app->GR_chunks);
    if (!fails) {
        /* an object starts with its class pointer (MetaBase) */
        ClassStruct *const *obj = app->GR_chunk[(ch - first) / 2].GC_data;
        CHECK((const void *) *obj == appClass);
    }
    return fails;
}
