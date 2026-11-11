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
#   Geodes/geodesResource.c <- resource handles of geodes
#   Geodes/geodesLoad.c  <- GeodeLoad of Geodes/geodesLoad.asm (modules)
#   Geodes/geodesLibrary.c <- GeodeUseLibrary, GeodeFreeLibrary
#   Geodes/geodesAccess.c <- GeodeGetInfo, GeodeGetProcessHandle, GeodeFind
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
#   Object/objectShutdown.c <- detach and quit of objectShutdown.asm
#   Object/objectDup.c <- duplicating resources, saved blocks of
#                          objectDup.asm
#   File/filePath.c <- current and standard paths of File/filePath.asm,
#                      host paths (ghal_fs)
#   File/fileOpenClose.c, File/fileIO.c, File/fileFile.c <- the same
#                      asm files: open/close, I/O, by-name operations
add_library(geos_kernel_objs OBJECT
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
  Geodes/geodesLoad.c
  Geodes/geodesLibrary.c
  Geodes/geodesAccess.c
  Object/objectClass.c
  Object/objectUtils.c
  Object/objectVarData.c
  Object/objectMeta.c
  Object/objectProcess.c
  Object/objectReloc.c
  Object/objectComposite.c
  Object/objectLinkable.c
  Object/objectShutdown.c
  Object/objectDup.c
  File/filePath.c
  File/fileOpenClose.c
  File/fileIO.c
  File/fileFile.c)
target_include_directories(geos_kernel_objs PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/Port32/include
  ${PROJECT_SOURCE_DIR}/CInclude)
target_compile_definitions(geos_kernel_objs PRIVATE GEOS_PORT32)
target_link_libraries(geos_kernel_objs PRIVATE ghal)

# The kernel's export table (what geodes may import by name): the
# symbols of the objects above that Library/Kernel/geos.gp exports
# (Port32/tools/kexports.py).  Without nm (Open Watcom) it is empty:
# that toolchain loads no geode modules.
find_package(Python3 COMPONENTS Interpreter)
set(_kexports ${CMAKE_CURRENT_BINARY_DIR}/kernelExports.c)
if(CMAKE_NM AND Python3_Interpreter_FOUND)
  set(_kexports_args)
  if(GEOS_HOST STREQUAL "dos32")
    list(APPEND _kexports_args --underscore)
  endif()
  add_custom_command(OUTPUT ${_kexports}
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Port32/tools/kexports.py
            --nm ${CMAKE_NM} --gp ${CMAKE_CURRENT_SOURCE_DIR}/geos.gp
            ${_kexports_args} -o ${_kexports} $<TARGET_OBJECTS:geos_kernel_objs>
    DEPENDS geos_kernel_objs $<TARGET_OBJECTS:geos_kernel_objs>
            ${CMAKE_CURRENT_SOURCE_DIR}/geos.gp
            ${PROJECT_SOURCE_DIR}/Port32/tools/kexports.py
    COMMAND_EXPAND_LISTS
    COMMENT "kernel exports"
    VERBATIM)
else()
  file(WRITE ${_kexports}
    "typedef struct { const char *name; const void *addr; } KernelExport;\n"
    "const KernelExport kernelExports[] = { { 0, 0 } };\n"
    "const unsigned kernelExportCount = 0;\n")
endif()

add_library(geos_kernel STATIC $<TARGET_OBJECTS:geos_kernel_objs> ${_kexports})
target_include_directories(geos_kernel PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/Port32/include
  ${PROJECT_SOURCE_DIR}/CInclude)
target_compile_definitions(geos_kernel PUBLIC GEOS_PORT32)
target_link_libraries(geos_kernel PUBLIC ghal)
