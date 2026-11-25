# The build tools of the C port that are programs of the build host:
# goc and uic (with uicpp), and the generators that make C of the asm
# sources' tables.  A project of its own, built with the compiler of the
# build host also when the port is cross compiled; the port's build
# configures and builds it as an external project
# (cmake/GeosHostTools.cmake).  The pmake build of the tools (Open
# Watcom, bin/) is not touched by this: it stays the one of the 16-bit
# build.
#
#   cmake -S Tools -B build-tools && cmake --build build-tools
cmake_minimum_required(VERSION 3.20)
project(GeosHostTools LANGUAGES C)

# Not optimized: the tools' old C does not survive gcc -O2 (goc crashes),
# and they are fast enough without
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# What Tools/include/config.h wants to know of a Linux (or other POSIX)
# host with gcc or clang: _LINUX, and not "unix" (which stands for
# SunOS there); the functions the host's C library has
add_library(geos_tool_config INTERFACE)
target_compile_definitions(geos_tool_config INTERFACE
  _LINUX HAVE_STRERROR HAVE_BCMP HAVE_BCOPY HAVE_BZERO
  O_BINARY=0 O_TEXT=0
  "genptrparam=void*" "genptrparamconst=const void*")
target_compile_options(geos_tool_config INTERFACE -std=gnu89 -Uunix -w)
target_include_directories(geos_tool_config INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_subdirectory(utils)
add_subdirectory(goc)
add_subdirectory(uic)
add_subdirectory(uicpp)

# The generators: plain C with nothing but the C library, so that they
# can be built wherever GEOS is built (DOS too).  Each gives the same
# output as the Python script it replaced.
add_subdirectory(gpres)
add_subdirectory(kexports)
add_subdirectory(symtabgen)
add_subdirectory(papergen)
add_subdirectory(asmchars2c)
add_subdirectory(epsfonts2c)
add_subdirectory(kmapgen)
add_subdirectory(localgen)
add_subdirectory(psdevinfo2c)
add_subdirectory(dmdevinfo2c)
add_subdirectory(sbigen)
