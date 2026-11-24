# The host display driver (hostvid.geo), on ghal_display.
geos_get_sources(_vidcom vidcom)
geos_geode(hostvid ${_vidcom} hostVideo.c hostPointer.c
           GP hostvid.gp PROTOCOL 5.3
           INCLUDES ${PROJECT_SOURCE_DIR}/Driver/Video/VidCom)
