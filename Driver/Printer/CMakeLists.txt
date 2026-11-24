# The printer drivers in C: the common code of PrintCom/ is compiled
# into each driver (the lists of its files and geos_dotmatrix_driver()
# are in DotMatrix/DotMatrix.cmake).
include(DotMatrix/DotMatrix.cmake)
add_subdirectory(PScript)
add_subdirectory(Ascii)
add_subdirectory(DotMatrix)
add_subdirectory(HP)
