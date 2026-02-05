#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>

/* =========================
 * Application States
 * (matches state machine: OFF → BOOT → SAFE ↔ MAINTENANCE ↔ CONFIGURATION ↔ NOMINAL)
 * ========================= */
typedef enum
{
    STATE_OFF = 0,
    STATE_BOOT,
    STATE_SAFE,           /* stand-by */
    STATE_MAINTENANCE,
    STATE_CONFIGURATION,
    STATE_NOMINAL
} app_state_t;

/* =========================
 * Public Interface
 * ========================= */

/* State transition (valid transitions enforced internally) */
void transition_to(app_state_t new_state);

/* Current state (read-only) */
app_state_t app_get_state(void);

/* State handlers (called each loop while in that state) */
void state_off_handler(void);
void state_boot_handler(void);
void state_safe_handler(void);
void state_maintenance_handler(void);
void state_configuration_handler(void);
void state_nominal_handler(void);

/* System-level: call from main loop */
void system_init(void);
void check_commands(void);           /* s/c commands → may request transitions */
void check_autonomous_events(void);  /* autonomous conditions → may request transitions */

#endif /* APP_H */
