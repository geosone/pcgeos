# The user interface library (ui.geo) in C, beside the asm: the visible
# classes (Vis/), GenClass and the generic tree (Gen/), the Flow
# utilities, FlowClass and the input map (User/) so far; each directory
# lists its files.  ui32.gp has the parameters; the 16-bit build keeps
# ui.gp.
if(NOT GEOS_BUILD_GEODES)
  return()
endif()

# the pictures (cvtpcx)
add_subdirectory(Art)
add_subdirectory(Vis)
add_subdirectory(Gen)
add_subdirectory(Proc)
add_subdirectory(User)
add_subdirectory(UI)
add_subdirectory(IACP)
geos_get_sources(_ui_goc ui_goc)
geos_geode(ui GP ui32.gp GOC ${_ui_goc} INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})
