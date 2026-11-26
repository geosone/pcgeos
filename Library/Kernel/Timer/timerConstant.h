/*
 * timerConstant.h -- after timerConstant.def: the structures of the Timer
 * module that the port has.
 */
#ifndef H_TIMERCONSTANT
#define H_TIMERCONSTANT

#include <timer.h>

/* a timer (the asm's HandleTimer) */
typedef struct KTimer {
    struct KTimer *TI_next;
    TimerHandle	TI_handle;
    TimerType	TI_type;
    dword	TI_due;			/* tick */
    word	TI_interval;		/* continual: ticks */
    word	TI_id;
    optr	TI_dest;		/* object, or the routine */
    Message	TI_msg;			/* message, or the routine's data */
} KTimer;

#endif /* H_TIMERCONSTANT */
