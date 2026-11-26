# GeosUic.cmake -- .ui files in CMake builds: uic -g writes their
# objects, monikers and chunks as goc source (Tools/uic/outgoc.c), which
# goes through goc -cg like the geode's own .goc files.
#
#   geos_uic(<name> <file.ui> [PROLOG <file.goh>...] [RESOURCES <name>...]
#            [INCLUDES <dir>...] [DEFINITIONS <def>...]
#            [DEPENDS <target or file>...] [OMIT_VARDATA <name>...])
#
# Makes, in ${CMAKE_CURRENT_BINARY_DIR}/uic:
#   <name>.goc	the goc source, a file of the geode: add ${<name>_UI_GOC}
#		to the GOC files of geos_geode().  It @includes the PROLOG
#		files first (the headers of the classes it uses), by
#		their names: their directories must be in the geode's
#		INCLUDES.
#   <name>.goh	"@extern" declarations of all it defines, for the .goc
#		files that use the objects: add ${<name>_UI_DIR} to the
#		geode's INCLUDES and ${<name>_UI_GOH} to its GOC_DEPENDS.
# RESOURCES: only these resources of the file (while a geode has not all
# its classes in C).  INCLUDES and DEFINITIONS are those of the .ui file
# (Include/ is always there).  The .ui and .uih files it includes are
# dependencies (uicpp -M); DEPENDS: what must be made first (the
# pictures of an Art directory, geos_art()).  OMIT_VARDATA: vardata uic
# does not write (uic -N): goc takes no vardata of another class on an
# object, as a specific UI's on a generic one; the code adds it.
include_guard(GLOBAL)
include(${CMAKE_CURRENT_LIST_DIR}/GeosHostTools.cmake)

function(geos_uic name ui_file)
  cmake_parse_arguments(PARSE_ARGV 2 U "" ""
                        "PROLOG;RESOURCES;INCLUDES;DEFINITIONS;DEPENDS;OMIT_VARDATA")
  get_filename_component(ui_file ${ui_file} ABSOLUTE)
  get_filename_component(ui_dir ${ui_file} DIRECTORY)
  set(dir ${CMAKE_CURRENT_BINARY_DIR}/uic)
  set(goc ${dir}/${name}.goc)
  set(goh ${dir}/${name}.goh)
  set(pp_args)
  foreach(inc IN LISTS U_INCLUDES)
    get_filename_component(inc ${inc} ABSOLUTE)
    list(APPEND pp_args -I${inc})
  endforeach()
  list(APPEND pp_args -I${PROJECT_SOURCE_DIR}/Include)
  foreach(def IN LISTS U_DEFINITIONS)
    list(APPEND pp_args -D${def})
  endforeach()
  set(args)
  foreach(res IN LISTS U_RESOURCES)
    list(APPEND args -G ${res})
  endforeach()
  foreach(v IN LISTS U_OMIT_VARDATA)
    list(APPEND args -N ${v})
  endforeach()
  set(prologs)
  foreach(p IN LISTS U_PROLOG)
    # (goc finds it by its name in the geode's INCLUDES)
    get_filename_component(p ${p} ABSOLUTE)
    get_filename_component(p_name ${p} NAME)
    list(APPEND args -i ${p_name})
    list(APPEND prologs ${p})
  endforeach()
  add_custom_command(OUTPUT ${goc} ${goh}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${dir}
    COMMAND ${GEOS_UIC} -g -p ${GEOS_UICPP} ${pp_args} ${args}
            -x ${goh} -o ${goc} ${ui_file}
    COMMAND ${CMAKE_COMMAND} -D UICPP=${GEOS_UICPP} -D IN=${ui_file}
            -D OUT=${goc}.d -D TARGET=${goc} "-DARGS=${pp_args}"
            -P ${PROJECT_SOURCE_DIR}/cmake/GeosUicDeps.cmake
    WORKING_DIRECTORY ${ui_dir}
    DEPENDS ${ui_file} ${prologs} ${GEOS_UIC} ${GEOS_UICPP} geos_host_tools
            ${PROJECT_SOURCE_DIR}/cmake/GeosUicDeps.cmake ${U_DEPENDS}
    DEPFILE ${goc}.d
    COMMENT "uic -g ${name}"
    VERBATIM)
  set(${name}_UI_GOC ${goc} PARENT_SCOPE)
  set(${name}_UI_GOH ${goh} PARENT_SCOPE)
  set(${name}_UI_DIR ${dir} PARENT_SCOPE)
endfunction()
