/*
 * graphicsMacro.h -- after graphicsMacro.def: the macros of the module, and
 * the helpers its files share.
 */
#ifndef H_GRAPHICSMACRO
#define H_GRAPHICSMACRO

#define AREA	((word) offsetof(GState, GS_areaAttr))
#define CGSC_WRITING	1
#define LINE	((word) offsetof(GState, GS_lineAttr))
#define TEXT	((word) offsetof(GState, GS_textAttr))

#endif /* H_GRAPHICSMACRO */
