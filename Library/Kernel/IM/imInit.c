/*
 * imInit.c -- the input manager's thread and class (after imInit.asm:
 * InitIM, IMAttach, InitIMMonitors; ProcessUserInput as the method of
 * the input messages, imMonitors.asm).
 *
 * The input manager is an event thread of the kernel with IMClass, a
 * subclass of ProcessClass.  The input drivers send it MSG_IM_* (the
 * keyboard driver from its interrupt, MSG_IM_KBD_SCAN); each goes
 * through the monitor chain (imMonitors.c) on this thread.
 *
 * KernelImInit makes the thread at the boot, before the drivers, which
 * add their monitors and send to it; it starts with KernelImStart, when
 * the drivers are there: its MSG_META_ATTACH looks for the default
 * keyboard driver and reads [input]:
 *   leftHanded            buttons 0 and 2 change places
 *   doubleClickTime       in ticks
 *   rebootOnReset         <Ctrl><Alt><Del> restarts the machine
 *   quickShutdownOnReset  the first <Ctrl><Alt><Del> ends GEOS dirty
 * DOS16 sent MSG_META_ATTACH with MSG_PROCESS_CREATE_EVENT_THREAD.
 *
 * The screen saver is in imScreenSaver.c.
 *
 * Not yet: pen mode (its stack).
 */
#include "kernelC.h"
#include "imInt.h"

#include <initfile.h>
#include <driver.h>

#define IM_STACK_SIZE	32768

static const char inputCategory[] = "input";

/* the input messages: an event through the chain */
static void ImInput(void *pself, optr oself, Message m, void *params,
		    void *reply)
{
    ImEvent ev;
    const IMParams *p = params;

    (void) pself; (void) oself; (void) reply;
    memset(&ev, 0, sizeof(ev));
    ev.IE_message = m;
    if (p) {
	ev.IE_cx = p->IMP_cx;
	ev.IE_dx = p->IMP_dx;
	ev.IE_bp = p->IMP_bp;
	if (m != IM_META_KBD_CHAR)	/* goc's KBD_CHAR has three words */
	    ev.IE_si = p->IMP_si;
    }
    ImProcessUserInput(&ev);
}

static Boolean ReadBoolean(const char *key, Boolean *b)
{
    Boolean v;

    if (InitFileReadBoolean(inputCategory, key, &v))
	return FALSE;
    *b = v ? TRUE : FALSE;
    return TRUE;
}

/* IMAttach */
static void ImAttach(void *pself, optr oself, Message m, void *params,
		     void *reply)
{
    GeodeHandle kbd = GeodeGetDefaultDriver(GDDT_KEYBOARD);
    word w;

    (void) pself; (void) oself; (void) m; (void) params; (void) reply;
    im.kbdDriver = kbd ? GeodeInfoDriver(kbd) : NULL;
    if (ReadBoolean("rebootOnReset", &im.rebootOnReset))
	ReadBoolean("quickShutdownOnReset", &im.alreadyReset);
    ReadBoolean("leftHanded", &im.leftHanded);
    if (!InitFileReadInteger(inputCategory, "doubleClickTime", &w))
	im.doubleClickTime = w;
}

static const Message imMessages[] = {
    MSG_IM_BUTTON_CHANGE,
    MSG_IM_PTR_CHANGE,
    MSG_IM_PRESSURE_CHANGE,
    MSG_IM_DIRECTION_CHANGE,
    MSG_IM_KBD_SCAN,
    MSG_IM_MOUSE_WHEEL_VERTICAL,
    IM_META_KBD_CHAR,
    KMSG_META_ATTACH,
    MSG_IM_ENABLE_SCREEN_SAVER,
    MSG_IM_DISABLE_SCREEN_SAVER,
    MSG_IM_INSTALL_SCREEN_SAVER,
    MSG_IM_REMOVE_SCREEN_SAVER,
    MSG_IM_GET_SCREEN_SAVER_DELAY,
    MSG_IM_SET_SCREEN_SAVER_DELAY,
    MSG_IM_SCREEN_SAVER_COUNTDOWN,
    MSG_IM_SCREEN_SAVER_COUNTDOWN_REACHED_ZERO,
    MSG_IM_ACTIVATE_SCREEN_SAVER,
    MSG_IM_DEACTIVATE_SCREEN_SAVER
};

static GocMethodThunk *const imMethods[] = {
    ImInput, ImInput, ImInput, ImInput, ImInput, ImInput, ImInput,
    ImAttach,
    ImScreenSaverMethod, ImScreenSaverMethod, ImScreenSaverMethod,
    ImScreenSaverMethod, ImScreenSaverMethod, ImScreenSaverMethod,
    ImScreenSaverMethod, ImScreenSaverMethod, ImScreenSaverMethod,
    ImScreenSaverMethod
};

ClassStruct IMClass = {
    &ProcessClass,
    0,
    sizeof(imMessages) / sizeof(imMessages[0]),
    sizeof(ClassStruct *),
    NULL, NULL,
    CLASSF_NEVER_SAVED,
    0,
    imMessages,
    (MessageMethod *const *) imMethods,
    NULL,
    NULL
};

/* InitIM: the thread, stopped; its monitors */
void KernelImInit(void)
{
    QueueHandle q;

    if (im.imThread)
	return;
    memset(&im, 0, sizeof(im));
    im.doubleClickTime = IM_DEFAULT_DOUBLE_CLICK_TIME;
    im.doubleClickDistance = IM_DEFAULT_DOUBLE_CLICK_DISTANCE;
    im.dragTime = IM_DEFAULT_DRAG_TIME;
    im.dragDistance = IM_DEFAULT_DRAG_DISTANCE;
    ImInitScreenSaver();
    ImInitMonitors();
    q = GeodeAllocQueue();
    HandleEntryOf(q)->HE_owner = kernelCurrentGeode;
    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(q, 0), KMSG_META_ATTACH,
		 NULL, 0, NULL, 0);
    im.imThread = KernelEventThreadCreate(q, &IMClass, PRIORITY_HIGH,
					  IM_STACK_SIZE, kernelCurrentGeode);
    if (!im.imThread)
	KernelFatal(CANNOT_START_INPUT_MANAGER);
}

void KernelImStart(void)
{
    KernelThreadRun(im.imThread);
}
