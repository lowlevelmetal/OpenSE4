// MicroPython's hardware layer for OpenSE4's script runtime: nothing but text
// output, which port.c keeps for the interpreter (print()). There is no input, no
// clock and no delay.

#ifndef OPENSE4_MPHALPORT_H
#define OPENSE4_MPHALPORT_H

#define mp_hal_set_interrupt_char(c) ((void)(c))

#endif // OPENSE4_MPHALPORT_H
