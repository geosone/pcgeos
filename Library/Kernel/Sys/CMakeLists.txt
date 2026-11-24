# The C kernel, Sys/: system services: fatal errors, shutdown, statistics, notifications.
# Beside the asm they replace:
#   sysError.c     <- fatal errors, SysNotify of Sys/sysError.asm
#   sysMisc.c   <- SysShutdown, exit flags, Util hex of sysMisc.asm
#   sysStats.c  <- SysGetInfo, SysStatistics of Sys/sysStats.asm
#   sysNotification.c <- SysSendNotification and the hooks of
#                     Sys/sysNotification.asm
target_sources(geos_kernel_objs PRIVATE
  sysError.c
  sysMisc.c
  sysNotification.c
  sysStats.c)
