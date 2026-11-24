# GeosSources.cmake -- the source files of a geode, listed per directory.
#
#   geos_sources(<list> <file>...)
#
# In the CMakeLists.txt of a source directory: adds the files (relative
# to that directory) to the named list.
#
#   geos_get_sources(<out-var> <list>)
#
# In the CMakeLists.txt of the geode, after the add_subdirectory() calls:
# the files of the list, with their full paths, in the order they were
# added.
include_guard(GLOBAL)

function(geos_sources list)
  foreach(f IN LISTS ARGN)
    get_filename_component(f ${f} ABSOLUTE)
    set_property(GLOBAL APPEND PROPERTY GEOS_SOURCES_${list} ${f})
  endforeach()
endfunction()

function(geos_get_sources out_var list)
  get_property(files GLOBAL PROPERTY GEOS_SOURCES_${list})
  set(${out_var} ${files} PARENT_SCOPE)
endfunction()
