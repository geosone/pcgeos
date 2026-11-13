# PC/GEOS C port on top of the GHAL (GEOS Hardware Abstraction Layer).
# See Port32/PLAN.md and Port32/GHAL-Concept.md.
#
# C ports of asm files live next to the asm they replace; Port32/ only holds
# new material (docs, tools, tests).  The existing 16-bit/pm16 build still
# uses pmake (Port32/tools/geosbuild.sh) and is not touched by this file.
#
#   cmake -S . -B build -G Ninja                            # host linux, native
#   cmake -S . -B build32 -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-linux-i686.cmake
#   cmake -S . -B build-dos32 -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-dos32-djgpp.cmake
#   (cmake/toolchain-dos32-watcom.cmake: the older Open Watcom 386 build,
#    without goc -cg)
#   cmake --build build && ctest --test-dir build
cmake_minimum_required(VERSION 3.20)
project(PCGEOS LANGUAGES C)

set(GEOS_HOSTS dos32 linux win32 win64 gonware)
set(GEOS_HOST linux CACHE STRING "GHAL host: ${GEOS_HOSTS}")
set_property(CACHE GEOS_HOST PROPERTY STRINGS ${GEOS_HOSTS})
if(NOT GEOS_HOST IN_LIST GEOS_HOSTS)
  message(FATAL_ERROR "GEOS_HOST must be one of: ${GEOS_HOSTS}")
endif()

# x11 is implemented (null where the target has no X11 libraries);
# sdl3 and fbdev give the null display until they are
set(GEOS_LINUX_DISPLAYS x11 sdl3 fbdev)
set(GEOS_LINUX_DISPLAY x11 CACHE STRING "Display backend for GEOS_HOST=linux: ${GEOS_LINUX_DISPLAYS}")
set_property(CACHE GEOS_LINUX_DISPLAY PROPERTY STRINGS ${GEOS_LINUX_DISPLAYS})
if(NOT GEOS_LINUX_DISPLAY IN_LIST GEOS_LINUX_DISPLAYS)
  message(FATAL_ERROR "GEOS_LINUX_DISPLAY must be one of: ${GEOS_LINUX_DISPLAYS}")
endif()

option(GEOS_ERROR_CHECK "Build the error-checking (EC) variant" ON)

if(CMAKE_C_COMPILER_ID STREQUAL "OpenWatcom")
  # dos32: Open Watcom 386 (C99 mode, warning level 4); enums are ints
  # as with gcc (Watcom makes them as small as their values, which
  # changes structures such as TimerDateAndTime)
  add_compile_options(-za99 -w4 -ei)
else()
  set(CMAKE_C_STANDARD 11)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  if(GEOS_HOST STREQUAL "dos32")
    # DJGPP hides its DOS/DPMI functions (inportb, _dos_*, __dpmi_*) from
    # strict ISO C: gnu11
    set(CMAKE_C_EXTENSIONS ON)
  else()
    set(CMAKE_C_EXTENSIONS OFF)
  endif()
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra)
endif()
if(GEOS_ERROR_CHECK)
  add_compile_definitions(DO_ERROR_CHECKING)   # GEOS convention; geos.h derives ERROR_CHECK
endif()
string(TOUPPER "${GEOS_HOST}" _host)
add_compile_definitions(GEOS_HOST_${_host})

message(STATUS "GEOS_HOST=${GEOS_HOST} arch=${CMAKE_SYSTEM_PROCESSOR} EC=${GEOS_ERROR_CHECK}")

enable_testing()
add_subdirectory(Ghal)
add_subdirectory(Gapi)
add_subdirectory(Driver/IFS/HostFS)
add_subdirectory(Library/Math/Float)
add_subdirectory(Library/Kernel)
add_subdirectory(Loader)
add_subdirectory(Driver/Keyboard)
add_subdirectory(Driver/Mouse)
add_subdirectory(Driver/Video)
add_subdirectory(Driver/Font)
add_subdirectory(Driver/Stream/Cups)
add_subdirectory(Driver/Stream/Filestr)
add_subdirectory(Library/Trans/Graphics/Vector/EPS)
add_subdirectory(Driver/Printer/PScript)
add_subdirectory(Driver/Printer/DotMatrix/Epson9)
add_subdirectory(Driver/Printer/DotMatrix/Epson24)
add_subdirectory(Driver/Printer/HP/Deskjet)
add_subdirectory(Driver/Printer/HP/Pcl4)
add_subdirectory(Library/Spool)
add_subdirectory(Port32/Tests)

# Inventory of direct host contacts (plan P2).  Not part of 'all'.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_custom_target(ghal-inventory
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/Port32/tools/ghal_inventory.py
            --root ${CMAKE_CURRENT_SOURCE_DIR}
            --markdown ${CMAKE_BINARY_DIR}/GHAL-Inventory.md
            --json ${CMAKE_BINARY_DIR}/ghal_inventory.json
    COMMENT "Scanning GEOS sources for direct host contacts"
    VERBATIM)
  add_custom_target(ghal-inventory-check
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/Port32/tools/ghal_inventory.py
            --root ${CMAKE_CURRENT_SOURCE_DIR}
            --check ${CMAKE_CURRENT_SOURCE_DIR}/Port32/tools/ghal_baseline.json
    COMMENT "Checking for new host contacts against the baseline"
    VERBATIM)
endif()
