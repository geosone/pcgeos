# GeosGeode.cmake -- build geodes in the module format of the host.
#
#   geos_geode(<name> <source>... [INCLUDES dir...] [DEFINITIONS def...])
#
# Builds <name>.geo in the current binary directory and the target
# geode_<name>; the path of the file goes to the variable <name>_GEO in
# the caller's scope.  The format is the host's own (Port32/GHAL-Concept.md
# section 4.4, loaded by ghal_module):
#
#   linux (later gonware): ELF shared object, -shared -nostdlib -z now
#   dos32 (DJGPP):         DXE3, made by dxe3gen -U from the objects;
#                          a module with unwind tables (.eh_frame, also
#                          from libgcc) imports __register_frame_info and
#                          __deregister_frame_info, which the kernel takes
#                          from its own libgcc
#
# On ARM, libgcc's division by zero calls raise(SIGFPE): geodes that
# divide import "raise", which the kernel provides (a divide fault).
#
# A geode has no host C library; it may only import what the kernel and
# other geodes export.  libgcc is linked in (64-bit division on 32-bit
# hosts and the like).  No stack protector and no sanitizers: their
# runtime would be host imports.
#
#   geos_embed(<out-var> <file> <symbol>)
#
# Makes a C file that holds <file> as "const unsigned char <symbol>[]"
# and "const unsigned long <symbol>_size", for tests that load a geode
# without a file system.

set(_geos_geode_cflags -ffreestanding -fno-stack-protector -fno-sanitize=all)

function(geos_geode name)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "INCLUDES;DEFINITIONS")
  set(sources ${ARG_UNPARSED_ARGUMENTS})
  set(geo ${CMAKE_CURRENT_BINARY_DIR}/${name}.geo)

  if(GEOS_HOST STREQUAL "dos32")
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
      message(FATAL_ERROR "geos_geode: dos32 geodes need DJGPP gcc")
    endif()
    string(REGEX REPLACE "gcc$" "dxe3gen" _dxe3gen "${CMAKE_C_COMPILER}")
    find_program(GEOS_DXE3GEN NAMES ${_dxe3gen} i586-pc-msdosdjgpp-dxe3gen
                 dxe3gen REQUIRED)
    execute_process(COMMAND ${CMAKE_C_COMPILER} -print-libgcc-file-name
                    OUTPUT_VARIABLE _libgcc OUTPUT_STRIP_TRAILING_WHITESPACE)
    add_library(geode_${name}_obj OBJECT ${sources})
    target_compile_options(geode_${name}_obj PRIVATE ${_geos_geode_cflags})
    target_include_directories(geode_${name}_obj PRIVATE ${ARG_INCLUDES})
    target_compile_definitions(geode_${name}_obj PRIVATE ${ARG_DEFINITIONS})
    add_custom_command(OUTPUT ${geo}
      COMMAND ${GEOS_DXE3GEN} -U -o ${geo} $<TARGET_OBJECTS:geode_${name}_obj>
              ${_libgcc}
      DEPENDS $<TARGET_OBJECTS:geode_${name}_obj>
      COMMAND_EXPAND_LISTS
      COMMENT "dxe3gen ${name}.geo"
      VERBATIM)
    add_custom_target(geode_${name} ALL DEPENDS ${geo})
  else()
    add_library(geode_${name} MODULE ${sources})
    set_target_properties(geode_${name} PROPERTIES
      PREFIX "" SUFFIX ".geo" OUTPUT_NAME ${name}
      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      POSITION_INDEPENDENT_CODE ON)
    target_compile_options(geode_${name} PRIVATE ${_geos_geode_cflags})
    target_include_directories(geode_${name} PRIVATE ${ARG_INCLUDES})
    target_compile_definitions(geode_${name} PRIVATE ${ARG_DEFINITIONS})
    target_link_options(geode_${name} PRIVATE -nostdlib -fno-sanitize=all
      -Wl,-z,now -Wl,--hash-style=both)
    target_link_libraries(geode_${name} PRIVATE gcc)
  endif()
  set(${name}_GEO ${geo} PARENT_SCOPE)
endfunction()

function(geos_embed out_var file symbol)
  set(out_c ${CMAKE_CURRENT_BINARY_DIR}/${symbol}.c)
  add_custom_command(OUTPUT ${out_c}
    COMMAND ${CMAKE_COMMAND} -D IN=${file} -D OUT=${out_c} -D SYMBOL=${symbol}
            -P ${PROJECT_SOURCE_DIR}/cmake/GeosEmbed.cmake
    DEPENDS ${file} ${PROJECT_SOURCE_DIR}/cmake/GeosEmbed.cmake
    COMMENT "embed ${symbol}"
    VERBATIM)
  set(${out_var} ${out_c} PARENT_SCOPE)
endfunction()
