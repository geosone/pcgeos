# The program that starts GEOS on the host (Loader/main.c, after
# Loader/main.asm): geos, or geos.exe on dos32.
add_executable(geos main.c)
target_link_libraries(geos PRIVATE geos_kernel)
