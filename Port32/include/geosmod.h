/*
 * geosmod.h -- the GEOS data of a geode module (Port32).
 *
 * A geode is a module in the host's format (ELF shared object, DXE3 on
 * dos32; loaded by ghal_module, Port32/GHAL-Concept.md section 4.4).  What
 * glue used to put into the geode header from the .gp file is the
 * exported object __geos_module, written by Port32/tools/gpgeode.py:
 * name, token, attributes, protocol, libraries, the exported entries in
 * .gp order and the resource table.  The kernel's GeodeLoad reads it
 * after ghal_module_load.  Port32/ABI.md section 7.
 */
#ifndef GEOSMOD_H
#define GEOSMOD_H

#include <geos.h>
#include <geode.h>

#define GEOS_MODULE_MAGIC	0x444f4d47u	/* "GMOD" */
#define GEOS_MODULE_VERSION	1

/* a library the geode uses ("library" lines) */
#define GML_NOLOAD		0x0001	/* "noload": loaded on demand */
#define GML_NOLOAD_FIXED	0x0002	/* "noload fixed" */
typedef struct {
    const char	*GML_name;		/* permanent name, e.g. "ui" */
    word	GML_protoMajor;		/* protocol needed (0.0: any) */
    word	GML_protoMinor;
    word	GML_flags;		/* GML_* */
} GeosModuleLibrary;

/* an exported entry ("export", "publish"); GME_address NULL: "skip" */
#define GME_PUBLISHED		0x0001
typedef struct {
    const void	*GME_address;
    const char	*GME_name;		/* the name after "as", if any */
    word	GME_flags;
} GeosModuleExport;

/* resource allocation flags (as Tools/include/geode.h, for glue) */
#define RESF_DISCARDED		0x0002	/* loaded on demand */
#define RESF_LMEM		0x0008
#define RESF_SWAPABLE		0x0010
#define RESF_DISCARDABLE	0x0020
#define RESF_SHARED		0x0040
#define RESF_FIXED		0x0080
#define RESF_CONFORMING		0x0100
#define RESF_CODE		0x0200
#define RESF_OBJECT		0x0400
#define RESF_READ_ONLY		0x0800
#define RESF_UI			0x1000

/*
 * A resource, indexed by its number (GOCR_RES_<NAME>); entries 0 (core
 * block) and 1 (fixed data) have no resource.  GMR_resource describes an
 * LMem resource made by goc -cg; code and plain data resources have
 * none (their contents are part of the module).  GMR_flags are the
 * RESF_* allocation flags from the .gp file, as glue computed them.
 */
typedef struct {
    const GocResource	*GMR_resource;
    const char		*GMR_name;
    word		GMR_flags;
} GeosModuleResource;

typedef struct {
    dword			GM_magic;	/* GEOS_MODULE_MAGIC */
    word			GM_version;	/* GEOS_MODULE_VERSION */
    char			GM_name[GEODE_NAME_SIZE];	/* space padded */
    char			GM_ext[GEODE_NAME_EXT_SIZE];
    const char			*GM_longName;
    GeodeToken			GM_token;
    GeodeAttrs			GM_attrs;
    word			GM_protoMajor;	/* from the .rev file */
    word			GM_protoMinor;
    word			GM_heapSpace;	/* "heapspace", 0: not given */
    word			GM_stackSize;	/* "stack", 0: default */
    ClassStruct			*GM_processClass;	/* "class" */
    optr			GM_appObj;	/* unrelocated, or 0 */
    const void			*GM_entry;	/* "entry" routine */
    word			GM_libraryCount;
    const GeosModuleLibrary	*GM_libraries;
    word			GM_exportCount;
    const GeosModuleExport	*GM_exports;
    word			GM_resourceCount;	/* including 0 and 1 */
    const GeosModuleResource	*GM_resources;
    /* the geode's resource handles, filled in by the kernel when it
     * loads the resources: GOCR_OPTR, GOCR_RESOLVE (geosport.h) */
    Handle			*GM_resourceHandles;
} GeosModuleInfo;

#endif /* GEOSMOD_H */
