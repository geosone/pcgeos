# The C kernel, Win/: the window system.
target_sources(geos_kernel_objs PRIVATE
  winUtils.c
  winWindows.c
  winCreDest.c
  winNotification.c
  winGeode.c
  winIndividual.c
  winState.c
  winTrans.c
  winVariable.c)
