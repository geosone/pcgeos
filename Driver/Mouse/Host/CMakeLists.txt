# The host mouse driver (hostmse.geo), on ghal_input.
geos_geode(hostmse ../mouseCommon.c hostMouse.c
           GP hostmse.gp PROTOCOL 3.2
           INCLUDES ${PROJECT_SOURCE_DIR}/Driver/Mouse)
