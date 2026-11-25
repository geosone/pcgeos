# The stream (port) drivers.
add_subdirectory(Cups)
add_subdirectory(Filestr)

# The stream driver (stream.geo) in C, beside stream.asm: the streams
# other drivers use (the sound library).
if(GEOS_BUILD_GEODES)
  # protocol: STREAM_PROTO (streamDr.def), DRIVER_PROTO + 1.0
  geos_geode(stream ${CMAKE_CURRENT_SOURCE_DIR}/stream.c
             GP stream32.gp PROTOCOL 3.0)
endif()
