/*
 * sysConstant.h -- after sysConstant.def: the constants and structures of
 * the Sys module that the port has.
 */
#ifndef H_SYSCONSTANT
#define H_SYSCONSTANT

#include <system.h>

/* the subsystems of SysSetNotification (one per SysSubsystemType) and
 * how each is told about errors */
#define SST_COUNT	(SST_SOCKET / 9 + 1)

typedef SysNotificationRoutine SysNotifyRoutine;

typedef enum { SNE_QUEUE, SNE_HOOKED, SNE_IGNORED } SNEState;

typedef struct {
    SNEState		state;
    SysNotifyRoutine	*routine;
    QueueHandle		queue;		/* SNE_QUEUE: 0 until the first */
} SubsystemNotificationElement;

#endif /* H_SYSCONSTANT */
