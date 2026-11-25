# GeosCvtpcx.cmake -- pictures made by cvtpcx (Tools/misc/cvtpcx) from
# PCX files in CMake builds, as the convert scripts beside them do.
#
#   geos_cvtpcx(<file.ui> <file.pcx> [IN <dir>] [STRIP_CHUNK]
#               <cvtpcx options>...)
#
# Runs cvtpcx -q <options> -o<out>/<file.ui> <file.pcx> in the current
# source directory, or its subdirectory <dir> (the PCX file's name goes
# into the output as given: a PCX of the same name as another, such as
# a colour picture replaced later, lies in a directory of its own),
# <out> being ${CMAKE_CURRENT_BINARY_DIR}.  STRIP_CHUNK: then the chunk
# name and the braces of a -g picture are taken out, as the sed of
# Library/Kernel/Art/convert does (GeosCvtpcxStrip.cmake).  Appends the
# output to
# GEOS_CVTPCX_OUTPUTS in the caller's scope; geos_art() makes a target
# of them.
include_guard(GLOBAL)
include(${CMAKE_CURRENT_LIST_DIR}/GeosHostTools.cmake)

function(geos_cvtpcx out pcx)
  set(args ${ARGN})
  set(dir ${CMAKE_CURRENT_SOURCE_DIR})
  if(ARGC GREATER 3 AND ARGV2 STREQUAL "IN")
    set(dir ${dir}/${ARGV3})
    list(REMOVE_AT args 0 1)
  endif()
  set(post)
  list(FIND args STRIP_CHUNK i)
  if(i GREATER -1)
    list(REMOVE_AT args ${i})
    set(post COMMAND ${CMAKE_COMMAND} -DFILE=${CMAKE_CURRENT_BINARY_DIR}/${out}
             -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GeosCvtpcxStrip.cmake)
  endif()
  string(REPLACE "\\" "/" file ${pcx})
  set(o ${CMAKE_CURRENT_BINARY_DIR}/${out})
  get_filename_component(od ${o} DIRECTORY)
  file(MAKE_DIRECTORY ${od})
  add_custom_command(OUTPUT ${o}
    COMMAND ${GEOS_CVTPCX} -q ${args} -o${o} ${pcx}
    ${post}
    WORKING_DIRECTORY ${dir}
    DEPENDS ${dir}/${file} ${GEOS_CVTPCX} geos_host_tools
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GeosCvtpcxStrip.cmake
    COMMENT "cvtpcx ${out}"
    VERBATIM)
  set(GEOS_CVTPCX_OUTPUTS ${GEOS_CVTPCX_OUTPUTS} ${o} PARENT_SCOPE)
endfunction()

#   geos_cvtpcx_cat(<file.ui> <part>...)
#
# <file.ui> made of the parts one after the other, as the cat and echo
# of a convert script put it together (Library/GrObj/Art/convert): a
# part is a picture made by geos_cvtpcx (parts/<x>.ui, say), or "=text",
# a line of its own.
function(geos_cvtpcx_cat out)
  set(o ${CMAKE_CURRENT_BINARY_DIR}/${out})
  set(parts)
  set(deps)
  set(n 0)
  # ARGV<i>, not ARGN: a line may have a ; of its own
  math(EXPR last "${ARGC} - 1")
  foreach(i RANGE 1 ${last})
    set(p "${ARGV${i}}")
    if(p MATCHES "^=(.*)")
      math(EXPR n "${n} + 1")
      set(t ${CMAKE_CURRENT_BINARY_DIR}/parts/${out}.${n})
      file(WRITE ${t} "${CMAKE_MATCH_1}\n")
      list(APPEND parts ${t})
    else()
      list(APPEND parts ${CMAKE_CURRENT_BINARY_DIR}/${p})
      list(APPEND deps ${CMAKE_CURRENT_BINARY_DIR}/${p})
    endif()
  endforeach()
  add_custom_command(OUTPUT ${o}
    COMMAND ${CMAKE_COMMAND} -E cat ${parts} > ${o}
    DEPENDS ${deps}
    COMMENT "cat ${out}")
  set(GEOS_CVTPCX_OUTPUTS ${GEOS_CVTPCX_OUTPUTS} ${o} PARENT_SCOPE)
endfunction()

#   geos_art(<name> [EXCEPT <file.ui>...])
#
# After the geos_cvtpcx() calls of an Art directory: the target
# <name>_art (built with all) over its pictures, and the directory
# registered for the ctest library.art.<name> (Port32/Tests/Library):
# every .ui file checked in there is made with the same text, but those
# of EXCEPT (written by hand, or of PCX files not there: noted in the
# directory's CMakeLists.txt), which the CMake build uses as they are.
function(geos_art name)
  cmake_parse_arguments(PARSE_ARGV 1 A "" "" "EXCEPT")
  add_custom_target(${name}_art ALL DEPENDS ${GEOS_CVTPCX_OUTPUTS})
  string(REPLACE ";" "," except "${A_EXCEPT}")
  set_property(GLOBAL APPEND PROPERTY GEOS_ART_DIRS
    "${name}|${CMAKE_CURRENT_SOURCE_DIR}|${CMAKE_CURRENT_BINARY_DIR}|${except}")
endfunction()
