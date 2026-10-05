// Visual C++ has no <unistd.h>; MicroPython includes it only for ssize_t.

#ifndef OPENSE4_MSVC_UNISTD_H
#define OPENSE4_MSVC_UNISTD_H

#include <BaseTsd.h>
typedef SSIZE_T ssize_t;

#endif // OPENSE4_MSVC_UNISTD_H
