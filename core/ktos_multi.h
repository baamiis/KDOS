/**
 * @file ktos_multi.h
 * @brief Portable primitive types, constants, and utility prototypes used
 *        throughout the KTOS kernel and all BSPs.
 *
 * Include this header wherever KTOS types are needed.  It is automatically
 * pulled in by @c ktos.h and @c ktos_hal.h, so application code rarely needs
 * to include it directly.
 *
 * @ingroup ktos_types
 */

#ifndef KTOS_MULTI_H_INCLUDED
#define KTOS_MULTI_H_INCLUDED

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* =========================================================================
 * Compile-time configuration
 * ========================================================================= */

/**
 * @ingroup ktos_types
 * @brief Enable (1) or disable (0) debug-level logging.
 *
 * When set to 1 the kernel calls ktos_DebugPrintf() at key scheduling events.
 * Override before including any KTOS header:
 * @code
 * #define DEBUG 1
 * #include "ktos.h"
 * @endcode
 */
#ifndef DEBUG
  #define DEBUG 0
#endif

/* =========================================================================
 * Primitive types
 * ========================================================================= */

/**
 * @ingroup ktos_types
 * @brief 16-bit unsigned word — used for message types, parameters, and timers.
 */
typedef unsigned short WORD;

/**
 * @ingroup ktos_types
 * @brief 32-bit signed long — used for the wide message parameter @c lParam.
 */
typedef long LONG;

/**
 * @ingroup ktos_types
 * @brief Signed integer — used for stack and queue sizes.
 */
typedef int INT;

/**
 * @ingroup ktos_types
 * @brief Unsigned byte — used for task identifiers.
 */
typedef unsigned char BYTE;

/* =========================================================================
 * Boolean constants
 * ========================================================================= */

/** @ingroup ktos_types @brief Integer true  (1). */
#define TRUE  1

/** @ingroup ktos_types @brief Integer false (0). */
#define FALSE 0

/* =========================================================================
 * Utility functions (implemented per platform in ktos_multi.c or BSP)
 * ========================================================================= */

/**
 * @ingroup ktos_core
 * @brief Emit a fatal-error message and halt execution.
 *
 * Called by the kernel whenever an unrecoverable condition occurs
 * (e.g. memory allocation failure).  The BSP or application must provide
 * an implementation that outputs @p Msg via a debug channel (UART, ITM, …)
 * and then enters an infinite loop or triggers a watchdog reset.
 *
 * @param Msg  Short, null-terminated error string (ASCII).
 *
 * @note This function must never return.
 */
void ktos_Emergency(const char *Msg);

/**
 * @ingroup ktos_core
 * @brief Printf-style debug logging (active only when @c DEBUG == 1).
 *
 * The BSP or application must provide an implementation that routes output
 * to a UART or semihosting channel.  When @c DEBUG == 0 the calls are
 * compiled out by the kernel and this function is never called.
 *
 * @param Format  printf-compatible format string.
 * @param ...     Variadic arguments matching the format string.
 */
void ktos_DebugPrintf(const char *Format, ...);

/**
 * @ingroup ktos_core
 * @brief Optional platform-level system initialisation hook.
 *
 * Called by application code before ktos_InitTask() if low-level hardware
 * setup (clock configuration, watchdog disable, …) is needed.
 * May be a no-op on platforms where the BSP Makefile startup file handles
 * all initialisation.
 */
void ktos_InitSys(void);

#endif /* KTOS_MULTI_H_INCLUDED */
