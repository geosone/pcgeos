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
  # ghal_audio: PipeWire where its library is there for the target,
  # else null only (GEOS_LINUX_AUDIO=null to leave it out)
  set(GEOS_LINUX_AUDIO "pipewire" CACHE STRING "ghal_audio: pipewire or null")
  target_sources(ghal PRIVATE Audio/ghal_audio.c)
  target_include_directories(ghal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Audio)
  set(_audio null)
  if(GEOS_LINUX_AUDIO STREQUAL "pipewire")
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(PIPEWIRE QUIET IMPORTED_TARGET libpipewire-0.3)
    endif()
    if(PIPEWIRE_FOUND)
      # the host's library may not fit the target (cross and -m32
      # builds): only if a program links with it
      include(CheckCSourceCompiles)
      set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
      set(CMAKE_REQUIRED_INCLUDES ${PIPEWIRE_INCLUDE_DIRS})
      set(CMAKE_REQUIRED_LIBRARIES ${PIPEWIRE_LINK_LIBRARIES})
      check_c_source_compiles("#include <pipewire/pipewire.h>
int main(void) { pw_init(0, 0); return 0; }" GHAL_HAVE_PIPEWIRE)
      unset(CMAKE_REQUIRED_DEFINITIONS)
      unset(CMAKE_REQUIRED_INCLUDES)
      unset(CMAKE_REQUIRED_LIBRARIES)
    endif()
    if(GHAL_HAVE_PIPEWIRE)
      set(_audio pipewire)
      # on its own: the spa headers include the C library's <math.h>,
      # which CInclude's hides; ghal.h is found as "ghal.h" (-iquote)
      add_library(ghal_pipewire OBJECT Audio/ghal_audio_pipewire.c)
      target_compile_options(ghal_pipewire PRIVATE
        "SHELL:-iquote ${PROJECT_SOURCE_DIR}/CInclude"
        "SHELL:-iquote ${CMAKE_CURRENT_SOURCE_DIR}/Audio")
      target_link_libraries(ghal_pipewire PRIVATE PkgConfig::PIPEWIRE)
      target_sources(ghal PRIVATE $<TARGET_OBJECTS:ghal_pipewire>)
      target_compile_definitions(ghal PRIVATE GHAL_HAVE_PIPEWIRE)
      target_compile_definitions(ghal_pipewire PRIVATE GHAL_HAVE_PIPEWIRE)
      target_link_libraries(ghal PUBLIC PkgConfig::PIPEWIRE)
    endif()
  endif()
  message(STATUS "ghal_audio: ${_audio}")
elseif(GEOS_HOST STREQUAL "dos32")
  target_sources(ghal PRIVATE Dos32/ghal_dos32.c Dos32/ghal_fs_dos.c
                              Display/ghal_display_null.c
                              Audio/ghal_audio_none.c)
  # geodes are DXE3 modules (dxe3gen); the Watcom build has no loader
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_sources(ghal PRIVATE Dos32/ghal_module_dxe.c)
  else()
    target_sources(ghal PRIVATE Dos32/ghal_module_none.c)
  endif()
else()
  message(FATAL_ERROR "No GHAL backend for GEOS_HOST=${GEOS_HOST} yet")
endif()
