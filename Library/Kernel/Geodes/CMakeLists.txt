# The C kernel, Geodes/: geodes: events and messages, loading, libraries, drivers.
# Beside the asm they replace:
#   geodesEvent.c <- events, queues, ObjMessage of geodesEvent.asm
#   geodesResource.c <- resource handles of geodes
#   geodesLoad.c  <- GeodeLoad of Geodes/geodesLoad.asm (modules)
#   geodesLibrary.c <- GeodeUseLibrary, GeodeFreeLibrary
#   geodesDriver.c <- geodesDriver.asm, DR_INIT and DR_EXIT of
#                      geodesLoad.asm and geodesUtils.asm
#   geodesAccess.c <- GeodeGetInfo, GeodeGetProcessHandle, GeodeFind
#   geodesPriv.c  <- geode private data of geodesPriv.asm
target_sources(geos_kernel_objs PRIVATE
  geodesEvent.c
  geodesResource.c
  geodesLoad.c
  geodesLibrary.c
  geodesDriver.c
  geodesAccess.c
  geodesPriv.c)
