/*
 * test_kernel_boot.c -- starting the C kernel (Library/Kernel/Boot,
 * Port32/ABI.md section 13i).
 *
 * The top GBTOP has a geos.ini that
 *   - gives HostFS a drive D ([hostfs] drives), the directory GBTOP/DRV;
 *   - adds D:\GEODES to SP_SYSTEM ([paths] system);
 *   - names the library kglib as the UI ([ui] generic).
 * The geodes are only in GBTOP/DRV/GEODES.  KernelBoot with the tail
 * "/a /bc kgapp.geo" must load kglib as the UI, and kgapp as the process
 * from SP_SYSTEM (its library from the same place).  When the process
 * has detached, KernelRun returns.
 */
#include "kernelC.h"
#include "ghal.h"
#include "hostfs.h"

#include <stdio.h>
#include <string.h>

extern const unsigned char kglib_geode[], kgapp_geode[];
extern const unsigned long kglib_geode_size, kgapp_geode_size;

/* as kgapp.goc */
typedef struct {
    word	KGAS_attached;
    word	KGAS_value1, KGAS_value2;
    int		KGAS_sum;
} KGAppState;

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)
#define WAIT_FOR(cond) \
    do { int n_; for (n_ = 0; n_ < 500 && !(cond); n_++) TimerSleep(1); \
    } while (0)

static char top[260];

static void RemoveTree(const char *path)
{
    ghal_dir *d;
    ghal_fs_info info;
    char name[260], sub[400];

    if (ghal_fs_opendir(path, &d) == GHAL_OK) {
	while (ghal_fs_readdir(d, name, sizeof(name), &info) == GHAL_OK) {
	    sprintf(sub, "%s%c%s", path, HostFSSep(), name);
	    if (info.attrs & GHAL_FS_ATTR_DIR)
		RemoveTree(sub);
	    else {
		ghal_fs_set_attrs(sub, 0);
		ghal_fs_delete(sub);
	    }
	}
	ghal_fs_closedir(d);
    }
    ghal_fs_rmdir(path);
}

/* the host path of `rel' ('/' separated) below the top */
static const char *H(const char *rel)
{
    static char buf[2][400];
    static int which;
    char *p = buf[which ^= 1];

    sprintf(p, "%s%c%s", top, HostFSSep(), rel);
    for (; *p; p++)
	if (*p == '/')
	    *p = HostFSSep();
    return buf[which];
}

static void Put(const char *rel, const void *data, size_t size)
{
    ghal_file *f;
    size_t done = 0;

    CHECK(ghal_fs_open(H(rel), GHAL_FS_WRITE | GHAL_FS_CREATE |
				 GHAL_FS_TRUNCATE, &f) == GHAL_OK);
    ghal_fs_write(f, data, size, &done);
    CHECK(done == size);
    ghal_fs_close(f);
}

int main(int argc, char **argv)
{
    char ini[600], *slash;
    GeodeHandle app, lib;
    GeodeLoadError e;
    KGAppState *st;
    struct { word callerID; optr ackOD; } dp = { 0, 0 };

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    strncpy(top, argv[0], sizeof(top) - 8);
    top[sizeof(top) - 8] = 0;
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash)
	strcpy(slash + 1, "GBTOP");
    else
	strcpy(top, "GBTOP");
    RemoveTree(top);
    CHECK(ghal_fs_mkdir(top) == GHAL_OK);
    CHECK(ghal_fs_mkdir(H("DRV")) == GHAL_OK);
    CHECK(ghal_fs_mkdir(H("DRV/GEODES")) == GHAL_OK);
    Put("DRV/GEODES/kglib.geo", kglib_geode, kglib_geode_size);
    Put("DRV/GEODES/kgapp.geo", kgapp_geode, kgapp_geode_size);
    sprintf(ini,
	    "[hostfs]\r\n"
	    "drives = D=%s\r\n"
	    "[paths]\r\n"
	    "system = D:\\GEODES\r\n"
	    "[ui]\r\n"
	    "generic = kglib.geo\r\n", H("DRV"));
    Put("geos.ini", ini, strlen(ini));

    app = KernelBoot(top, " /a /bc kgapp.geo", &e);
    CHECK(app != 0 && e == GLE_OK);
    if (!app) {
	printf("KernelBoot: GeodeLoadError %d\n", (int) e);
	return 1;
    }
    CHECK(FSDDriveOf(3) != NULL);
    lib = GeodeFind("kglib   ", GEODE_NAME_SIZE, GA_LIBRARY, 0);
    CHECK(lib != 0 && kernelUIHandle == lib);
    CHECK(GeodeFind("kgapp   ", GEODE_NAME_SIZE, GA_PROCESS, 0) == app);
    CHECK(GeodeGetInfo(lib, GGIT_GEODE_REF_COUNT, NULL) == 2);
    st = (KGAppState *) ProcGetLibraryEntry(app, 0);
    WAIT_FOR(st->KGAS_attached);
    /* attached, with no appInfo; it called the library (0 + 0) */
    CHECK(st->KGAS_attached == 1 && st->KGAS_value1 == 0 &&
	  st->KGAS_value2 == 0 && st->KGAS_sum == 0);

    /* the process detaches: KernelRun returns */
    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(app, 0), KMSG_META_DETACH,
		 &dp, sizeof(dp), NULL, 0);
    KernelRun();
    CHECK(!GeodeFind("kgapp   ", GEODE_NAME_SIZE, 0, 0));
    CHECK(GeodeGetInfo(lib, GGIT_GEODE_REF_COUNT, NULL) == 1);

    printf("%s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
