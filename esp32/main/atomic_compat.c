/*
 * FMRack ESP32-S3 — Missing GCC atomic built-in for Xtensa
 *
 * Xtensa GCC 14.2 does not ship libatomic.  Provide the one
 * function the FMRack Rack.cpp needs: __atomic_test_and_set
 * for std::atomic_flag.
 *
 * On ESP32-S3 the S32C1I instruction provides a HW compare-and-swap.
 * We use a FreeRTOS spinlock (portMUX) for portability and safety.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static portMUX_TYPE s_atomic_mux = portMUX_INITIALIZER_UNLOCKED;

/*
 * GCC expects:
 *   bool __atomic_test_and_set(volatile void *ptr, int memorder);
 *
 * Sets *ptr to non-zero and returns the previous value (true if was set).
 */
bool __atomic_test_and_set(volatile void *ptr, int memorder)
{
    (void)memorder;

    volatile unsigned char *p = (volatile unsigned char *)ptr;
    bool prev;

    portENTER_CRITICAL(&s_atomic_mux);
    prev = *p != 0;
    *p = 1;
    portEXIT_CRITICAL(&s_atomic_mux);

    return prev;
}

/*
 * GCC expects:
 *   void __atomic_clear(volatile void *ptr, int memorder);
 *
 * Clears (sets to 0) the flag pointed to by ptr.
 */
void __atomic_clear(volatile void *ptr, int memorder)
{
    (void)memorder;

    volatile unsigned char *p = (volatile unsigned char *)ptr;

    portENTER_CRITICAL(&s_atomic_mux);
    *p = 0;
    portEXIT_CRITICAL(&s_atomic_mux);
}
