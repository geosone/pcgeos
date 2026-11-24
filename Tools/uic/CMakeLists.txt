# uic, the GEOS user interface compiler.  parse.c is the checked-in
# output of parse.y, not generated here.
add_executable(uic
  map.c
  outgoc.c
  output.c
  parse.c
  scan.c
  semantic.c
  strwid.c
  symbol.c
  uic.c)
target_compile_definitions(uic PRIVATE YYDEBUG=1 LEXDEBUG=1 UIC=1)
target_include_directories(uic PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uic PRIVATE geos_tool_utils)
