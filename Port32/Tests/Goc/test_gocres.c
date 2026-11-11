/*
 * test_gocres.c -- runs the checks of gocres.goc (goc -cg output).
 *
 * The classes of the UI library are stubbed, and a minimal message
 * kernel stands in for the real one: every message goes to
 * GocTestProcessClass (or the superclass for @callsuper) through the
 * method thunks of its class structure, @record keeps one event.
 */
#include <geos.h>
#include <object.h>
#include <geode.h>
#include <stdio.h>
#include <string.h>

ClassStruct GenProcessClass, GenApplicationClass, GenPrimaryClass,
    GenTriggerClass, GenGlyphClass;
Handle __goc_resource_handles[8];
extern ClassStruct GocTestProcessClass;

int gocTestSuperCalls;

GeodeHandle GeodeGetProcessHandle(void) { return 1; }
dword CObjMessage(void) { return 0; }

static void *Deliver(ClassStruct *cls, optr dest, Message message,
		     const void *params, void *reply)
{
    word i;

    for (; cls != NULL; cls = cls->Class_superClass) {
	for (i = 0; i < cls->Class_methodCount; i++) {
	    if (cls->Class_messages[i] == message) {
		((GocMethodThunk *) cls->Class_methods[i])
		    (NULL, dest, message, (void *) params, reply);
		return reply;
	    }
	}
    }
    return reply;
}

/* the one recorded event */
static struct {
    optr dest;
    Message message;
    byte params[64];
} event;

void *CObjMessageP(word flags, optr dest, Message message,
		   const void *params, dword paramSize,
		   void *reply, dword replySize)
{
    if (flags & MF_RECORD) {
	if (paramSize > sizeof(event.params) || replySize != sizeof(EventHandle)) {
	    printf("FAIL record: sizes %u/%u\n", (unsigned) paramSize,
		   (unsigned) replySize);
	    return reply;
	}
	event.dest = dest;
	event.message = message;
	memcpy(event.params, params, paramSize);
	*(EventHandle *) reply = 1;
	return reply;
    }
    return Deliver(&GocTestProcessClass, dest, message, params, reply);
}

void *CObjCallSuperP(ClassStruct *superOf, optr dest, Message message,
		     const void *params, dword paramSize,
		     void *reply, dword replySize)
{
    (void) paramSize;
    (void) replySize;
    gocTestSuperCalls++;
    return Deliver(superOf->Class_superClass, dest, message, params, reply);
}

void *CMessageDispatchP(EventHandle ev, word flags, void *reply,
			dword replySize)
{
    (void) flags;
    (void) replySize;
    if (ev != 1) {
	return reply;
    }
    return Deliver(&GocTestProcessClass, event.dest, event.message,
		   event.params, reply);
}

extern int GocTestCheck(void);

int main(void)
{
    int fails = GocTestCheck();

    printf("%s: %d failure(s)\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
