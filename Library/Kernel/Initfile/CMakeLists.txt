# The C kernel, Initfile/: the init file (geos.ini).
# Beside the asm they replace:
#   initfileLow.c, initfileConstruct.c, initfileHigh.c <- the
#                      init file (geos.ini) of the same asm files,
#                      initfileC.asm and Loader/ini.asm
target_sources(geos_kernel_objs PRIVATE
  initfileLow.c
  initfileConstruct.c
  initfileHigh.c)
