# The tests of the port (ctest), a directory per part of the system:
#
#   Math/      the math library          Ghal/     the GHAL
#   Gapi/      GAPI                      CInclude/ the headers under GEOS_PORT32
#   Goc/       goc -cg                   Geode/    the geodes the tests load
#   Uic/       uic -g
#   Kernel/    the C kernel              Driver/   the drivers
#   Library/   the libraries: the UI, text, printing
#   Common/    what several tests share (the stand-in for CUPS)
#
# The programs all land in this binary directory and run there, wherever
# their sources are (Port32/tools/uiboot-shot.sh and the DOS runs count
# on it).
set(GEOS_TESTS_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(GEOS_TESTS_SRC ${CMAKE_CURRENT_SOURCE_DIR})
find_program(GEOS_XVFB Xvfb)
# display tests get a screen of their own (Xvfb) where the display is
# X11; never the desktop
set(GEOS_TEST_XSERVER OFF)
if(GEOS_HOST STREQUAL "linux" AND GHAL_HAVE_X11 AND
   GEOS_LINUX_DISPLAY STREQUAL "x11" AND GEOS_XVFB)
  set(GEOS_TEST_XSERVER ON)
endif()

# geos_test_program(<program> [SOURCES <file>...] [EMBED <name>...]
#                   [LIBS <lib>...] [INCLUDES <dir>...]
#                   [DEFINITIONS <def>...])
# The test program <program> of <program>.c (and SOURCES), linked with
# the kernel (or LIBS).  EMBED: the geodes (or other files, see
# geos_test_embed) it holds as arrays.
function(geos_test_program program)
  cmake_parse_arguments(PARSE_ARGV 1 T "" ""
                        "SOURCES;EMBED;LIBS;INCLUDES;DEFINITIONS")
  if(NOT T_LIBS)
    set(T_LIBS geos_kernel)
  endif()
  add_executable(${program} ${program}.c ${T_SOURCES})
  set_target_properties(${program} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${GEOS_TESTS_DIR})
  target_link_libraries(${program} PRIVATE ${T_LIBS})
  target_include_directories(${program} PRIVATE ${T_INCLUDES})
  target_compile_definitions(${program} PRIVATE ${T_DEFINITIONS})
  foreach(e IN LISTS T_EMBED)
    geos_test_embed(${e})
    target_link_libraries(${program} PRIVATE embed_${e})
  endforeach()
endfunction()

# geos_test_embed(<name> [<file> <symbol>])
# The library embed_<name>: a file as the array <symbol> (geos_embed of
# cmake/GeosGeode.cmake).  Without a file, the geode <name> as
# <name>_geode.  Made once, whoever asks first.
function(geos_test_embed name)
  if(TARGET embed_${name})
    return()
  endif()
  if(ARGC GREATER 1)
    set(file ${ARGV1})
    set(symbol ${ARGV2})
  else()
    set(file ${${name}_GEO})
    set(symbol ${name}_geode)
  endif()
  geos_embed(c ${file} ${symbol})
  add_library(embed_${name} STATIC ${c})
  if(TARGET geode_${name})
    add_dependencies(embed_${name} geode_${name})
  endif()
endfunction()

# geos_add_test(<test> <program> [DISPLAY] [<argument>...])
# The ctest <test>: <program>, run in this directory.  DISPLAY: on a
# screen of its own.  GEOS_AUDIO=null: ghal_audio plays nowhere.
function(geos_add_test test program)
  cmake_parse_arguments(PARSE_ARGV 2 T "DISPLAY" "" "")
  if(T_DISPLAY AND GEOS_TEST_XSERVER)
    add_test(NAME ${test}
             COMMAND ${PROJECT_SOURCE_DIR}/Port32/tools/geos-xserver.sh
                     $<TARGET_FILE:${program}> ${T_UNPARSED_ARGUMENTS}
             WORKING_DIRECTORY ${GEOS_TESTS_DIR})
  else()
    add_test(NAME ${test} COMMAND ${program} ${T_UNPARSED_ARGUMENTS}
             WORKING_DIRECTORY ${GEOS_TESTS_DIR})
  endif()
  # never sound from the speakers: ghal_audio plays nowhere
  set_tests_properties(${test} PROPERTIES ENVIRONMENT GEOS_AUDIO=null)
endfunction()

if(GEOS_BUILD_GEODES)
  # Berkeley, for the tests that measure or draw text; Cooper Black for
  # the TrueType font driver, which no printer has
  geos_test_embed(berkeley ${PROJECT_SOURCE_DIR}/FontData/Berkeley.fnt
                  berkeley_fnt)
  geos_test_embed(cooper ${PROJECT_SOURCE_DIR}/FontData/Ttf/cooper.ttf
                  cooper_ttf)
endif()

# (Geode first: the others embed its geodes)
add_subdirectory(Geode)
add_subdirectory(Math)
add_subdirectory(Ghal)
add_subdirectory(Gapi)
add_subdirectory(CInclude)
add_subdirectory(Goc)
add_subdirectory(Uic)
add_subdirectory(Kernel)
add_subdirectory(Driver)
add_subdirectory(Library)
