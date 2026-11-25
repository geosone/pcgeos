# GeosCvtpcx.cmake -- pictures made by cvtpcx (Tools/misc/cvtpcx) from
# PCX files in CMake builds, as the convert scripts beside them do.
#
#   geos_cvtpcx(<file.ui> <file.pcx> [IN <dir>] <cvtpcx options>...)
#
# Runs cvtpcx -q <options> -o<out>/<file.ui> <file.pcx> in the current
# source directory, or its subdirectory <dir> (the PCX file's name goes
# into the output as given: a PCX of the same name as another, such as
# a colour picture replaced later, lies in a directory of its own),
# <out> being ${CMAKE_CURRENT_BINARY_DIR}.  Appends the output to
# GEOS_CVTPCX_OUTPUTS in the caller's scope; a custom target over them
# lets uic wait for them.
include_guard(GLOBAL)
include(${CMAKE_CURRENT_LIST_DIR}/GeosHostTools.cmake)

function(geos_cvtpcx out pcx)
  set(args ${ARGN})
  set(dir ${CMAKE_CURRENT_SOURCE_DIR})
  if(ARGC GREATER 3 AND ARGV2 STREQUAL "IN")
    set(dir ${dir}/${ARGV3})
    list(REMOVE_AT args 0 1)
  endif()
  string(REPLACE "\\" "/" file ${pcx})
  set(o ${CMAKE_CURRENT_BINARY_DIR}/${out})
  add_custom_command(OUTPUT ${o}
    COMMAND ${GEOS_CVTPCX} -q ${args} -o${o} ${pcx}
    WORKING_DIRECTORY ${dir}
    DEPENDS ${dir}/${file} ${GEOS_CVTPCX} geos_host_tools
    COMMENT "cvtpcx ${out}"
    VERBATIM)
  set(GEOS_CVTPCX_OUTPUTS ${GEOS_CVTPCX_OUTPUTS} ${o} PARENT_SCOPE)
endfunction()
