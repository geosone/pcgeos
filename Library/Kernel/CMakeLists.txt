# The C kernel (Port32/PLAN.md P4).  Its files lie next to the asm they
# replace (one CMakeLists.txt per directory says which); the pmake build
# of the asm kernel lists its sources in the generated Makefile and is
# not affected.
add_library(geos_kernel_objs OBJECT
  # the HostFS file system driver (CInclude/Internal/fsDriver.h), part
  # of the kernel until drivers are geodes
  ${PROJECT_SOURCE_DIR}/Driver/IFS/HostFS/hostfsDriver.c)
add_subdirectory(Sys)
add_subdirectory(Heap)
add_subdirectory(Thread)
add_subdirectory(LMem)
add_subdirectory(Timer)
add_subdirectory(Geodes)
add_subdirectory(Object)
add_subdirectory(File)
add_subdirectory(FSD)
add_subdirectory(Drive)
add_subdirectory(Disk)
add_subdirectory(Initfile)
add_subdirectory(VMem)
add_subdirectory(Boot)
add_subdirectory(IM)
add_subdirectory(Win)
add_subdirectory(Graphics)
add_subdirectory(Local)
add_subdirectory(DBase)
target_include_directories(geos_kernel_objs PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/CInclude)
target_compile_definitions(geos_kernel_objs PRIVATE GEOS_PORT32)
target_link_libraries(geos_kernel_objs PRIVATE ghal gapi hostfs)

# The kernel's export table (what geodes may import by name): the
# symbols of the objects above that Library/Kernel/geos.gp exports
# (Port32/tools/kexports.py).  Without nm (Open Watcom) it is empty:
# that toolchain loads no geode modules.
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
    "typedef struct { const char *name; const void *addr; unsigned short entry; } KernelExport;\n"
    "const KernelExport kernelExports[] = { { 0, 0, 0 } };\n"
    "const unsigned kernelExportCount = 0;\n")
endif()

add_library(geos_kernel STATIC $<TARGET_OBJECTS:geos_kernel_objs> ${_kexports})
target_include_directories(geos_kernel PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/CInclude)
target_compile_definitions(geos_kernel PUBLIC GEOS_PORT32)
target_link_libraries(geos_kernel PUBLIC ghal gapi hostfs)
