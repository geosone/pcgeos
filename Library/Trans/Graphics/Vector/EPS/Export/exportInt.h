/*
 * exportInt.h -- inside the export side of the PostScript translation
 * library (the C port of exportConstant.def, exportMacro.def and the
 * TGSLocals frame that the asm routines inherited).
 *
 * The asm shared TGSLocals through ".enter inherit"; here it is a struct
 * the routines get a pointer to.  Its argument buffer TGS_buffer is
 * filled through tgs->p, which plays the part of es:di.
 */
#ifndef EXPORT_INT_H
#define EXPORT_INT_H

#include <geos.h>
#include <graphics.h>
#include <gstring.h>
#include <heap.h>
#include <lmem.h>
#include <streamC.h>
#include <Internal/streamDr.h>
#include <xlatPS.h>
#include <string.h>               /* memcpy, memset */


#define EPS_BUFFER_SIZE         300     /* TGS_buffer */

/* opcode ranges of gstring.def */
#define GSE_FIRST_OUTPUT_OPCODE GR_DRAW_LINE
#define GSE_LAST_OUTPUT_IN_PATH GR_DRAW_TEXT_PTR
#define GSE_LAST_OUTPUT_OPCODE  0x5f    /* GR_OUTPUT_5F */
#define GSE_FIRST_TEXT_OPCODE   GR_DRAW_CHAR
#define GSE_FIRST_PATH_OPCODE   GR_BEGIN_PATH
#define GSE_LAST_PATH_OPCODE    0xaf    /* GR_PATH_AF */
#define GR_DRAW_TEXT_OPTR       64

/* ThreePointArcParams with its points as WWFixedAsDWord */
typedef struct {
    word           close;
    WWFixedAsDWord x1, y1, x2, y2, x3, y3;
} EPSArc3;

/* ConvertArcParams */
typedef struct {
    WWFixedAsDWord p1x, p1y, p2x, p2y;  /* midpoints of the chords */
    WWFixedAsDWord m1, m2;              /* their inverse slopes */
    WWFixedAsDWord tlx, tly, brx, bry;  /* bounds of the circle */
    WWFixedAsDWord ang1, ang2;
    WWFixedAsDWord radius;
    WWFixedAsDWord cx, cy;              /* center */
} EPSNewArc;

typedef struct TGSLocals {
    GStateHandle    TGS_gstate;         /* attribute gstate */
    MemHandle       TGS_stream;         /* EPSExportLowStreamStruct */
    GStateHandle    TGS_gstring;        /* the source */
    GStateHandle    TGS_src;            /* where elements come from now:
                                           the source or a path */
    PSExportBlock  *TGS_options;
    GSControl       TGS_flags;
    GSRetType       TGS_emitRetType;
    byte            TGS_xfactor, TGS_yfactor;
    TransMatrix     TGS_matrix;
    TransMatrix     TGS_pathmatrix;
    MemHandle       TGS_elemBlock;      /* the element being emitted */
    word            TGS_elemSize;
    byte           *TGS_elem;
    EPSArc3         TGS_arc;
    EPSNewArc       TGS_newArc;
    byte            TGS_bmType;         /* zero width line with a mask */
    byte            TGS_pathType;
    byte            TGS_pathCnt;
    Boolean         TGS_pathgs;         /* building a path */
    GStateHandle    TGS_pathGState;
    word            TGS_writeErr;
    word            TGS_elemLen;        /* the element's size */
    MemHandle       TGS_pageFonts;      /* PageFont[1 + TGS_pfCount] */
    word            TGS_pfCount;
    TextStyle       TGS_newstyle;       /* styles the font leaves to us */
    WWFixedAsDWord  TGS_locmot;         /* last run's local motion */
    WWFixedAsDWord  TGS_runlen;         /* width of the current run */
    WWFixedAsDWord  TGS_width;          /* of the text field */
    WWFixedAsDWord  TGS_gwidth;         /* of the style group */
    WWFixedAsDWord  TGS_gpos;           /* x of the style group */
    char           *p;                  /* into TGS_buffer */
    char            TGS_buffer[EPS_BUFFER_SIZE];
} TGSLocals;

/* the element bytes: DOS16 layout, little endian, packed */
#define EL_W(e, off)    ((sword) ((e)[off] | ((e)[(off) + 1] << 8)))
#define EL_D(e, off)    ((WWFixedAsDWord) ((dword) (e)[off] |           \
                         ((dword) (e)[(off) + 1] << 8) |                \
                         ((dword) (e)[(off) + 2] << 16) |               \
                         ((dword) (e)[(off) + 3] << 24)))

/* the PostScript text (generated from exportPSCode.asm, exportComments.asm
 * and exportProlog.asm by Port32/tools/asmchars2c.py) */
#define PS(name)        name, (word) (sizeof(name) - 1)
#include "epsStrings.h"

/* an AdobeFontEntry (exportConstant.def), the tables of epsFonts.c
 * (Port32/tools/epsfonts2c.py) */
typedef struct {
    word        AFE_id;                 /* FontID */
    byte        AFE_style;              /* TextStyle */
    byte        AFE_encode;             /* AdobeFontEncoding */
    byte        AFE_nlen;
    const char *AFE_name;
} EPSAdobeFont;
#define AFE_STANDARD            0       /* AdobeFontEncoding */
#define AFE_EXPERT              1
#define AFE_SYMBOL              2
#define AFE_SPECIAL             3
#define EPS_NUM_FONT_MAPS       19
extern const EPSAdobeFont *const *const *const epsFontMaps[EPS_NUM_FONT_MAPS];
extern const byte epsFaces[];
extern const word epsFaceStart[4];

/* exportUtils.c */
Boolean SendToStream(MemHandle stream, const void *data, word size,
                     word *error);
Boolean EmitPSCode(TGSLocals *tgs, const char *s, word len);
Boolean EmitBuffer(TGSLocals *tgs);
void    Put(TGSLocals *tgs, const char *s, word len);
void    PutChar(TGSLocals *tgs, char c);
void    ResetBuffer(TGSLocals *tgs);
word    EpsStrLen(const char *s);      /* geodes have no libc strlen */
char   *WWFixedToAscii(char *p, WWFixedAsDWord v);
char   *DWFixedToAscii(char *p, const DWFixed *v);
char   *SWordToAscii(char *p, sword v);
char   *SDWordToAscii(char *p, sdword v);
char   *UWordToAscii(char *p, word v);
void    EmitStartObject(TGSLocals *tgs, word op);
void    EmitEndObject(TGSLocals *tgs);
void    EmitTransform(TGSLocals *tgs);
void    EmitLineAttributes(TGSLocals *tgs, Boolean multi);
void    EmitAreaAttributes(TGSLocals *tgs);
void    EmitColor(TGSLocals *tgs, RGBColorAsDWord c);
void    MapRGBForPrinter(byte *r, byte *g, byte *b);
void    MatrixToAscii(TGSLocals *tgs, const TransMatrix *m);
byte   *ExtractElement(TGSLocals *tgs);

/* exportGString.c */
GSRetType TranslateGString(TGSLocals *tgs, GStateHandle gstring,
                           MemHandle stream, PSExportBlock *opts,
                           GSControl flags);
void    CoordToAscii(TGSLocals *tgs, sword x, sword y);
void    XCoordPlusSpace(TGSLocals *tgs, sword x);
void    YCoordPlusSpace(TGSLocals *tgs, sword y);
void    WWFCoordToAscii(TGSLocals *tgs, WWFixedAsDWord x, WWFixedAsDWord y);
void    EmitPolyCoords(TGSLocals *tgs, const byte *pts, word count,
                       GStateHandle gs);
void    OrderRoundRectCoords(const byte *e, sword *x1, sword *y1,
                             sword *x2, sword *y2, sword *radius);
Boolean CheckFullLineMask(TGSLocals *tgs);
Boolean CheckFullAreaMask(TGSLocals *tgs);
void    EmitNothing(TGSLocals *tgs);

/* exportPath.c */
void    EmitClipPath(TGSLocals *tgs, word op);
Boolean EmitPath(TGSLocals *tgs, GetPathType type);

/* exportText.c */
void    InitPageFonts(TGSLocals *tgs);
void    FreePageFonts(TGSLocals *tgs);
void    EmitText(TGSLocals *tgs, word op);
void    EmitTextField(TGSLocals *tgs, word op);

/* exportBitmap.c */
void    EmitBitmap(TGSLocals *tgs, word op);

/* exportArc.c */
void    Convert3PointToNormalArc(TGSLocals *tgs, const byte *e,
                                 GStateHandle gs);

/* exportHeader.c */
Boolean EmitHeaderComments(PSExportBlock *opts, MemHandle stream);
Boolean EmitDocSetup(PSExportBlock *opts, MemHandle stream);
Boolean EmitPageSetup(PSExportBlock *opts, MemHandle stream);
Boolean EmitEndPageSetup(MemHandle stream);

#endif /* EXPORT_INT_H */
