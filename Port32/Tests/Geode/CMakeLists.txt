# The geodes the tests load, and the SDK samples.
if(NOT GEOS_HAVE_GOC)
  return()
endif()
# a geode of plain C, for ghal_module (../Ghal)
geos_geode(testgeode testgeode.c)
if(NOT GEOS_BUILD_GEODES)
  return()
endif()

# The SDK_C samples of goc_sdk.txt must keep building through goc -cg,
# each as a geode in the module format of the host (ELF, DXE3 on dos32;
# cmake/GeosGeode.cmake).  Their imports are resolved only when a kernel
# loads them; test geode.hello does that for Hello.  Only errors count
# here; the samples are old code with many harmless warnings.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/goc_sdk.txt _samples REGEX "^[^#]")
set(_sdk_geodes)
foreach(_s IN LISTS _samples)
  set(_dir ${PROJECT_SOURCE_DIR}/Appl/SDK_C/${_s})
  file(GLOB _gp ${_dir}/*.gp)
  file(GLOB _gocs ${_dir}/*.goc)
  list(GET _gp 0 _gp)
  geos_geode(sdk_${_s} GP ${_gp} GOC ${_gocs} OPTIONS -w)
  list(APPEND _sdk_geodes geode_sdk_${_s})
endforeach()
add_custom_target(goc_sdk DEPENDS ${_sdk_geodes})

# a real geode loaded and checked: Hello (no kernel yet: dummy imports)
geos_test_embed(sdk_Hello ${sdk_Hello_GEO} hello_geode)
geos_test_program(test_geode_hello SOURCES geode_hello_check.c LIBS ghal
                  EMBED sdk_Hello)
set_source_files_properties(geode_hello_check.c PROPERTIES
  COMPILE_DEFINITIONS GEOS_PORT32
  INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/CInclude")
geos_add_test(geode.hello test_geode_hello)

# for the kernel's loader (kernel.geode, kernel.boot): the process kgapp,
# its library kglib and a driver
geos_geode(kglib GP kglib.gp GOC kglib.goc)
geos_geode(kgapp GP kgapp.gp GOC kgapp.goc)
geos_geode(kgdrv kgdrv.c GP kgdrv.gp)
# for the sound library's test: a sound driver that logs what it is told
geos_geode(tsnddrv tsnddrv.c GP tsnddrv.gp PROTOCOL 3.0)

# for the tests of the libraries (../Library)
# the visual classes in a window
geos_geode(uivis GP uivis.gp GOC uivis.goc)
# text objects off the screen
geos_geode(textobj GP textobj.gp GOC textobj.goc)
# the flow object between the input manager and two geodes
geos_geode(uiflow GP uiflow.gp GOC uiflow.goc)
# the generic classes, built by a specific UI library of the test
geos_geode(tspui GP tspui.gp GOC tspui.goc PROTOCOL 727.0)
geos_geode(uigen GP uigen.gp GOC uigen.goc)
# the system, screen and field classes
geos_geode(uisys GP uisys.gp GOC uisys.goc)
# an application's life: GenProcessClass, GenApplicationClass
geos_geode(uahost GP uahost.gp GOC uahost.goc)
geos_geode(uiapp GP uiapp.gp GOC uiapp.goc)
# Motif: the system, a screen and a field
geos_geode(uimotif GP uimotif.gp GOC uimotif.goc)
# the application of "[ui] execOnStartup", with a document window
geos_geode(uiboot GP uiboot.gp GOC uiboot.goc)
