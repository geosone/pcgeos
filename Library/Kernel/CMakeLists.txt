# The C kernel (Port32/PLAN.md P4).  Its files lie next to the asm they
# replace; the pmake build of the asm kernel lists its sources in the
# generated Makefile and is not affected.
#   Sys/sysError.c     <- fatal errors of Sys/sysError.asm
#   Heap/heapHandle.c  <- handle table of Heap/heapHandle.asm
#   Heap/heapHigh.c    <- memory block API of Heap/heapHigh.asm, heapC.asm
#   Thread/threadThread.c <- threads, scheduler of Thread/threadThread.asm
#   Thread/threadSem.c <- semaphores, thread locks of Thread/threadSem.asm
#   LMem/lmemCode.c    <- local memory heaps of LMem/lmemCode.asm
#   LMem/lmemChunkArray.c, lmemElementArray.c, lmemNameArray.c
#                      <- the arrays of the same names (.asm)
#   LMem/lmemGCNList.c <- GCN lists of LMem/lmemGCNList.asm
#   Timer/timerList.c  <- timers of Timer/timerList.asm, timerMisc.asm
#   Timer/timerInt.c   <- the timer interrupt of Timer/timerInt.asm
#   Geodes/geodesEvent.c <- events, queues, ObjMessage of geodesEvent.asm
#   Geodes/geodesResource.c <- resource handles of geodes (until GeodeLoad)
#   Object/objectClass.c <- method calls, ObjInstantiate of objectClass.asm
#   Object/objectUtils.c <- master parts, chunk flags, counts, block output
#                          of Object/objectUtils.asm
#   Object/objectVarData.c <- variable data of Object/objectVarData.asm
#   Object/objectMeta.c  <- MetaClass, freeing objects of objectMeta.asm
#   Object/objectProcess.c <- ProcessClass of Object/objectProcess.asm
#   Object/objectReloc.c <- object relocation of Object/objectReloc.asm
#   Object/objectComposite.c <- composites of Object/objectComposite.asm,
#                          CObjSendToChildren of Object/objectC.asm
#   Object/objectLinkable.c <- ObjLinkFindParent of objectLinkable.asm
add_library(geos_kernel STATIC
  Sys/sysError.c
  Heap/heapHandle.c
  Heap/heapHigh.c
  Thread/threadThread.c
  Thread/threadSem.c
  LMem/lmemCode.c
  LMem/lmemChunkArray.c
  LMem/lmemElementArray.c
  LMem/lmemNameArray.c
  LMem/lmemGCNList.c
  Timer/timerList.c
  Timer/timerInt.c
  Geodes/geodesEvent.c
  Geodes/geodesResource.c
  Object/objectClass.c
  Object/objectUtils.c
  Object/objectVarData.c
  Object/objectMeta.c
  Object/objectProcess.c
  Object/objectReloc.c
  Object/objectComposite.c
  Object/objectLinkable.c)
target_include_directories(geos_kernel PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/Port32/include
  ${PROJECT_SOURCE_DIR}/CInclude)
target_compile_definitions(geos_kernel PUBLIC GEOS_PORT32)
target_link_libraries(geos_kernel PUBLIC ghal)
