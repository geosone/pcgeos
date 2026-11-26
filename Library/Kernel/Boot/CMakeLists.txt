# The C kernel, Boot/: starting and ending GEOS.
# Beside the asm they replace:
#   bootBoot.c <- EndGeos of bootBoot.asm (RemoveGeodes, ExitFile)
#   bootInit.c <- InitGeos and ProcessCommandLine of bootInit.asm,
#                      LoadUI of bootInitfile.asm
target_sources(geos_kernel_objs PRIVATE
  bootInit.c
  bootInitfile.c
  bootBoot.c
  bootVariable.c)
