# HostFS: GEOS file headers and host names on ghal_fs (hostfs.h).  Used
# by the kernel's file routines and by the tests.
add_library(hostfs STATIC hostfs.c)
target_include_directories(hostfs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hostfs PUBLIC ghal)
