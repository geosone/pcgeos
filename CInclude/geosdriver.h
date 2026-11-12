/*
 * geosdriver.h -- the strategy routine of a driver in Port32 (the C form
 * of Include/driver.def; Port32/ABI.md section 7.2).
 *
 * A DOS16 driver had a strategy routine called with the function number
 * in di and its arguments in registers, as the .def file of its driver
 * type describes them; the carry flag returned an error.  In Port32 the
 * strategy routine is a C function that takes the function number and
 * the registers in a structure.  Each register is a dword: handles are
 * 32 bits wide (ABI.md section 2), and a "segment:offset" pair is a
 * pointer.  The return value is the carry flag.
 *
 * The file system drivers have their own table of C functions instead
 * (Internal/fsDriver.h).
 *
 * A driver geode defines its DriverInfoStruct as the variable
 * "DriverTable", as glue required; gpgeode.py puts its address into
 * __geos_module (GM_driverTable).
 *
 *	static Boolean MyStrategy(word function, DriverRegs *r);
 *	DriverInfoStruct DriverTable = {
 *	    DRIVER_STRATEGY(MyStrategy), DA_CHARACTER, DRIVER_TYPE_STREAM
 *	};
 */
#ifndef GEOSDRIVER_H
#define GEOSDRIVER_H

#include <geos.h>
#include <driver.h>

typedef struct {
    dword	DR_ax, DR_bx, DR_cx, DR_dx, DR_si, DR_bp;
    void	*DR_dsSi;	/* ds:si as a pointer */
    void	*DR_esDi;	/* es:di as a pointer */
    dword	DR_di;		/* di as a value (DR_KBD_XLATE_SCAN) */
    void	*DR_esSi;	/* es:si as a pointer */
    void	*DR_bpPtr;	/* bp with its segment as a pointer: ss:bp
			 * (a structure on the stack), ds:bp */
} DriverRegs;

/* TRUE: the carry flag, an error */
typedef Boolean DriverStrategy(word function, DriverRegs *regs);

/* DIS_strategy is declared "void (*)()" in driver.h */
#define DRIVER_STRATEGY(f)	((void (*)()) (void *) (f))
#define DriverStrategyOf(dis)	((DriverStrategy *) (void *) (dis)->DIS_strategy)

/* call the strategy routine of a DriverInfoStruct */
#define DriverCall(dis, function, regs) \
    (DriverStrategyOf(dis)((word) (function), (regs)))

#endif /* GEOSDRIVER_H */
