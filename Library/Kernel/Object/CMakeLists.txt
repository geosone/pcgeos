# The C kernel, Object/: the object system.
# Beside the asm they replace:
#   objectClass.c <- method calls, ObjInstantiate of objectClass.asm
#   objectUtils.c <- master parts, chunk flags, counts, block output
#                          of Object/objectUtils.asm
#   objectVarData.c <- variable data of Object/objectVarData.asm
#   objectMeta.c  <- MetaClass, freeing objects of objectMeta.asm
#   objectProcess.c <- ProcessClass of Object/objectProcess.asm
#   objectReloc.c <- object relocation of Object/objectReloc.asm
#   objectComposite.c <- composites of Object/objectComposite.asm,
#                          CObjSendToChildren of Object/objectC.asm
#   objectLinkable.c <- ObjLinkFindParent of objectLinkable.asm
#   objectShutdown.c <- detach and quit of objectShutdown.asm
#   objectDup.c <- duplicating resources, saved blocks of
#                          objectDup.asm
#   objectFile.c <- the state file of objectFile.asm
target_sources(geos_kernel_objs PRIVATE
  objectClass.c
  objectUtils.c
  objectVarData.c
  objectMeta.c
  objectProcess.c
  objectReloc.c
  objectComposite.c
  objectC.c
  objectLinkable.c
  objectShutdown.c
  objectDup.c
  objectFile.c)
