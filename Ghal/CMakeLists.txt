# GHAL core library (Port32/GHAL-Concept.md): the host layer under the
# kernel and the drivers.  One backend per host; the interface is
# CInclude/ghal.h.
add_library(ghal STATIC)
target_include_directories(ghal PUBLIC ${PROJECT_SOURCE_DIR}/CInclude
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# the same on every host
target_sources(ghal PRIVATE Common/ghal_input.c Common/ghal_hid_linux.c)

if(GEOS_HOST STREQUAL "linux")
  target_sources(ghal PRIVATE Posix/ghal_posix.c Posix/ghal_fs_posix.c Elf/ghal_module_elf.c)
  find_package(Threads REQUIRED)
  target_link_libraries(ghal PUBLIC Threads::Threads)
  # ghal_display: X11 where its libraries are there for the target (not
  # in the cross builds), else null
  set(_display null)
  if(GEOS_LINUX_DISPLAY STREQUAL "x11")
    find_package(X11 QUIET)
    if(X11_FOUND AND X11_Xext_FOUND)
      include(CheckCSourceCompiles)
      set(CMAKE_REQUIRED_INCLUDES ${X11_INCLUDE_DIR})
      set(CMAKE_REQUIRED_LIBRARIES ${X11_X11_LIB} ${X11_Xext_LIB})
      check_c_source_compiles("#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
int main(void) { return XShmQueryExtension(XOpenDisplay(0)); }"
        GHAL_HAVE_X11)
      unset(CMAKE_REQUIRED_INCLUDES)
      unset(CMAKE_REQUIRED_LIBRARIES)
      if(GHAL_HAVE_X11)
        set(_display x11)
      endif()
    endif()
  endif()
  if(_display STREQUAL "x11")
    target_sources(ghal PRIVATE Display/ghal_display_x11.c)
    target_include_directories(ghal PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(ghal PUBLIC ${X11_X11_LIB} ${X11_Xext_LIB})
  else()
    target_sources(ghal PRIVATE Display/ghal_display_null.c)
  endif()
  message(STATUS "ghal_display: ${_display}")
elseif(GEOS_HOST STREQUAL "dos32")
  target_sources(ghal PRIVATE Dos32/ghal_dos32.c Dos32/ghal_fs_dos.c
                              Display/ghal_display_null.c)
  # geodes are DXE3 modules (dxe3gen); the Watcom build has no loader
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_sources(ghal PRIVATE Dos32/ghal_module_dxe.c)
  else()
    target_sources(ghal PRIVATE Dos32/ghal_module_none.c)
  endif()
else()
  message(FATAL_ERROR "No GHAL backend for GEOS_HOST=${GEOS_HOST} yet")
endif()
