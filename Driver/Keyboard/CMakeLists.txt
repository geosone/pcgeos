# The C keyboard driver (keyboard.h): the asm driver's design on
# ghal_input.  One geode per keyboard map: the map's kmap*.def becomes C
# through Port32/tools/kmapgen.py, and the geode takes the .gp of the asm
# driver for that map.  The protocol is KEYBOARD_PROTO (4.1, kbdDr.h),
# as glue took it from kbd.ldf.
if(NOT GEOS_BUILD_GEODES)
  return()
endif()

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
endfunction()

geos_keyboard(kbd kmapUS.def kbd.gp)
geos_keyboard(kbde_gr German/Extended/kmapGermanExt.def
              German/Extended/kbde_gr.gp)
