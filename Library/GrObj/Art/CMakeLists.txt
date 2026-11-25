# The pictures of Library/GrObj (the .ui files) made of the PCX files
# here by cvtpcx at build time, into the binary directory: convert2.bat
# in CMake (cmake/GeosCvtpcx.cmake).  The .ui files checked in are the
# 16-bit build's; the ctest library.art.grobj checks that both are the
# same.
#
# Not made here (the CMake build uses the files checked in):
#   mkrLineAttr.ui: cvtpcx does not make it the same (yet)
#   mkrTools.ui: edited by hand after cvtpcx
include(GeosCvtpcx)

# convert
geos_cvtpcx(parts/mkrMoveHC.ui grobj.pcx -f -m2 -t -x169 -y1 -w41 -h25
            -nMoveHC)
geos_cvtpcx(parts/mkrMoveHM.ui grobj.pcx -f -b -m2 -t -x211 -y1 -w41 -h25
            -nMoveHM)
geos_cvtpcx(parts/mkrMoveVC.ui grobj.pcx -f -m2 -t -x169 -y27 -w25 -h31
            -nMoveVC)
geos_cvtpcx(parts/mkrMoveVM.ui grobj.pcx -f -b -m2 -t -x195 -y27 -w25 -h31
            -nMoveVM)
geos_cvtpcx(parts/mkrRotateC.ui grobj.pcx -f -m2 -t -x169 -y59 -w41 -h25
            -nRotateC)
geos_cvtpcx(parts/mkrRotateM.ui grobj.pcx -f -b -m2 -t -x211 -y59 -w41 -h25
            -nRotateM)
geos_cvtpcx(parts/mkrScaleHC.ui grobj.pcx -f -m2 -t -x169 -y85 -w41 -h25
            -nScaleHC)
geos_cvtpcx(parts/mkrScaleHM.ui grobj.pcx -f -b -m2 -t -x211 -y85 -w41 -h25
            -nScaleHM)
geos_cvtpcx(parts/mkrScaleVC.ui grobj.pcx -f -m2 -t -x169 -y111 -w25 -h41
            -nScaleVC)
geos_cvtpcx(parts/mkrScaleVM.ui grobj.pcx -f -b -m2 -t -x195 -y111 -w25 -h41
            -nScaleVM)
geos_cvtpcx(parts/mkrSkewHC.ui grobj.pcx -f -m2 -t -x169 -y153 -w41 -h25
            -nSkewHC)
geos_cvtpcx(parts/mkrSkewHM.ui grobj.pcx -f -b -m2 -t -x211 -y153 -w41 -h25
            -nSkewHM)
geos_cvtpcx(parts/mkrSkewVC.ui grobj.pcx -f -m2 -t -x169 -y179 -w25 -h41
            -nSkewVC)
geos_cvtpcx(parts/mkrSkewVM.ui grobj.pcx -f -b -m2 -t -x195 -y179 -w25 -h41
            -nSkewVM)
geos_cvtpcx(parts/mkrArcStartAngleC.ui grobj.pcx -f -m2 -t -x361 -y1 -w35
            -h40 -nArcStartAngleC)
geos_cvtpcx(parts/mkrArcStartAngleM.ui grobj.pcx -f -b -m2 -t -x397 -y1 -w35
            -h40 -nArcStartAngleM)
geos_cvtpcx(parts/mkrArcEndAngleC.ui grobj.pcx -f -m2 -t -x361 -y42 -w35
            -h37 -nArcEndAngleC)
geos_cvtpcx(parts/mkrArcEndAngleM.ui grobj.pcx -f -b -m2 -t -x397 -y42 -w35
            -h37 -nArcEndAngleM)
geos_cvtpcx(parts/mkrPieC.ui grobj.pcx -f -m2 -t -x361 -y80 -w53 -h39
            -nPieC)
geos_cvtpcx(parts/mkrPieM.ui grobj.pcx -f -b -m2 -t -x415 -y80 -w53 -h39
            -nPieM)
geos_cvtpcx(parts/mkrChordC.ui grobj.pcx -f -m2 -t -x361 -y120 -w53 -h15
            -nChordC)
geos_cvtpcx(parts/mkrChordM.ui grobj.pcx -f -b -m2 -t -x415 -y120 -w53 -h15
            -nChordM)
geos_cvtpcx(parts/mkrBringToFront.ui grobj.pcx -f -m2 -t -z -x0 -rGrObj
            -y208 -nBringToFront)
geos_cvtpcx(parts/mkrSendToBack.ui grobj.pcx -f -m2 -t -z -x0 -rGrObj -y224
            -nSendToBack)
geos_cvtpcx(parts/mkrShuffleUp.ui grobj.pcx -f -m2 -t -z -x48 -rGrObj -y208
            -nShuffleUp)
geos_cvtpcx(parts/mkrShuffleDown.ui grobj.pcx -f -m2 -t -z -x48 -rGrObj
            -y224 -nShuffleDown)
geos_cvtpcx(parts/mkrGroup.ui grobj.pcx -f -m2 -t -z -x0 -rGrObj -y240
            -nGroup)
geos_cvtpcx(parts/mkrUngroup.ui grobj.pcx -f -m2 -t -z -x0 -rGrObj -y256
            -nUngroup)
geos_cvtpcx(parts/mkrPasteInside.ui grobj.pcx -f -m2 -t -z -x0 -rGrObj -y272
            -nPasteInside)
geos_cvtpcx(parts/mkrBreakoutPasteInside.ui grobj.pcx -f -m2 -t -z -x0
            -rGrObj -y288 -nBreakoutPasteInside)
geos_cvtpcx(parts/mkrDuplicate.ui grobj.pcx -f -m2 -t -z -x0 -rGrObj -y304
            -nDuplicate)
geos_cvtpcx(parts/mkrClone.ui grobj.pcx -f -m2 -t -z -x0 -rGrObj -y320
            -nClone)
geos_cvtpcx(parts/mkrFlipLeftToRight.ui grobj.pcx -f -m2 -t -z -x0 -rGrObj
            -y336 -nFlipLeftToRight)
geos_cvtpcx(parts/mkrFlipTopToBottom.ui grobj.pcx -f -m2 -t -z -x0 -rGrObj
            -y352 -nFlipTopToBottom)
geos_cvtpcx(parts/mkrHorizontalGradientColor.ui grobj.pcx -f -m2 -t -x133
            -y289 -w52 -h25 -nHorizontalGradientColor)
geos_cvtpcx(parts/mkrHorizontalGradientMono.ui grobj.pcx -f -m2 -t -x186
            -y289 -w52 -h25 -b -nHorizontalGradientMono)
geos_cvtpcx(parts/mkrVerticalGradientColor.ui grobj.pcx -f -m2 -t -x133
            -y315 -w52 -h25 -nVerticalGradientColor)
geos_cvtpcx(parts/mkrVerticalGradientMono.ui grobj.pcx -f -m2 -t -x186 -y315
            -w52 -h25 -b -nVerticalGradientMono)
geos_cvtpcx(parts/mkrRadialRectGradientColor.ui grobj.pcx -f -m2 -t -x133
            -y341 -w52 -h25 -nRadialRectGradientColor)
geos_cvtpcx(parts/mkrRadialRectGradientMono.ui grobj.pcx -f -m2 -t -x186
            -y341 -w52 -h25 -b -nRadialRectGradientMono)
geos_cvtpcx(parts/mkrRadialEllipseGradientColor.ui grobj.pcx -f -m2 -t -x133
            -y367 -w52 -h25 -nRadialEllipseGradientColor)
geos_cvtpcx(parts/mkrRadialEllipseGradientMono.ui grobj.pcx -f -m2 -t -x186
            -y367 -w52 -h25 -b -nRadialEllipseGradientMono)
geos_cvtpcx(parts/mkrAlignLeftC.ui grobj.pcx -f -m2 -t -x97 -y1 -w35 -h17
            -nAlignLeftC)
geos_cvtpcx(parts/mkrAlignCenterHC.ui grobj.pcx -f -m2 -t -x97 -y19 -w35
            -h17 -nAlignCenterHC)
geos_cvtpcx(parts/mkrAlignRightC.ui grobj.pcx -f -m2 -t -x97 -y37 -w35 -h17
            -nAlignRightC)
geos_cvtpcx(parts/mkrAlignWidthC.ui grobj.pcx -f -m2 -t -x97 -y55 -w35 -h17
            -nAlignWidthC)
geos_cvtpcx(parts/mkrAlignTopC.ui grobj.pcx -f -m2 -t -x97 -y73 -w17 -h35
            -nAlignTopC)
geos_cvtpcx(parts/mkrAlignCenterVC.ui grobj.pcx -f -m2 -t -x97 -y109 -w17
            -h35 -nAlignCenterVC)
geos_cvtpcx(parts/mkrAlignBottomC.ui grobj.pcx -f -m2 -t -x97 -y145 -w17
            -h35 -nAlignBottomC)
geos_cvtpcx(parts/mkrAlignHeightC.ui grobj.pcx -f -m2 -t -x97 -y181 -w17
            -h35 -nAlignHeightC)
geos_cvtpcx(parts/mkrDistributeLeftC.ui grobj.pcx -f -m2 -t -x97 -y217 -w35
            -h17 -nDistributeLeftC)
geos_cvtpcx(parts/mkrDistributeCenterHC.ui grobj.pcx -f -m2 -t -x97 -y235
            -w35 -h17 -nDistributeCenterHC)
geos_cvtpcx(parts/mkrDistributeRightC.ui grobj.pcx -f -m2 -t -x97 -y253 -w35
            -h17 -nDistributeRightC)
geos_cvtpcx(parts/mkrDistributeWidthC.ui grobj.pcx -f -m2 -t -x97 -y271 -w35
            -h17 -nDistributeWidthC)
geos_cvtpcx(parts/mkrDistributeTopC.ui grobj.pcx -f -m2 -t -x97 -y289 -w17
            -h35 -nDistributeTopC)
geos_cvtpcx(parts/mkrDistributeCenterVC.ui grobj.pcx -f -m2 -t -x97 -y325
            -w17 -h35 -nDistributeCenterVC)
geos_cvtpcx(parts/mkrDistributeBottomC.ui grobj.pcx -f -m2 -t -x97 -y361
            -w17 -h35 -nDistributeBottomC)
geos_cvtpcx(parts/mkrDistributeHeightC.ui grobj.pcx -f -m2 -t -x97 -y397
            -w17 -h35 -nDistributeHeightC)
geos_cvtpcx(parts/mkrAlignLeftM.ui grobj.pcx -f -b -m2 -t -x133 -y1 -w35
            -h17 -nAlignLeftM)
geos_cvtpcx(parts/mkrAlignCenterHM.ui grobj.pcx -f -b -m2 -t -x133 -y19 -w35
            -h17 -nAlignCenterHM)
geos_cvtpcx(parts/mkrAlignRightM.ui grobj.pcx -f -b -m2 -t -x133 -y37 -w35
            -h17 -nAlignRightM)
geos_cvtpcx(parts/mkrAlignWidthM.ui grobj.pcx -f -b -m2 -t -x133 -y55 -w35
            -h17 -nAlignWidthM)
geos_cvtpcx(parts/mkrAlignTopM.ui grobj.pcx -f -b -m2 -t -x115 -y73 -w17
            -h35 -nAlignTopM)
geos_cvtpcx(parts/mkrAlignCenterVM.ui grobj.pcx -f -b -m2 -t -x115 -y109
            -w17 -h35 -nAlignCenterVM)
geos_cvtpcx(parts/mkrAlignBottomM.ui grobj.pcx -f -b -m2 -t -x115 -y145 -w17
            -h35 -nAlignBottomM)
geos_cvtpcx(parts/mkrAlignHeightM.ui grobj.pcx -f -b -m2 -t -x115 -y181 -w17
            -h35 -nAlignHeightM)
geos_cvtpcx(parts/mkrDistributeLeftM.ui grobj.pcx -f -b -m2 -t -x133 -y217
            -w35 -h17 -nDistributeLeftM)
geos_cvtpcx(parts/mkrDistributeCenterHM.ui grobj.pcx -f -b -m2 -t -x133
            -y235 -w35 -h17 -nDistributeCenterHM)
geos_cvtpcx(parts/mkrDistributeRightM.ui grobj.pcx -f -b -m2 -t -x133 -y253
            -w35 -h17 -nDistributeRightM)
geos_cvtpcx(parts/mkrDistributeWidthM.ui grobj.pcx -f -b -m2 -t -x133 -y271
            -w35 -h17 -nDistributeWidthM)
geos_cvtpcx(parts/mkrDistributeTopM.ui grobj.pcx -f -b -m2 -t -x115 -y289
            -w17 -h35 -nDistributeTopM)
geos_cvtpcx(parts/mkrDistributeCenterVM.ui grobj.pcx -f -b -m2 -t -x115
            -y325 -w17 -h35 -nDistributeCenterVM)
geos_cvtpcx(parts/mkrDistributeBottomM.ui grobj.pcx -f -b -m2 -t -x115 -y361
            -w17 -h35 -nDistributeBottomM)
geos_cvtpcx(parts/mkrDistributeHeightM.ui grobj.pcx -f -b -m2 -t -x115 -y397
            -w17 -h35 -nDistributeHeightM)
geos_cvtpcx(parts/mkrAlignToGridLeftTopColor.ui grobj.pcx -f -m2 -t -x253
            -y1 -w39 -h39 -nAlignToGridLeftTopColor)
geos_cvtpcx(parts/mkrAlignToGridRightTopColor.ui grobj.pcx -f -m2 -t -x253
            -y41 -w39 -h39 -nAlignToGridRightTopColor)
geos_cvtpcx(parts/mkrAlignToGridRightBottomColor.ui grobj.pcx -f -m2 -t
            -x253 -y81 -w39 -h39 -nAlignToGridRightBottomColor)
geos_cvtpcx(parts/mkrAlignToGridLeftBottomColor.ui grobj.pcx -f -m2 -t -x253
            -y121 -w39 -h39 -nAlignToGridLeftBottomColor)
geos_cvtpcx(parts/mkrAlignToGridLeftColor.ui grobj.pcx -f -m2 -t -x253 -y161
            -w39 -h39 -nAlignToGridLeftColor)
geos_cvtpcx(parts/mkrAlignToGridTopColor.ui grobj.pcx -f -m2 -t -x253 -y201
            -w39 -h39 -nAlignToGridTopColor)
geos_cvtpcx(parts/mkrAlignToGridRightColor.ui grobj.pcx -f -m2 -t -x253
            -y241 -w39 -h39 -nAlignToGridRightColor)
geos_cvtpcx(parts/mkrAlignToGridBottomColor.ui grobj.pcx -f -m2 -t -x253
            -y281 -w39 -h39 -nAlignToGridBottomColor)
geos_cvtpcx(parts/mkrAlignToGridCenterColor.ui grobj.pcx -f -m2 -t -x253
            -y321 -w39 -h39 -nAlignToGridCenterColor)
geos_cvtpcx(parts/mkrAlignToGridLeftTopMono.ui grobj.pcx -f -m2 -b -t -x293
            -y1 -w39 -h39 -nAlignToGridLeftTopMono)
geos_cvtpcx(parts/mkrAlignToGridRightTopMono.ui grobj.pcx -f -m2 -b -t -x293
            -y41 -w39 -h39 -nAlignToGridRightTopMono)
geos_cvtpcx(parts/mkrAlignToGridRightBottomMono.ui grobj.pcx -f -m2 -b -t
            -x293 -y81 -w39 -h39 -nAlignToGridRightBottomMono)
geos_cvtpcx(parts/mkrAlignToGridLeftBottomMono.ui grobj.pcx -f -m2 -b -t
            -x293 -y121 -w39 -h39 -nAlignToGridLeftBottomMono)
geos_cvtpcx(parts/mkrAlignToGridLeftMono.ui grobj.pcx -f -m2 -b -t -x293
            -y161 -w39 -h39 -nAlignToGridLeftMono)
geos_cvtpcx(parts/mkrAlignToGridTopMono.ui grobj.pcx -f -m2 -b -t -x293
            -y201 -w39 -h39 -nAlignToGridTopMono)
geos_cvtpcx(parts/mkrAlignToGridRightMono.ui grobj.pcx -f -m2 -b -t -x293
            -y241 -w39 -h39 -nAlignToGridRightMono)
geos_cvtpcx(parts/mkrAlignToGridBottomMono.ui grobj.pcx -f -m2 -b -t -x293
            -y281 -w39 -h39 -nAlignToGridBottomMono)
geos_cvtpcx(parts/mkrAlignToGridCenterMono.ui grobj.pcx -f -m2 -b -t -x293
            -y321 -w39 -h39 -nAlignToGridCenterMono)

# convert2.bat
geos_cvtpcx(mkrTextToolTC.ui A110-8.pcx -m225 -x0 -y0 -2 -nTextToolTC -w15
            -h15 -Stool -stiny)

# the pictures put together (cat)
geos_cvtpcx_cat(mkrMove.ui
  "=start GrObjOtherColorMonikerResource, data;"
  parts/mkrMoveHC.ui
  parts/mkrMoveVC.ui
  "=end GrObjOtherColorMonikerResource;"
  "=start GrObjOtherMonoMonikerResource, data;"
  parts/mkrMoveHM.ui
  parts/mkrMoveVM.ui
  "=end GrObjOtherMonoMonikerResource;")
geos_cvtpcx_cat(mkrRotate.ui
  "=start GrObjOtherColorMonikerResource, data;"
  parts/mkrRotateC.ui
  "=end GrObjOtherColorMonikerResource;"
  "=start GrObjOtherMonoMonikerResource, data;"
  parts/mkrRotateM.ui
  "=end GrObjOtherMonoMonikerResource;")
geos_cvtpcx_cat(mkrScale.ui
  "=start GrObjOtherColorMonikerResource, data;"
  parts/mkrScaleHC.ui
  parts/mkrScaleVC.ui
  "=end GrObjOtherColorMonikerResource;"
  "=start GrObjOtherMonoMonikerResource, data;"
  parts/mkrScaleHM.ui
  parts/mkrScaleVM.ui
  "=end GrObjOtherMonoMonikerResource;")
geos_cvtpcx_cat(mkrSkew.ui
  "=start GrObjOtherColorMonikerResource, data;"
  parts/mkrSkewHC.ui
  parts/mkrSkewVC.ui
  "=end GrObjOtherColorMonikerResource;"
  "=start GrObjOtherMonoMonikerResource, data;"
  parts/mkrSkewHM.ui
  parts/mkrSkewVM.ui
  "=end GrObjOtherMonoMonikerResource;")
geos_cvtpcx_cat(mkrArc.ui
  "=start GrObjOtherColorMonikerResource, data;"
  parts/mkrArcEndAngleC.ui
  parts/mkrArcStartAngleC.ui
  parts/mkrPieC.ui
  parts/mkrChordC.ui
  "=end GrObjOtherColorMonikerResource;"
  "=start GrObjOtherMonoMonikerResource, data;"
  parts/mkrArcEndAngleM.ui
  parts/mkrArcStartAngleM.ui
  parts/mkrPieM.ui
  parts/mkrChordM.ui
  "=end GrObjOtherMonoMonikerResource;")
geos_cvtpcx_cat(mkrDepth.ui
  parts/mkrBringToFront.ui
  parts/mkrSendToBack.ui
  parts/mkrShuffleDown.ui
  parts/mkrShuffleUp.ui)
geos_cvtpcx_cat(mkrGroup.ui
  parts/mkrGroup.ui
  parts/mkrUngroup.ui)
geos_cvtpcx_cat(mkrEditSpecial.ui
  parts/mkrPasteInside.ui
  parts/mkrBreakoutPasteInside.ui
  parts/mkrDuplicate.ui
  parts/mkrClone.ui)
geos_cvtpcx_cat(mkrFlip.ui
  parts/mkrFlipLeftToRight.ui
  parts/mkrFlipTopToBottom.ui)
geos_cvtpcx_cat(mkrGradients.ui
  "=start GrObjOtherColorMonikerResource, data;"
  parts/mkrHorizontalGradientColor.ui
  parts/mkrRadialEllipseGradientColor.ui
  parts/mkrRadialRectGradientColor.ui
  parts/mkrVerticalGradientColor.ui
  "=end GrObjOtherColorMonikerResource;"
  "=start GrObjOtherMonoMonikerResource, data;"
  parts/mkrHorizontalGradientMono.ui
  parts/mkrRadialEllipseGradientMono.ui
  parts/mkrRadialRectGradientMono.ui
  parts/mkrVerticalGradientMono.ui
  "=end GrObjOtherMonoMonikerResource;")
geos_cvtpcx_cat(mkrAlign.ui
  "=start GrObjAlignColorMonikerResource, data;"
  parts/mkrAlignBottomC.ui
  parts/mkrAlignCenterHC.ui
  parts/mkrAlignCenterVC.ui
  parts/mkrAlignHeightC.ui
  parts/mkrAlignLeftC.ui
  parts/mkrAlignRightC.ui
  parts/mkrAlignTopC.ui
  parts/mkrAlignWidthC.ui
  parts/mkrDistributeBottomC.ui
  parts/mkrDistributeCenterHC.ui
  parts/mkrDistributeCenterVC.ui
  parts/mkrDistributeHeightC.ui
  parts/mkrDistributeLeftC.ui
  parts/mkrDistributeRightC.ui
  parts/mkrDistributeTopC.ui
  parts/mkrDistributeWidthC.ui
  "=end GrObjAlignColorMonikerResource;"
  "=start GrObjAlignMonoMonikerResource, data;"
  parts/mkrAlignBottomM.ui
  parts/mkrAlignCenterHM.ui
  parts/mkrAlignCenterVM.ui
  parts/mkrAlignHeightM.ui
  parts/mkrAlignLeftM.ui
  parts/mkrAlignRightM.ui
  parts/mkrAlignTopM.ui
  parts/mkrAlignWidthM.ui
  parts/mkrDistributeBottomM.ui
  parts/mkrDistributeCenterHM.ui
  parts/mkrDistributeCenterVM.ui
  parts/mkrDistributeHeightM.ui
  parts/mkrDistributeLeftM.ui
  parts/mkrDistributeRightM.ui
  parts/mkrDistributeTopM.ui
  parts/mkrDistributeWidthM.ui
  "=end GrObjAlignMonoMonikerResource;")
geos_cvtpcx_cat(mkrAlignToGrid.ui
  "=start GrObjAlignToGridColorMonikerResource, data;"
  parts/mkrAlignToGridBottomColor.ui
  parts/mkrAlignToGridCenterColor.ui
  parts/mkrAlignToGridLeftBottomColor.ui
  parts/mkrAlignToGridLeftColor.ui
  parts/mkrAlignToGridLeftTopColor.ui
  parts/mkrAlignToGridRightBottomColor.ui
  parts/mkrAlignToGridRightColor.ui
  parts/mkrAlignToGridRightTopColor.ui
  parts/mkrAlignToGridTopColor.ui
  "=end GrObjAlignToGridColorMonikerResource;"
  "=start GrObjAlignToGridMonoMonikerResource, data;"
  parts/mkrAlignToGridBottomMono.ui
  parts/mkrAlignToGridCenterMono.ui
  parts/mkrAlignToGridLeftBottomMono.ui
  parts/mkrAlignToGridLeftMono.ui
  parts/mkrAlignToGridLeftTopMono.ui
  parts/mkrAlignToGridRightBottomMono.ui
  parts/mkrAlignToGridRightMono.ui
  parts/mkrAlignToGridRightTopMono.ui
  parts/mkrAlignToGridTopMono.ui
  "=end GrObjAlignToGridMonoMonikerResource;")

geos_art(grobj EXCEPT mkrLineAttr.ui mkrTools.ui)
