/*
 * graphicsState.c -- graphics states (after graphicsState.asm:
 * GrCreateState, GrDestroyState, GrSaveState, GrRestoreState and the
 * attribute routines; graphicsUtils.asm: GrCopyDrawMask, GrShiftMask).
 *
 * A GState is an LMem block (LMEM_TYPE_GSTATE) with the GState
 * structure as its header.  GrSaveState keeps a copy of the whole block
 * in a block of its own, linked by GS_saveStateLink; GrRestoreState
 * copies it back.
 *
 * Differences from the asm:
 *   - no cache of free GStates: the heap is the host's;
 *   - no graphics strings and paths yet: the attributes are only set
 *     in the GState;
 *   - the font: GrInvalidateFont (graphicsChars.c) where the font
 *     changes; GrSetFont and the kerning there too.
 * Not yet: the pen position and the transformations (with
 * graphicsTransform), the clip routines, GrSetVMFile.
 */
#include "graphicsInt.h"

/* the CommonAttr of GState <> (gstate.def) */
static void DefaultAttr(CommonAttr *ca, ColorMapMode map)
{
    ca->CA_mapMode = map;
    ca->CA_colorIndex = C_BLACK;
    ca->CA_colorRGB.RGB_red = ca->CA_colorRGB.RGB_green =
	ca->CA_colorRGB.RGB_blue = 0;
    ca->CA_maskType = SDM_100;
    memset(ca->CA_mask, 0xff, sizeof(DrawMask));
    ca->CA_flags = AO_MASK_1 | AO_MASK_1_COPY;
}

/* what GrCreateState copies: GState <> with the defaults of GrInitSys */
static void DefaultState(GState *gs)
{
    memset((byte *) gs + sizeof(LMemBlockHeader), 0,
	   sizeof(GState) - sizeof(LMemBlockHeader));
    gs->GS_TMatrix.TM_11.WWF_int = 1;
    gs->GS_TMatrix.TM_22.WWF_int = 1;
    DefaultAttr(&gs->GS_lineAttr, CMT_CLOSEST);
    DefaultAttr(&gs->GS_areaAttr, CMT_DITHER);
    DefaultAttr(&gs->GS_textAttr, CMT_CLOSEST);
    gs->GS_lineStyle = LS_SOLID;
    gs->GS_lineJoin = LJ_MITERED;
    gs->GS_lineEnd = LE_BUTTCAP;
    gs->GS_lineWidth.WWF_int = 1;
    gs->GS_inverseMiterLimit = 0x199a;	/* a miter limit of 10 */
    gs->GS_mixMode = MM_COPY;
    gs->GS_pixelHeightM1 = 15;
    gs->GS_fontFirstChar = 32;
    gs->GS_fontLastChar = 252;
    GrDefaultFontAttrs(&gs->GS_fontAttr);
}

GStateHandle GrCreateState(WindowHandle win)
{
    MemHandle mh = MemAllocLMem(LMEM_TYPE_GSTATE, sizeof(GState));
    GState *gs = MemLock(mh);

    DefaultState(gs);
    gs->GS_window = win;
    MemUnlock(mh);
    return mh;
}

void GrWinStateFlags(GStateHandle gstate, const GState *gs, WinGrFlags keep,
		     WinGrRegFlags keepReg)
{
    Window *w;

    if (!gs->GS_window || gs->GS_gstring)
	return;
    w = MemLock(gs->GS_window);
    if (w && w->W_curState == gstate) {
	w->W_grFlags &= keep;
	w->W_grRegFlags &= keepReg;
    }
    if (w)
	MemUnlock(gs->GS_window);
}

void GrDestroyState(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    Window *w;
    Handle next;

    GrInvalidateFont(gs);
    if (gs->GS_window && (w = MemLock(gs->GS_window)) != NULL) {
	if (w->W_curState == gstate) {
	    w->W_curState = 0;
	    w->W_grFlags = 0;
	}
	MemUnlock(gs->GS_window);
    }
    while (gstate) {			/* with the saved states */
	gs = MemLock(gstate);
	next = gs->GS_saveStateLink;
	MemFree(gstate);
	gstate = next;
    }
}

void GrSaveState(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    word size = (word) MemGetInfo(gstate, MGIT_SIZE);
    MemHandle copy = MemAlloc(size, HF_SWAPABLE, HAF_NO_ERR | HAF_LOCK);
    GState *saved = MemDeref(copy);

    (void) GrGSRecord(gstate, GR_SAVE_STATE, "");
    memcpy(saved, gs, size);
    saved->GS_fontHandle = 0;
    gs->GS_saveStateLink = copy;
    MemUnlock(copy);
    MemUnlock(gstate);
}

void GrRestoreState(GStateHandle gstate)
{
    GState *gs = MemLock(gstate), *saved;
    Handle link = gs->GS_saveStateLink;
    word size;

    (void) GrGSRecord(gstate, GR_RESTORE_STATE, "");
    if (!link) {
	MemUnlock(gstate);
	return;
    }
    GrInvalidateFont(gs);
    saved = MemLock(link);
    size = (word) MemGetInfo(link, MGIT_SIZE);
    if (size != MemGetInfo(gstate, MGIT_SIZE)) {
	MemReAlloc(gstate, size, HAF_NO_ERR);
	gs = MemDeref(gstate);
    }
    memcpy(gs, saved, size);
    gs->GS_header.LMBH_handle = gstate;
    MemFree(link);
    GrWinStateFlags(gstate, gs,
		    (WinGrFlags) ~(WGF_MASK_VALID | WGF_XFORM_VALID),
		    (WinGrRegFlags) ~(WGRF_PATH_VALID | WGRF_WIN_PATH_VALID));
    MemUnlock(gstate);
}

void GrSetPrivateData(GStateHandle gstate, word dataAX, word dataBX,
		      word dataCX, word dataDX)
{
    GState *gs = MemLock(gstate);

    gs->GS_privData.GPD_ax = dataAX;
    gs->GS_privData.GPD_bx = dataBX;
    gs->GS_privData.GPD_cx = dataCX;
    gs->GS_privData.GPD_dx = dataDX;
    MemUnlock(gstate);
}

/* CalcRectOpts */
static void RectOpts(CommonAttr *ca, MixMode mode)
{
    AttrOpts f = (AttrOpts) (ca->CA_flags &
			     ~(AO_MASK_1_COPY | AO_MASK_1_INVERT));

    if (f & AO_MASK_1) {
	if (mode == MM_COPY)
	    f |= AO_MASK_1_COPY;
	else if (mode == MM_INVERT)
	    f |= AO_MASK_1_INVERT;
    }
    ca->CA_flags = f;
}

void GrSetMixMode(GStateHandle gstate, MixMode mode)
{
    GState *gs = MemLock(gstate);

    (void) GrGSRecord(gstate, GR_SET_MIX_MODE, "b", mode);
    gs->GS_mixMode = mode;
    RectOpts(&gs->GS_lineAttr, mode);
    RectOpts(&gs->GS_areaAttr, mode);
    RectOpts(&gs->GS_textAttr, mode);
    MemUnlock(gstate);
}

/* UpdateOptsAndReturn (the kerning values come with the fonts) */
void GrUpdateOpts(GState *gs)
{
    TextMode tm = (TextMode) (gs->GS_textMode & ~TM_PAD_SPACES);

    gs->GS_optDrawMask = (byte) ((gs->GS_textAttr.CA_flags & AO_MASK_1) ?
				 0 : 0xff);
    if (gs->GS_textSpacePad.WBF_int || gs->GS_textSpacePad.WBF_frac) {
	tm |= TM_PAD_SPACES;
	gs->GS_optSpacePad = GO_SPECIAL_CASE;
    } else
	gs->GS_optSpacePad = GO_FALL_THRU;
    gs->GS_textMode = tm;
    gs->GS_hyphenOpcode = (GOpt) ((tm & TM_DRAW_OPTIONAL_HYPHENS) ?
				  GO_SPECIAL_CASE : GO_FALL_THRU);
    gs->GS_drawCtrlOpcode = (GOpt) ((tm & TM_DRAW_CONTROL_CHARS) ?
				    GO_SPECIAL_CASE : GO_FALL_THRU);
    gs->GS_kernOp = GO_FALL_THRU;
    gs->GS_trackKernValue.BBF_int = 0;
    gs->GS_trackKernValue.BBF_frac = 0;
    gs->GS_drawOffsetOpcode = (GOpt) (gs->GS_textDrawOffset ?
				      GO_SPECIAL_CASE : GO_FALL_THRU);
}

/* colors: CF_GRAY as RGB, CF_INDEX as the _INDEX element */
static void RecordColor(GStateHandle gstate, byte op, byte opIndex,
			ColorFlag flag, word redOrIndex, word green, word blue)
{
    if (flag == CF_INDEX)
	(void) GrGSRecord(gstate, opIndex, "b", redOrIndex);
    else if (flag == CF_GRAY)
	(void) GrGSRecord(gstate, op, "bbb", redOrIndex, redOrIndex,
			  redOrIndex);
    else
	(void) GrGSRecord(gstate, op, "bbb", redOrIndex, green, blue);
}

/* colors */
static void SetColor(GStateHandle gstate, word attr, ColorFlag flag,
		     word redOrIndex, word green, word blue)
{
    GState *gs = MemLock(gstate);

    GrSetColorIn(gs, (CommonAttr *) ((byte *) gs + attr), flag,
		 (byte) redOrIndex, (byte) green, (byte) blue);
    MemUnlock(gstate);
}

void GrSetLineColor(GStateHandle gstate, ColorFlag flag, word redOrIndex,
		    word green, word blue)
{
    RecordColor(gstate, GR_SET_LINE_COLOR, GR_SET_LINE_COLOR_INDEX,
		flag, redOrIndex, green, blue);
    SetColor(gstate, offsetof(GState, GS_lineAttr), flag, redOrIndex,
	     green, blue);
}

void GrSetAreaColor(GStateHandle gstate, ColorFlag flag, word redOrIndex,
		    word green, word blue)
{
    RecordColor(gstate, GR_SET_AREA_COLOR, GR_SET_AREA_COLOR_INDEX,
		flag, redOrIndex, green, blue);
    SetColor(gstate, offsetof(GState, GS_areaAttr), flag, redOrIndex,
	     green, blue);
}

void GrSetTextColor(GStateHandle gstate, ColorFlag flag, word redOrIndex,
		    word green, word blue)
{
    RecordColor(gstate, GR_SET_TEXT_COLOR, GR_SET_TEXT_COLOR_INDEX,
		flag, redOrIndex, green, blue);
    SetColor(gstate, offsetof(GState, GS_textAttr), flag, redOrIndex,
	     green, blue);
}

static void SetColorMap(GStateHandle gstate, word attr, word colorMap)
{
    GState *gs = MemLock(gstate);

    ((CommonAttr *) ((byte *) gs + attr))->CA_mapMode = (ColorMapMode) colorMap;
    MemUnlock(gstate);
}

void GrSetLineColorMap(GStateHandle gstate, word colorMap)
{
    (void) GrGSRecord(gstate, GR_SET_LINE_COLOR_MAP, "b", colorMap);
    SetColorMap(gstate, offsetof(GState, GS_lineAttr), colorMap);
}

void GrSetAreaColorMap(GStateHandle gstate, word colorMap)
{
    (void) GrGSRecord(gstate, GR_SET_AREA_COLOR_MAP, "b", colorMap);
    SetColorMap(gstate, offsetof(GState, GS_areaAttr), colorMap);
}

void GrSetTextColorMap(GStateHandle gstate, word colorMap)
{
    (void) GrGSRecord(gstate, GR_SET_TEXT_COLOR_MAP, "b", colorMap);
    SetColorMap(gstate, offsetof(GState, GS_textAttr), colorMap);
}

/* draw masks */

/*
 * GrShiftMask: rotate a mask from the GState's shift to a new one, so
 * that it lines up with the screen: pixel x, y of the result is pixel
 * x - dx, y - dy of the mask.
 */
void GrShiftMask(GState *gs, DrawMask m, byte xShift, byte yShift)
{
    DrawMask old;
    int dx = (xShift - gs->GS_xShift) & 7, dy = (yShift - gs->GS_yShift) & 7;
    int y;

    memcpy(old, m, sizeof(DrawMask));
    for (y = 0; y < 8; y++) {
	byte b = old[(y - dy) & 7];
	m[y] = (byte) (dx ? (b >> dx) | (b << (8 - dx)) : b);
    }
}

/* SetMask, GrCopyDrawMask */
void GrSetMaskIn(GState *gs, CommonAttr *ca, SysDrawMask sdm,
		 const DrawMask *custom)
{
    AttrOpts f = (AttrOpts) (ca->CA_flags &
			     ~(AO_MASK_1 | AO_MASK_1_COPY | AO_MASK_1_INVERT));
    byte n = (byte) (sdm & ~SDM_INVERSE);
    int i;

    if (sdm == SDM_100 || sdm == (SDM_0 | SDM_INVERSE)) {
	f |= AO_MASK_1;
	if (gs->GS_mixMode == MM_COPY)
	    f |= AO_MASK_1_COPY;
	else if (gs->GS_mixMode == MM_INVERT)
	    f |= AO_MASK_1_INVERT;
    }
    ca->CA_flags = f;
    ca->CA_maskType = sdm;
    if (n == SDM_CUSTOM) {
	if (custom)
	    memcpy(ca->CA_mask, *custom, sizeof(DrawMask));
    } else if (n < GR_NUM_SYS_DRAW_MASKS)
	memcpy(ca->CA_mask, grSysDrawMasks[n], sizeof(DrawMask));
    if (sdm & SDM_INVERSE)
	for (i = 0; i < 8; i++)
	    ca->CA_mask[i] = (byte) ~ca->CA_mask[i];
    /* the system masks are for shift 0: line them up */
    {
	byte xs = gs->GS_xShift, ys = gs->GS_yShift;
	gs->GS_xShift = gs->GS_yShift = 0;
	GrShiftMask(gs, ca->CA_mask, xs, ys);
	gs->GS_xShift = xs;
	gs->GS_yShift = ys;
    }
}

static void SetMask(GStateHandle gstate, word attr, SysDrawMask sdm,
		    const DrawMask *custom)
{
    GState *gs = MemLock(gstate);

    GrSetMaskIn(gs, (CommonAttr *) ((byte *) gs + attr), sdm, custom);
    if (attr == offsetof(GState, GS_textAttr))
	GrUpdateOpts(gs);
    MemUnlock(gstate);
}

void GrSetLineMaskSys(GStateHandle gstate, word sysDM)
{
    (void) GrGSRecord(gstate, GR_SET_LINE_MASK, "b", sysDM);
    SetMask(gstate, offsetof(GState, GS_lineAttr), (SysDrawMask) sysDM, NULL);
}

void GrSetLineMaskCustom(GStateHandle gstate, const DrawMask *dm)
{
    (void) GrGSRecord(gstate, GR_SET_CUSTOM_LINE_MASK, "r", dm, 8);
    SetMask(gstate, offsetof(GState, GS_lineAttr), SDM_CUSTOM, dm);
}

void GrSetAreaMaskSys(GStateHandle gstate, word sysDM)
{
    (void) GrGSRecord(gstate, GR_SET_AREA_MASK, "b", sysDM);
    SetMask(gstate, offsetof(GState, GS_areaAttr), (SysDrawMask) sysDM, NULL);
}

void GrSetAreaMaskCustom(GStateHandle gstate, const DrawMask *dm)
{
    (void) GrGSRecord(gstate, GR_SET_CUSTOM_AREA_MASK, "r", dm, 8);
    SetMask(gstate, offsetof(GState, GS_areaAttr), SDM_CUSTOM, dm);
}

void GrSetTextMaskSys(GStateHandle gstate, SysDrawMask sysDM)
{
    (void) GrGSRecord(gstate, GR_SET_TEXT_MASK, "b", sysDM);
    SetMask(gstate, offsetof(GState, GS_textAttr), sysDM, NULL);
}

void GrSetTextMaskCustom(GStateHandle gstate, const DrawMask *dm)
{
    (void) GrGSRecord(gstate, GR_SET_CUSTOM_TEXT_MASK, "r", dm, 8);
    SetMask(gstate, offsetof(GState, GS_textAttr), SDM_CUSTOM, dm);
}

/* lines */
void GrSetLineWidth(GStateHandle gstate, WWFixedAsDWord width)
{
    GState *gs = MemLock(gstate);

    (void) GrGSRecord(gstate, GR_SET_LINE_WIDTH, "d", width);
    gs->GS_lineWidth.WWF_frac = (word) width;
    gs->GS_lineWidth.WWF_int = (sword) (width >> 16);
    MemUnlock(gstate);
}

void GrSetLineEnd(GStateHandle gstate, LineEnd end)
{
    GState *gs = MemLock(gstate);

    (void) GrGSRecord(gstate, GR_SET_LINE_END, "b", end);
    gs->GS_lineEnd = end;
    MemUnlock(gstate);
}

void GrSetLineJoin(GStateHandle gstate, LineJoin join)
{
    GState *gs = MemLock(gstate);

    (void) GrGSRecord(gstate, GR_SET_LINE_JOIN, "b", join);
    gs->GS_lineJoin = join;
    MemUnlock(gstate);
}

void GrSetMiterLimit(GStateHandle gstate, WWFixedAsDWord limit)
{
    GState *gs = MemLock(gstate);
    word inv = (word) GrReciprocal32(limit);

    (void) GrGSRecord(gstate, GR_SET_MITER_LIMIT, "d", limit);
    gs->GS_inverseMiterLimit = inv ? inv : 0xffff;
    MemUnlock(gstate);
}

/* SetLineStyle: pairs is the custom array, a byte on and off each */
static void SetLineStyle(GState *gs, LineStyle style, byte skip,
			 const byte *pairs, byte n)
{
    gs->GS_lineStyle = style;
    if (style == LS_SOLID)
	return;
    if (style != LS_CUSTOM) {
	if (style < LS_DASHED || style > LS_DASHDDOT)
	    return;
	pairs = grSysDashArrays[style - LS_DASHED] + 1;
	n = grSysDashArrays[style - LS_DASHED][0];
    }
    if (n > MAX_DASH_ARRAY_PAIRS)
	n = MAX_DASH_ARRAY_PAIRS;
    gs->GS_numOfDashPairs = n;
    gs->GS_dashSkipDistance = skip;
    if (pairs)
	memcpy(gs->GS_dashPairArray, pairs, 2 * n);
}

void GrSetLineStyle(GStateHandle gstate, LineStyle style, word skipDistance,
		    const DashPairArray *dpa, word numPairs)
{
    GState *gs = MemLock(gstate);

    if (style == LS_CUSTOM)
	(void) GrGSRecord(gstate, GR_SET_CUSTOM_LINE_STYLE, "wwr", skipDistance,
			  numPairs, dpa, 2 * numPairs);
    else
	(void) GrGSRecord(gstate, GR_SET_LINE_STYLE, "bb", style,
			  skipDistance);
    SetLineStyle(gs, style, (byte) skipDistance, (const byte *) dpa,
		 (byte) numPairs);
    MemUnlock(gstate);
}

/* SetCommonAttr: the first part of LineAttr and AreaAttr */
static void SetCommonAttr(GState *gs, CommonAttr *ca, const AreaAttr *aa)
{
    ca->CA_mapMode = aa->AA_mapMode;
    GrSetColorIn(gs, ca, aa->AA_colorFlag, aa->AA_color.RGB_red,
		 aa->AA_color.RGB_green, aa->AA_color.RGB_blue);
    GrSetMaskIn(gs, ca, aa->AA_mask, NULL);
}

void GrSetLineAttr(GStateHandle gstate, const LineAttr *la)
{
    GState *gs = MemLock(gstate);

    (void) GrGSRecord(gstate, GR_SET_LINE_ATTR, "bbbbbbbbbd",
		      la->LA_colorFlag, la->LA_color.RGB_red,
		      la->LA_color.RGB_green, la->LA_color.RGB_blue,
		      la->LA_mask, la->LA_mapMode, la->LA_end, la->LA_join,
		      la->LA_style, GR_WWF_D(la->LA_width));
    SetCommonAttr(gs, &gs->GS_lineAttr, (const AreaAttr *) la);
    gs->GS_lineEnd = la->LA_end;
    gs->GS_lineJoin = la->LA_join;
    SetLineStyle(gs, la->LA_style, 0, NULL, 0);
    gs->GS_lineWidth = la->LA_width;
    MemUnlock(gstate);
}

void GrSetAreaAttr(GStateHandle gstate, const AreaAttr *aa)
{
    GState *gs = MemLock(gstate);

    (void) GrGSRecord(gstate, GR_SET_AREA_ATTR, "bbbbbb",
		      aa->AA_colorFlag, aa->AA_color.RGB_red,
		      aa->AA_color.RGB_green, aa->AA_color.RGB_blue,
		      aa->AA_mask, aa->AA_mapMode);
    SetCommonAttr(gs, &gs->GS_areaAttr, aa);
    MemUnlock(gstate);
}

/* text */
static void SetStyle(GState *gs, TextStyle set, TextStyle reset)
{
    TextStyle now = (TextStyle) ((gs->GS_fontAttr.FCA_textStyle & ~reset) |
				 set);

    if ((now ^ gs->GS_fontAttr.FCA_textStyle) & ~GR_KERNEL_STYLES)
	GrInvalidateFont(gs);
    gs->GS_fontAttr.FCA_textStyle = now;
}

void GrSetTextStyle(GStateHandle gstate, TextStyle bitsToSet,
		    TextStyle bitsToClear)
{
    GState *gs = MemLock(gstate);

    (void) GrGSRecord(gstate, GR_SET_TEXT_STYLE, "bb", bitsToSet,
		      bitsToClear);
    SetStyle(gs, bitsToSet, bitsToClear);
    MemUnlock(gstate);
}

void GrSetTextMode(GStateHandle gstate, TextMode bitsToSet,
		   TextMode bitsToClear)
{
    GState *gs = MemLock(gstate);

    (void) GrGSRecord(gstate, GR_SET_TEXT_MODE, "bb", bitsToSet,
		      bitsToClear);
    bitsToSet &= (TextMode) ~GR_TM_INTERNAL;
    bitsToClear &= (TextMode) ~GR_TM_INTERNAL;
    gs->GS_textMode = (TextMode) ((gs->GS_textMode & ~bitsToClear) |
				  bitsToSet);
    GrUpdateOpts(gs);
    MemUnlock(gstate);
}

void GrSetTextDrawOffset(GStateHandle gstate, word numToDraw)
{
    GState *gs = MemLock(gstate);

    gs->GS_textDrawOffset = numToDraw;
    GrUpdateOpts(gs);
    MemUnlock(gstate);
}

/* padding: WWFixed, kept to 1/256 */
void GrSetTextSpacePad(GStateHandle gstate, WWFixedAsDWord padding)
{
    GState *gs = MemLock(gstate);

    (void) GrGSRecord(gstate, GR_SET_TEXT_SPACE_PAD, "f", padding);
    gs->GS_textSpacePad.WBF_int = (sword) (padding >> 16);
    gs->GS_textSpacePad.WBF_frac = (byte) (padding >> 8);
    GrUpdateOpts(gs);
    MemUnlock(gstate);
}

/* SetTextAttrLow */
void GrSetTextAttr(GStateHandle gstate, const TextAttr *ta)
{
    GState *gs = MemLock(gstate);
    TextMode set, clear;

    (void) GrGSRecord(gstate, GR_SET_TEXT_ATTR, "bbbbbbbbbbbfwfwbbb",
		      ta->TA_color.CQ_redOrIndex, ta->TA_color.CQ_info,
		      ta->TA_color.CQ_green, ta->TA_color.CQ_blue,
		      ta->TA_mask, ta->TA_pattern.HP_type,
		      ta->TA_pattern.HP_data, ta->TA_styleSet,
		      ta->TA_styleClear, ta->TA_modeSet, ta->TA_modeClear,
		      GR_WBF_D(ta->TA_spacePad), ta->TA_font,
		      GR_WBF_D(ta->TA_size), ta->TA_trackKern,
		      ta->TA_fontWeight, ta->TA_fontWidth, 0);
    GrSetColorIn(gs, &gs->GS_textAttr, ta->TA_color.CQ_info,
		 ta->TA_color.CQ_redOrIndex, ta->TA_color.CQ_green,
		 ta->TA_color.CQ_blue);
    GrSetMaskIn(gs, &gs->GS_textAttr, ta->TA_mask, NULL);
    SetStyle(gs, ta->TA_styleSet, ta->TA_styleClear);
    set = (TextMode) (ta->TA_modeSet & ~GR_TM_INTERNAL);
    clear = (TextMode) (ta->TA_modeClear & ~GR_TM_INTERNAL);
    gs->GS_textMode = (TextMode) ((gs->GS_textMode & ~clear) | set);
    gs->GS_textSpacePad = ta->TA_spacePad;
    if (gs->GS_fontAttr.FCA_fontID != ta->TA_font ||
	gs->GS_fontAttr.FCA_pointsize.WBF_int != ta->TA_size.WBF_int ||
	gs->GS_fontAttr.FCA_pointsize.WBF_frac != ta->TA_size.WBF_frac ||
	gs->GS_fontAttr.FCA_weight != ta->TA_fontWeight ||
	gs->GS_fontAttr.FCA_width != ta->TA_fontWidth) {
	GrInvalidateFont(gs);
	gs->GS_fontAttr.FCA_fontID = ta->TA_font;
	gs->GS_fontAttr.FCA_pointsize = ta->TA_size;
	gs->GS_fontAttr.FCA_weight = ta->TA_fontWeight;
	gs->GS_fontAttr.FCA_width = ta->TA_fontWidth;
    }
    gs->GS_trackKernDegree = ta->TA_trackKern;
    gs->GS_textPattern.GCP_pattern = ta->TA_pattern.HP_type;
    gs->GS_textPattern.GCP_system = ta->TA_pattern.HP_data;
    GrUpdateOpts(gs);
    MemUnlock(gstate);
}
