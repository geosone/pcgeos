# The C kernel, FSD/: the file system drivers' side of the kernel.
# Beside the asm they replace:
#   fsdDrive.c  <- the drive table of FSD/fsdDrive.asm (FSDInitDrive)
#   fsdInit.c   <- the primary driver of FSD/fsdInit.asm (the HostFS
#                      name layout from [system] primaryFSD)
target_sources(geos_kernel_objs PRIVATE
  fsdDrive.c
  fsdInit.c
  fsdFile.c
  fsdVariable.c)
