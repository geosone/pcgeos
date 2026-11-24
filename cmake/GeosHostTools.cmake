# GeosHostTools.cmake -- the build tools that run on the build host.
#
# Tools/ (goc, uic with its preprocessor uicpp) is a project of its own, configured and built here with
# the compiler of the build host: the port may be cross compiled.  It is
# built before anything that uses it and again whenever its sources
# change.  Afterwards:
#
#   GEOS_GOC        the goc program (a file; rules that run it depend on
#                   it and on the target geos_host_tools)
#   GEOS_UIC        uic, the same
#   GEOS_UICPP      uicpp, the same
#   GEOS_GPRES, GEOS_GPGEODE, GEOS_KEXPORTS, GEOS_SYMTABGEN,
#   GEOS_PAPERGEN, GEOS_ASMCHARS2C, GEOS_EPSFONTS2C, GEOS_KMAPGEN,
#   GEOS_LOCALGEN, GEOS_PSDEVINFO2C, GEOS_DMDEVINFO2C
#                   the generators (Tools/<name>), the same
#
# GEOS_HOST_CC picks the host compiler (default: cc, gcc or clang of the
# PATH).
include_guard(GLOBAL)
include(ExternalProject)

find_program(GEOS_HOST_CC NAMES cc gcc clang
             DOC "C compiler of the build host, for the build tools"
             NO_CMAKE_FIND_ROOT_PATH)
if(NOT GEOS_HOST_CC)
  message(FATAL_ERROR "No C compiler for the build host (GEOS_HOST_CC)")
endif()

set(_tools_dir ${CMAKE_BINARY_DIR}/hosttools)
set(GEOS_GOC ${_tools_dir}/goc/goc CACHE INTERNAL "goc of the build host")
set(GEOS_UIC ${_tools_dir}/uic/uic CACHE INTERNAL "uic of the build host")
set(GEOS_UICPP ${_tools_dir}/uicpp/uicpp CACHE INTERNAL
    "uicpp of the build host")
set(_tool_files ${GEOS_GOC} ${GEOS_UIC} ${GEOS_UICPP})
foreach(_tool gpres gpgeode kexports symtabgen papergen asmchars2c
              epsfonts2c kmapgen localgen psdevinfo2c dmdevinfo2c)
  string(TOUPPER ${_tool} _var)
  set(_dir ${_tool})
  if(_tool STREQUAL "gpgeode")
    set(_dir gpres)
  endif()
  set(GEOS_${_var} ${_tools_dir}/${_dir}/${_tool} CACHE INTERNAL
      "${_tool} of the build host")
  list(APPEND _tool_files ${GEOS_${_var}})
endforeach()
ExternalProject_Add(geos_host_tools
  SOURCE_DIR ${PROJECT_SOURCE_DIR}/Tools
  BINARY_DIR ${_tools_dir}
  CMAKE_GENERATOR ${CMAKE_GENERATOR}
  CMAKE_ARGS -DCMAKE_C_COMPILER=${GEOS_HOST_CC}
  INSTALL_COMMAND ""
  BUILD_ALWAYS ON
  BUILD_BYPRODUCTS ${_tool_files}
  USES_TERMINAL_BUILD OFF)
