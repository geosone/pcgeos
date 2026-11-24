# gpres: the resource numbers of a geode (GOCR_RES_*) for goc -cg;
# gpgeode: the geode's module data (__geos_module) from its .gp file.
# Both read the .gp file and the @start lines of the goc files (gpscan.c).
add_executable(gpres gpres.c gpscan.c)
add_executable(gpgeode gpgeode.c gpscan.c)
