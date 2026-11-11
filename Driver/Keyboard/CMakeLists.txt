# The C keyboard driver (keyboard.h): the asm driver's design on
# ghal_input.  One geode per keyboard map: the map's kmap*.def becomes C
# through Port32/tools/kmapgen.py, and the geode takes the .gp of the asm
# driver for that map.  The protocol is KEYBOARD_PROTO (4.1, kbdDr.h),
# as glue took it from kbd.ldf.  Geodes need gcc or clang (module loading).
include(${PROJECT_SOURCE_DIR}/cmake/GeosGoc.cmake)
find_package(Python3 COMPONENTS Interpreter)
if(NOT (GEOS_GOC AND Python3_Interpreter_FOUND AND
        CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
  return()
endif()
include(${PROJECT_SOURCE_DIR}/cmake/GeosGeode.cmake)

set(_kbd_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/keyboardProcess.c
  ${CMAKE_CURRENT_SOURCE_DIR}/keyboardInput.c
  ${CMAKE_CURRENT_SOURCE_DIR}/keyboardInit.c)

# geos_keyboard(<geode> <kmap .def> <.gp>)
function(geos_keyboard name kmap gp)
  get_filename_component(_base ${kmap} NAME_WE)
  set(_c ${CMAKE_CURRENT_BINARY_DIR}/${name}_${_base}.c)
  add_custom_command(OUTPUT ${_c}
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Port32/tools/kmapgen.py
            ${CMAKE_CURRENT_SOURCE_DIR}/${kmap} ${_c}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${kmap}
            ${PROJECT_SOURCE_DIR}/Port32/tools/kmapgen.py
    COMMENT "keyboard map ${kmap}"
    VERBATIM)
  geos_geode(${name} ${_kbd_sources} ${_c} GP ${gp}
             PROTOCOL 4.1 INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})
  set(${name}_GEO ${${name}_GEO} PARENT_SCOPE)
endfunction()

geos_keyboard(kbd kmapUS.def kbd.gp)
geos_keyboard(kbde_gr German/Extended/kmapGermanExt.def
              German/Extended/kbde_gr.gp)
set(kbd_GEO ${kbd_GEO} PARENT_SCOPE)
set(kbde_gr_GEO ${kbde_gr_GEO} PARENT_SCOPE)
