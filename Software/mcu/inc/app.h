#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>

/* =========================
 * Application States
 * ========================= */
typedef enum
{
    STATE_OFF = 0,
    STATE_BOOT,
    STATE_SAFE,
    STATE_CALIBRATION, // New State
    STATE_MAINTENANCE,
    STATE_CONFIGURATION,
    STATE_NOMINAL
} app_state_t;

/* =========================
 * Public Interface
 * ========================= */

void transition_to(app_state_t new_state);
app_state_t app_get_state(void);

/* State handlers */
void state_off_handler(void);
void state_boot_handler(void);
void state_safe_handler(void);
void state_calibration_handler(void); // New Handler
void state_maintenance_handler(void);
void state_configuration_handler(void);
void state_nominal_handler(void);

void system_init(void);
void check_commands(void);
void check_autonomous_events(void);

#endif /* APP_H */