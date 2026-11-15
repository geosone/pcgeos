/*
 * specUI.h -- the interface of a specific UI library (after
 * Include/Internal/specUI.def), in C for Port32.
 *
 * The generic UI loads the specific UI of [ui] specific (motif by
 * default) by hand, as two libraries may not depend on each other.  Its
 * first exported routines are, in this order, the SpecificUIRoutine
 * entries.  A Gen class resolves its variant superclass by calling the
 * entry for it (GenQueryUICallSpecificUI).
 *
 * Port32 (Port32/ABI.md section 13x): a build routine is a C function
 * that gets the generic object and the master offset of the variant
 * being resolved and returns the class to build it into (NULL: none).
 * The asm routines got *ds:si and returned cx:dx.
 */
#ifndef __SPECUI_H
#define __SPECUI_H

#define SPUI_PROTO_MAJOR	727
#define SPUI_PROTO_MINOR	0

typedef enum {
    SPIR_BUILD_TRIGGER,
    SPIR_BUILD_DISPLAY,
    SPIR_BUILD_APPLICATION,
    SPIR_BUILD_FIELD,
    SPIR_BUILD_SCREEN,
    SPIR_BUILD_SYSTEM,
    SPIR_BUILD_VIEW,
    SPIR_BUILD_INTERACTION_GROUP,
    SPIR_BUILD_GLYPH_DISPLAY,
    SPIR_BUILD_TEXT,
    SPIR_BUILD_DISPLAY_CONTROL,
    SPIR_BUILD_PRIMARY,
    SPIR_BUILD_GADGET,
    SPIR_BUILD_CONTENT,
    SPIR_BUILD_UI_DOCUMENT_CONTROL,
    SPIR_BUILD_APP_DOCUMENT_CONTROL,
    SPIR_BUILD_DOCUMENT,
    SPIR_BUILD_FILE_SELECTOR,
    SPIR_BUILD_BOOLEAN_GROUP,
    SPIR_BUILD_ITEM_GROUP,
    SPIR_BUILD_DYNAMIC_LIST,
    SPIR_BUILD_ITEM,
    SPIR_BUILD_BOOLEAN,
    SPIR_BUILD_VALUE,
    SPIR_GET_TEXT_KBD_BINDINGS,
    SPIR_GET_TEXT_POINTER_IMAGE,
    SPIR_GET_DISPLAY_SCHEME,
    SPIR_DRAW_TEXT_CURSOR,
    SPIR_GET_DOC_CONTROL_OPTIONS,
    SPIR_GET_WINDOW_OPTIONS,
    SPIR_TOOL_AREA_CLASS_PLACE_HOLDER_DO_NOT_USE,
    SPIR_WINDOW_LIST_DIALOG_CLASS_PLACE_HOLDER_DO_NOT_USE,
    SPIR_OLFS_DYNAMIC_LIST_CLASS_PLACE_HOLDER_DO_NOT_USE,
    SPIR_EMC_CONTROL_PANEL_CLASS_PLACE_HOLDER_DO_NOT_USE,
    SPIR_BUILD_PEN_INPUT_CONTROL
} SpecificUIRoutine;

/* SPIR_BUILD_*: the class for the generic object oself */
typedef ClassStruct *SpecBuildRoutine(optr oself, word masterOffset);

#endif
