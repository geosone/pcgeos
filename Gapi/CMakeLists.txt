# GAPI, the GEOS Host API (Port32/GAPI-Concept.md): the system management
# of GEOS (clock, printers, power, network, ...) handed to the host's own
# services.  One backend per host; the interface is CInclude/gapi.h.
add_library(gapi STATIC)
target_include_directories(gapi PUBLIC ${PROJECT_SOURCE_DIR}/CInclude)
target_link_libraries(gapi PUBLIC ghal)

if(GEOS_HOST STREQUAL "linux")
  target_sources(gapi PRIVATE Linux/gapi_linux.c Linux/gapi_dbus.c
                              Linux/gapi_unix.c Linux/gapi_journal.c
                              Linux/gapi_ipp.c)
  # the D-Bus client and the sockets, for the tests
  target_include_directories(gapi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Linux)
elseif(GEOS_HOST STREQUAL "dos32")
  target_sources(gapi PRIVATE Dos32/gapi_dos32.c)
else()
  message(FATAL_ERROR "No GAPI backend for GEOS_HOST=${GEOS_HOST} yet")
endif()
