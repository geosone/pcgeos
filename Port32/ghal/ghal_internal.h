/*
 * ghal_internal.h -- what the parts of the GHAL use among themselves; not
 * for its users.
 */
#ifndef GHAL_INTERNAL_H
#define GHAL_INTERNAL_H

#include "ghal.h"

/* Run the handlers attached to a source other than GHAL_IRQ_TICK, as an
 * interrupt: inside ghal_crit, ghal_irq_active() true.  Not inside
 * ghal_crit or a handler (dos32: may be called inside, it nests). */
void ghal_irq_raise(int source);

#endif /* GHAL_INTERNAL_H */
