# uicpp, the C preprocessor uic reads .ui files through (GNU cccp).
# cexp.c is the checked-in output of cexp.y, not generated here.
add_executable(uicpp cccp.c cexp.c obstack.c version.c)
target_compile_definitions(uicpp PRIVATE
  GCC_INCLUDE_DIR="unused" GPLUSPLUS_INCLUDE_DIR="unused")
target_include_directories(uicpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uicpp PRIVATE geos_tool_utils)
