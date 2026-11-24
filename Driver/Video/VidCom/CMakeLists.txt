# What the video drivers share (beside the vidcom*.asm); compiled into
# each driver.
geos_sources(vidcom
  vidcomEntry.c vidcomOutput.c vidcomLine.c vidcomRaster.c
  vidcomPolygon.c vidcomRegion.c vidcomPutLine.c vidcomChars.c
  vidcomXOR.c vidcomUnder.c vidcomPalette.c)
