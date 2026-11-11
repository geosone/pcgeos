# GHAL core library (Port32/GHAL-Concept.md).  One backend per host.
add_library(ghal STATIC)
target_include_directories(ghal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(GEOS_HOST STREQUAL "linux")
  target_sources(ghal PRIVATE posix/ghal_posix.c elf/ghal_module_elf.c)
  find_package(Threads REQUIRED)
  target_link_libraries(ghal PUBLIC Threads::Threads)
elseif(GEOS_HOST STREQUAL "dos32")
  target_sources(ghal PRIVATE dos32/ghal_dos32.c)
  # geodes are DXE3 modules (dxe3gen); the Watcom build has no loader
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_sources(ghal PRIVATE dos32/ghal_module_dxe.c)
  else()
    target_sources(ghal PRIVATE dos32/ghal_module_none.c)
  endif()
else()
  message(FATAL_ERROR "No GHAL backend for GEOS_HOST=${GEOS_HOST} yet")
endif()
