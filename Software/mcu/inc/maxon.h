#ifndef MAXON_H
#define MAXON_H

#include <stdint.h>
#include <stdbool.h>

/* =========================
 * Maxon motor control (stubs for now)
 * ========================= */

/* Initialize motor drivers / communication (e.g. EPOS, CAN, UART) */
void maxon_init(void);

/* Enable/disable motor power or enable signal */
void maxon_enable(void);
void maxon_disable(void);

/* Motion commands – empty implementations for now */
void maxon_set_velocity(int32_t velocity_cps);
void maxon_set_position(int32_t position_counts);
void maxon_stop(void);

/* Optional: multi-axis if payload has several motors */
void maxon_set_velocity_axis(uint8_t axis, int32_t velocity_cps);
void maxon_set_position_axis(uint8_t axis, int32_t position_counts);

#endif /* MAXON_H */
