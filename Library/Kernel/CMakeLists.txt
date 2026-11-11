# The C kernel (Port32/PLAN.md P4).  Its files lie next to the asm they
# replace; the pmake build of the asm kernel lists its sources in the
# generated Makefile and is not affected.
#   Sys/sysError.c     <- fatal errors of Sys/sysError.asm
#   Heap/heapHandle.c  <- handle table of Heap/heapHandle.asm
#   Heap/heapHigh.c    <- memory block API of Heap/heapHigh.asm, heapC.asm
add_library(geos_kernel STATIC
  Sys/sysError.c
  Heap/heapHandle.c
  Heap/heapHigh.c)
target_include_directories(geos_kernel PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/Port32/include
  ${PROJECT_SOURCE_DIR}/CInclude)
target_compile_definitions(geos_kernel PUBLIC GEOS_PORT32)
target_link_libraries(geos_kernel PUBLIC ghal)
