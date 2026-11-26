/*
 * spoolVariable.c -- after spoolVariable.def: the variables of the
 * spooler (declared in spoolLocal.h).
 */
#include "spoolLocal.h"

MemHandle	queueHandle;
SemaphoreHandle	queueSemaphore;
word		spoolStateFlags;
word		nextJobID;
word		customQueueID;
ThreadLockHandle errorThreadLock;
word		errorBoxCount;
word		spoolAckID;
optr		spoolAckOD;
Boolean		spoolDetachHeld;	/* Port32: a detach waits for the queues */
