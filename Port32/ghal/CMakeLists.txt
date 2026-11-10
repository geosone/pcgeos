# GHAL core library (Port32/GHAL-Concept.md).  One backend per host.
add_library(ghal STATIC)
target_include_directories(ghal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(GEOS_HOST STREQUAL "linux")
  target_sources(ghal PRIVATE posix/ghal_posix.c)
  find_package(Threads REQUIRED)
  target_link_libraries(ghal PUBLIC Threads::Threads)
else()
  message(FATAL_ERROR "No GHAL backend for GEOS_HOST=${GEOS_HOST} yet")
endif()
