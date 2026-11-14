/*
 * powerDr.h -- the power management driver interface (the C form of
 * Include/Internal/powerDr.def).
 *
 * The kernel loads the driver named by "[system] power" from
 * SP_POWER_DRIVERS and makes it the default GDDT_POWER_MANAGEMENT
 * driver.  DOS16 called it when the CPU had nothing to do
 * (DR_POWER_IDLE) and after interrupts; in Port32 the host schedules
 * the CPU and those calls are not made.  The screen saver still offers
 * it the long term idle (DR_POWER_LONG_TERM_IDLE).
 *
 * The registers are those of DriverRegs (geosdriver.h); the strategy
 * routine returns the carry flag.
 */
#ifndef POWERDR_H
#define POWERDR_H

#include <driver.h>

#define POWER_PROTO_MAJOR	(DRIVER_PROTO_MAJOR + 1)
#define POWER_PROTO_MINOR	(DRIVER_PROTO_MINOR + 0)

/* PowerManagementFunction */
#define DR_POWER_IDLE				8
#define DR_POWER_NOT_IDLE			10
#define DR_POWER_NOT_IDLE_ON_INTERRUPT_COMPLETION 12
/* the screen saver's time has come: carry set if the driver dealt with
 * it (turned the machine off), clear to blank the screen */
#define DR_POWER_LONG_TERM_IDLE			14
/* ax = PowerGetStatusType; returns ax, bx, cx, dx; carry: unsupported */
#define DR_POWER_GET_STATUS			16
/* bx = PowerSetStatusType, dx:ax the value; carry: unsupported */
#define DR_POWER_SET_STATUS			18
/* ax = PowerDeviceType, bx unit, cx non-zero: on, dx device data;
 * carry and ax = PowerDeviceError when it cannot be turned on */
#define DR_POWER_DEVICE_ON_OFF			20
#define DR_POWER_SET_PASSWORD_OLD		22
#define DR_POWER_VERIFY_PASSWORD_OLD		24
/* DR_farPtr = the routine (void (*)(PowerNotifyChange)); carry: full */
#define DR_POWER_ON_OFF_NOTIFY			26
#define DR_POWER_DISABLE_PASSWORD_OLD		28
#define DR_POWER_PASSWORD_OK_OLD		30
#define DR_POWER_RTC_ACK_OLD			32
/* DR_farPtr = the routine; carry: not registered */
#define DR_POWER_ON_OFF_UNREGISTER		34
/* si = PowerEscCommand; carry: unsupported */
#define DR_POWER_ESC_COMMAND			36
/* the screen is unblanked again */
#define DR_POWER_LONG_TERM_IDLE_END		38

typedef enum {
    PGST_POWER_ON_WARNINGS,	/* ax PowerWarnings, bx those supported */
    PGST_POLL_WARNINGS,		/* the same, polled */
    PGST_STATUS,		/* ax PowerStatus, bx those supported */
    PGST_BATTERY_CHARGE_MINUTES,	/* dx:ax minutes left */
    PGST_BATTERY_CHARGE_PERCENT,	/* dx:ax 0 (none) to 1000 (full) */
    PGST_BATTERY_CHARGE_MINUTES_BACKUP,
    PGST_BATTERY_CHARGE_PERCENT_BACKUP
} PowerGetStatusType;

/* PowerWarnings (Esp records: the first field is the high bit) */
#define PW_MAIN_BATTERY			0x8000
#define PW_BACKUP_BATTERY		0x4000
#define PW_PCMCIA_SLOT_1_BATTERY	0x2000
#define PW_PCMCIA_SLOT_2_BATTERY	0x1000
#define PW_PCMCIA_SLOT_3_BATTERY	0x0800
#define PW_PCMCIA_SLOT_4_BATTERY	0x0400
#define PW_CUSTOM_1			0x0200
#define NUMBER_OF_STANDARD_POWER_WARNINGS 7

/* PowerStatus */
#define PS_AC_ADAPTER_CONNECTED		0x8000
#define PS_CHARGING			0x4000

typedef enum {
    PSST_BATTERY_STATUS = 0,
    PSST_SUSPEND_STATUS = 2,
    PSST_IDLE_STATUS = 4
} PowerSetStatusType;

typedef enum {
    PDE_LOW_POWER,
    PDE_NO_SUCH_DEVICE
} PowerDeviceError;

typedef enum {
    PDT_SERIAL_PORT,
    PDT_PARALLEL_PORT,
    PDT_PCMCIA_SOCKET,
    PDT_KEYBOARD,
    PDT_DISPLAY,
    PDT_SPEAKER,
    PDT_CPU,
    PDT_AC_ADAPTER,
    PDT_MAIN_BATTERY,
    PDT_BACKUP_BATTERY,
    PDT_MICROPHONE,
    PDT_TELEPHONE
} PowerDeviceType;

typedef enum {
    PNC_POWER_SHUTTING_OFF,
    PNC_POWER_TURNING_ON,
    PNC_POWER_TURNED_OFF_AND_ON,
    PNC_POWER_AUTO_OFF
} PowerNotifyChange;

typedef void PowerNotifyRoutine(PowerNotifyChange change);

#endif /* POWERDR_H */
