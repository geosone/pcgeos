# The C kernel, IM/: the input manager.
# Beside the asm they replace:
#   imInit.c, imMonitors.c, imMisc.c, imPtr.c, imScreenSaver.c <- the input
#                      manager of imInit.asm, imMonitors.asm, imMisc.asm,
#                      imPtr.asm and imScreenSaver.asm
target_sources(geos_kernel_objs PRIVATE
  imInit.c
  imMonitors.c
  imMisc.c
  imPtr.c
  imScreenSaver.c)
