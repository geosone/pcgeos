# The pictures of Library/Bitmap (the .ui files) made of the PCX files
# here by cvtpcx at build time, into the binary directory: convert in
# CMake (cmake/GeosCvtpcx.cmake).  The .ui files checked in are the
# 16-bit build's; the ctest library.art.bitmap checks that both are the
# same.
#
# Not made here (the CMake build uses the files checked in):
#   mkrBrushTool.ui: its PCX file (bitmaptool.pcx) is not here
#   mkrCreateTool.ui: its PCX file (bitmaptool.pcx) is not here
#   mkrDrawRectTool.ui: cvtpcx does not make it the same (yet)
#   mkrLassoTool.ui: its PCX file (bitmaptool.pcx) is not here
#   mkrRectTool.ui: cvtpcx does not make it the same (yet)
#   mkrResizeTool.ui: its PCX file (bitmaptool.pcx) is not here
include(GeosCvtpcx)

# convert
geos_cvtpcx(mkrSelectionTool.ui grobj.pcx -f -m2 -t -z -x48 -y16 -rBitmap
            -nSelectionTool)
geos_cvtpcx(mkrPencilTool.ui grobj.pcx -f -m2 -t -z -x48 -y32 -rBitmap
            -nPencilTool)
geos_cvtpcx(mkrEraserTool.ui grobj.pcx -f -m2 -t -z -x48 -y48 -rBitmap
            -nEraserTool)
geos_cvtpcx(mkrFloodFillTool.ui grobj.pcx -f -m2 -t -z -x48 -y64 -rBitmap
            -nFloodFillTool)
geos_cvtpcx(mkrFatbitsTool.ui grobj.pcx -f -m2 -t -z -x48 -y80 -rBitmap
            -nFatbitsTool)
geos_cvtpcx(mkrEllipseTool.ui grobj.pcx -f -m2 -t -z -x48 -y96 -rBitmap
            -nEllipseTool)
geos_cvtpcx(mkrLineTool.ui grobj.pcx -f -m2 -t -z -x48 -y112 -rBitmap
            -nLineTool)
geos_cvtpcx(mkrRoundedRectTool.ui grobj.pcx -f -m2 -t -z -x48 -y144 -rBitmap
            -nRoundedRectTool)
geos_cvtpcx(mkrArcTool.ui grobj.pcx -f -m2 -t -z -x48 -y160 -rBitmap
            -nArcTool)
geos_cvtpcx(mkrDrawEllipseTool.ui grobj.pcx -f -m2 -t -z -x48 -y192 -rBitmap
            -nDrawEllipseTool)

geos_art(bitmap EXCEPT
  mkrBrushTool.ui mkrCreateTool.ui mkrDrawRectTool.ui mkrLassoTool.ui mkrRectTool.ui mkrResizeTool.ui)
