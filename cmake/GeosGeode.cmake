# GeosGeode.cmake -- build geodes in the module format of the host.
#
#   geos_geode(<name> <source>... [GP <geode.gp> [GOC <file.goc>...]
#              [REV <geode.rev>] [PROTOCOL <major.minor>]]
#              [INCLUDES dir...] [DEFINITIONS def...] [OPTIONS opt...])
#
# Builds <name>.geo in the current binary directory and the target
# geode_<name>; the path of the file goes to the variable <name>_GEO in
# the caller's scope.  With GP it is a GEOS geode: the .goc files go
# through goc -cg (GeosGoc.cmake) and Port32/tools/gpgeode.py writes the
# GEOS data of the module (__geos_module, CInclude/geosmod.h) from
# the .gp file and the .rev file (default: <geode>.rev next to the .gp
# file, if there is one).  PROTOCOL gives the protocol where the .rev
# file does not have it (drivers: glue took theirs from the .ldf file of
# their protocol, e.g. kbd.ldf for KEYBOARD_PROTO).  The format is the host's own (Port32/GHAL-Concept.md
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
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "GP;REV;PROTOCOL"
                        "GOC;INCLUDES;DEFINITIONS;OPTIONS")
  set(sources ${ARG_UNPARSED_ARGUMENTS})
  set(geo ${CMAKE_CURRENT_BINARY_DIR}/${name}.geo)
  set(incs ${ARG_INCLUDES})
  set(defs ${ARG_DEFINITIONS})
  set(opts ${_geos_geode_cflags} ${ARG_OPTIONS})

  if(ARG_GP)
    include(${PROJECT_SOURCE_DIR}/cmake/GeosGoc.cmake)
    get_filename_component(gp ${ARG_GP} ABSOLUTE)
    get_filename_component(gp_dir ${gp} DIRECTORY)
    get_filename_component(geode ${gp} NAME_WE)
    set(gocs)
    foreach(g IN LISTS ARG_GOC)
      get_filename_component(g ${g} ABSOLUTE)
      list(APPEND gocs ${g})
    endforeach()
    geos_goc(wrappers ${gp} ${gocs} INCLUDES ${ARG_INCLUDES})
    set(dir ${CMAKE_CURRENT_BINARY_DIR}/goc/${geode})
    set(module_c ${dir}/${geode}_module.c)
    set(rev ${ARG_REV})
    if(NOT rev AND EXISTS ${gp_dir}/${geode}.rev)
      set(rev ${gp_dir}/${geode}.rev)
    endif()
    set(rev_args)
    if(rev)
      set(rev_args --rev ${rev})
    endif()
    if(ARG_PROTOCOL)
      list(APPEND rev_args --protocol ${ARG_PROTOCOL})
    endif()
    add_custom_command(OUTPUT ${module_c}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${dir}
      COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Port32/tools/gpgeode.py
              -o ${module_c} ${rev_args} --res ${geode}_res.h
              --objs ${geode}_objs.h ${gp} ${gocs}
      DEPENDS ${gp} ${gocs} ${rev} ${PROJECT_SOURCE_DIR}/Port32/tools/gpgeode.py
              ${PROJECT_SOURCE_DIR}/Port32/tools/gpres.py
      COMMENT "gpgeode ${geode}"
      VERBATIM)
    set_source_files_properties(${module_c} PROPERTIES
      OBJECT_DEPENDS "${dir}/${geode}_res.h;${dir}/${geode}_objs.h")
    list(APPEND sources ${wrappers} ${module_c})
    # GEOS's own C library headers (CInclude/Ansi) before the host's:
    # a geode imports the C functions from GEOS, not from the host
    list(APPEND incs ${gp_dir} ${PROJECT_SOURCE_DIR}/CInclude
         ${PROJECT_SOURCE_DIR}/CInclude/Ansi ${PROJECT_SOURCE_DIR}/CInclude)
    list(APPEND defs GEOS_PORT32)
  endif()

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
    target_compile_options(geode_${name}_obj PRIVATE ${opts})
    target_include_directories(geode_${name}_obj PRIVATE ${incs})
    target_compile_definitions(geode_${name}_obj PRIVATE ${defs})
    # dxe3gen writes dxe_tmp.* into the current directory: one directory
    # per geode, so parallel builds do not collide
    set(work ${CMAKE_CURRENT_BINARY_DIR}/dxe3gen/${name})
    file(MAKE_DIRECTORY ${work})
    add_custom_command(OUTPUT ${geo}
      COMMAND ${GEOS_DXE3GEN} -U -o ${geo} $<TARGET_OBJECTS:geode_${name}_obj>
              ${_libgcc}
      WORKING_DIRECTORY ${work}
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
    target_compile_options(geode_${name} PRIVATE ${opts})
    target_include_directories(geode_${name} PRIVATE ${incs})
    target_compile_definitions(geode_${name} PRIVATE ${defs})
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
