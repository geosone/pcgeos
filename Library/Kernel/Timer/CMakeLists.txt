# The C kernel, Timer/: timers, the timer interrupt, date and time.
# Beside the asm they replace:
#   timerList.c  <- timers of Timer/timerList.asm, timerMisc.asm
#   timerInt.c   <- the timer interrupt of Timer/timerInt.asm
#   timerMisc.c  <- date and time of Timer/timerMisc.asm
target_sources(geos_kernel_objs PRIVATE
  timerList.c
  timerInt.c
  timerMisc.c)
