# The part of the tools' utility library that goc uses.
add_library(geos_tool_utils STATIC
  fileargs.c
  fileUtil.c
  hash.c
  localize.c)
target_include_directories(geos_tool_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(geos_tool_utils PUBLIC geos_tool_config)
