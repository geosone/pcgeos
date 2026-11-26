/*
 * graphicsTransform.c -- the transformation of a GState (after
 * graphicsTransform.asm; graphicsTransformSimple.asm: GSetNullTransform;
 * the routines of graphicsTransformUtils.asm are in
 * graphicsTransformUtils.c).
 *
 * GS_TMatrix goes from document to page coordinates, the window's
 * W_TMatrix from page to window coordinates; their product with the
 * window's position is W_curTMatrix (GrComposeMatrix), kept for the
 * GState in W_curState while WGF_XFORM_VALID.  A change here clears
 * that flag (and WGRF_PATH_VALID) when the window holds this GState.
 * Every change but a translation drops the font (GrInvalidateFont).
 */
#include "graphicsInt.h"

/* GrExitTrans, ExitTransform */
void GrExitTrans(GStateHandle gstate, GState *gs)
{
    GrWinStateFlags(gstate, gs, (WinGrFlags) ~WGF_XFORM_VALID,
		    (WinGrRegFlags) ~WGRF_PATH_VALID);
    MemUnlock(gstate);
}

void GrApplyTranslation(GStateHandle gstate, WWFixedAsDWord xTrans,
			WWFixedAsDWord yTrans)
{
    GState *gs = MemLock(gstate);

    (void) GrGSRecord(gstate, GR_APPLY_TRANSLATION, "dd", xTrans, yTrans);
    GrMatTranslate(&gs->GS_TMatrix, (sdword) xTrans, (sdword) yTrans);
    GrExitTrans(gstate, gs);
}

void GrApplyTranslationDWord(GStateHandle gstate, sdword xTrans,
			     sdword yTrans)
{
    GState *gs = MemLock(gstate);

    (void) GrGSRecord(gstate, GR_APPLY_TRANSLATION_DWORD, "dd",
		      (dword) xTrans, (dword) yTrans);
    GrMatTranslate(&gs->GS_TMatrix, (int64_t) xTrans * 65536,
		   (int64_t) yTrans * 65536);
    GrExitTrans(gstate, gs);
}

static TMatrix *Chunk(GStateHandle gstate, ChunkHandle ch)
{
    return LMemDeref(ConstructOptr(gstate, ch));
}

void GrInitDefaultTransform(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);

    (void) GrGSRecord(gstate, GR_INIT_DEFAULT_TRANSFORM, "");
    if (!gs->GS_defTMatrix) {
	ChunkHandle ch = LMemAlloc(gstate, sizeof(TMatrix));
	gs = MemDeref(gstate);
	gs->GS_defTMatrix = ch;
    }
    *Chunk(gstate, gs->GS_defTMatrix) = gs->GS_TMatrix;
    MemUnlock(gstate);
}

void GrSetDefaultTransform(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);

    (void) GrGSRecord(gstate, GR_SET_DEFAULT_TRANSFORM, "");
    GrInvalidateFont(gs);
    if (gs->GS_defTMatrix)
	gs->GS_TMatrix = *Chunk(gstate, gs->GS_defTMatrix);
    else
	GrMatIdentity(&gs->GS_TMatrix);
    GrExitTrans(gstate, gs);
}

/*
 * GrSaveTransform: a stack of chunks (SavedTMatrix): the next one, the
 * matrix, the default matrix's chunk and, if there is one, its matrix.
 */
typedef struct {
    ChunkHandle	STM_next;
    TMatrix	STM_matrix;
    ChunkHandle	STM_default;
    TMatrix	STM_defMatrix;		/* when STM_default */
} SavedTMatrix;

void GrSaveTransform(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    ChunkHandle ch = LMemAlloc(gstate, sizeof(SavedTMatrix));
    SavedTMatrix *s;

    (void) GrGSRecord(gstate, GR_SAVE_TRANSFORM, "");
    gs = MemDeref(gstate);
    s = LMemDeref(ConstructOptr(gstate, ch));
    s->STM_next = gs->GS_savedMatrix;
    s->STM_matrix = gs->GS_TMatrix;
    s->STM_default = gs->GS_defTMatrix;
    if (gs->GS_defTMatrix)
	s->STM_defMatrix = *Chunk(gstate, gs->GS_defTMatrix);
    gs->GS_savedMatrix = ch;
    MemUnlock(gstate);
}

void GrRestoreTransform(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    ChunkHandle ch = gs->GS_savedMatrix;
    SavedTMatrix *s;

    (void) GrGSRecord(gstate, GR_RESTORE_TRANSFORM, "");
    if (!ch) {
	MemUnlock(gstate);
	return;
    }
    s = LMemDeref(ConstructOptr(gstate, ch));
    GrInvalidateFont(gs);
    gs->GS_savedMatrix = s->STM_next;
    gs->GS_TMatrix = s->STM_matrix;
    gs->GS_defTMatrix = s->STM_default;
    if (s->STM_default)
	*Chunk(gstate, s->STM_default) = s->STM_defMatrix;
    LMemFree(ConstructOptr(gstate, ch));
    gs = MemDeref(gstate);
    GrExitTrans(gstate, gs);
}

/*
 * GrComposeMatrix: W_curTMatrix = GS_TMatrix * W_TMatrix, then moved
 * by the window's position; W_curState is the caller's.
 */
void GrComposeMatrix(GStateHandle gstate, GState *gs, Window *w)
{
    (void) gstate;
    if (!gs->GS_TMatrix.TM_flags)
	w->W_curTMatrix = w->W_TMatrix;
    else if (!w->W_TMatrix.TM_flags)
	w->W_curTMatrix = gs->GS_TMatrix;
    else
	GrMatMul(&w->W_curTMatrix, &gs->GS_TMatrix, &w->W_TMatrix);
    w->W_TMatrix.TM_flags &= (TransFlags) ~TF_INV_VALID;
    w->W_curTMatrix.TM_flags &= (TransFlags) ~TF_INV_VALID;
    GrSetDWF(&w->W_curTMatrix.TM_31, GrDWF(&w->W_curTMatrix.TM_31) +
	     (int64_t) w->W_winRect.R_left * 65536);
    GrSetDWF(&w->W_curTMatrix.TM_32, GrDWF(&w->W_curTMatrix.TM_32) +
	     (int64_t) w->W_winRect.R_top * 65536);
    w->W_curTMatrix.TM_flags |= TF_TRANSLATED;
    w->W_grFlags |= WGF_XFORM_VALID;
}

/* EnterTranslate */
TMatrix *GrLockTransform(GStateHandle gstate, GState **gs, Window **win)
{
    Window *w;

    *gs = MemLock(gstate);
    *win = NULL;
    if (!(*gs)->GS_window)
	return &(*gs)->GS_TMatrix;
    w = MemLock((*gs)->GS_window);
    if (!w)
	return &(*gs)->GS_TMatrix;
    if (!(w->W_grFlags & WGF_XFORM_VALID) || w->W_curState != gstate) {
	GrComposeMatrix(gstate, *gs, w);
	/* as the asm: not marked as this GState's */
	w->W_grFlags &= (WinGrFlags) ~WGF_XFORM_VALID;
    }
    *win = w;
    return &w->W_curTMatrix;
}

void GrUnlockTransform(GStateHandle gstate, GState *gs, Window *win)
{
    if (win)
	MemUnlock(gs->GS_window);
    MemUnlock(gstate);
}
