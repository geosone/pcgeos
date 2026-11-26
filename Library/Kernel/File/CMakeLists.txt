# The C kernel, File/: files and paths.
# Beside the asm they replace:
#   filePath.c <- current and standard paths of File/filePath.asm,
#                      the resolution of paths to a disk
#   fileOpenClose.c, File/fileIO.c, File/fileFile.c <- the same
#                      asm files: open/close, I/O, by-name operations,
#                      over the file system driver of the disk
#   fileEA.c   <- extended attributes (the FileGet/Set...ExtAttributes
#                      of fileFile.asm, fileOpenClose.asm)
#   fileEnum.c <- FileEnum of File/fileEnum.asm, fileC.asm
#   fileLink.c <- links (File/fileLink.asm, fileUtils.asm)
#   fileNotify.c <- file change notification (FSD/fsdFile.asm)
target_sources(geos_kernel_objs PRIVATE
  filePath.c
  fileUtils.c
  fileOpenClose.c
  fileIO.c
  fileFile.c
  fileEA.c
  fileEnum.c
  fileLink.c
  fileNotify.c
  fileEC.c)
