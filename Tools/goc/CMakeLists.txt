# goc, the GEOS object C preprocessor.  parse.c is the checked-in output
# of parse.y (edited by hand with it), not generated here.
add_executable(goc
  buf.c
  cond.c
  depends.c
  goc.c
  map.c
  output.c
  parse.c
  scan.c
  semantic.c
  stringt.c
  strwid.c
  symbol.c)
target_compile_definitions(goc PRIVATE YYDEBUG=1 LEXDEBUG=1 GOC)
target_include_directories(goc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(goc PRIVATE geos_tool_utils)
