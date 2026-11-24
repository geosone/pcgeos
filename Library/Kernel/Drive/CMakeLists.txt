# The C kernel, Drive/: drives.
# Beside the asm they replace:
#   driveHigh.c <- the drive API of Drive/driveHigh.asm, driveC.asm
target_sources(geos_kernel_objs PRIVATE
  driveHigh.c)
