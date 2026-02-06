/*
 * Optical payload application – state machine (OFF / BOOT / SAFE / MAINTENANCE / CONFIGURATION / NOMINAL).
 * Transitions: commanded (s/c) and autonomous (delay, data taking plan, hard limit).
 * Maxon motor control is stubbed for now.
 */

#include "app.h"
#include "maxon.h"
#include "tcp_server.h"
#include "pico/cyw43_arch.h"
#include "pico/stdio.h"
#include "pico/time.h"
#include <cyw43.h>
#include <cyw43_country.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * State and transition request (set by check_commands / check_autonomous_events)
 * ------------------------------------------------------------------------- */
static app_state_t current_state = STATE_OFF;
static app_state_t requested_transition = STATE_OFF;  /* no request when equal to current_state */
static bool transition_requested;

/* Boot: delay before BOOT → SAFE (automatic if application OK and not prevented by s/c) */
#define BOOT_TO_SAFE_DELAY_MS 2000
static absolute_time_t boot_done_at;

/* -------------------------------------------------------------------------
 * Valid transitions (from state machine diagram)
 * ------------------------------------------------------------------------- */
static bool is_valid_transition(app_state_t from, app_state_t to)
{
    switch (from) {
        case STATE_OFF:
            return to == STATE_BOOT;
        case STATE_BOOT:
            return to == STATE_SAFE;
        case STATE_SAFE:
            return to == STATE_OFF || to == STATE_BOOT || to == STATE_MAINTENANCE || to == STATE_CONFIGURATION;
        case STATE_MAINTENANCE:
            return to == STATE_SAFE || to == STATE_CONFIGURATION;
        case STATE_CONFIGURATION:
            return to == STATE_MAINTENANCE || to == STATE_NOMINAL;
        case STATE_NOMINAL:
            return to == STATE_CONFIGURATION;
        default:
            return false;
    }
}

void transition_to(app_state_t new_state)
{
    if (!is_valid_transition(current_state, new_state))
        return;
    current_state = new_state;
    transition_requested = false;
    requested_transition = current_state;
    if (current_state == STATE_BOOT)
        boot_done_at = make_timeout_time_ms(BOOT_TO_SAFE_DELAY_MS);
}

app_state_t app_get_state(void)
{
    return current_state;
}

/* -------------------------------------------------------------------------
 * Command / autonomous checks (stubs: set requested_transition for testing)
 * In flight: check_commands() would parse s/c commands; check_autonomous_events()
 * would evaluate delay, data taking plan, hard limits.
 * ------------------------------------------------------------------------- */
void check_commands(void)
{
    /* TODO: parse spacecraft command interface (e.g. UART, USB, CAN).
     * For now: no command source; could add a simple trigger (e.g. GPIO or USB) for testing. */
    (void)requested_transition;
}

void check_autonomous_events(void)
{
    if (current_state == STATE_BOOT) {
        if (absolute_time_diff_us(get_absolute_time(), boot_done_at) <= 0) {
            /* Automatic BOOT → SAFE after delay if application OK and not prevented by s/c */
            requested_transition = STATE_SAFE;
            transition_requested = true;
        }
    }
    /* TODO: CONFIGURATION ↔ NOMINAL from preloaded data taking plan.
     * TODO: CONFIGURATION → MAINTENANCE, MAINTENANCE → SAFE on hard limit exceeded. */
}

/* -------------------------------------------------------------------------
 * State handlers (each runs once per main loop while in that state)
 * ------------------------------------------------------------------------- */
void state_off_handler(void)
{
    /* Powered down; motors not driven */
    maxon_disable();
    printf("State OFF\n");
}

void state_boot_handler(void)
{
    /* Start-up software: init, then wait for BOOT_TO_SAFE_DELAY_MS */
    maxon_disable();
    printf("State BOOT\n");
}

void state_safe_handler(void)
{
    /* Stand-by: low power, motors disabled, ready for MAINTENANCE / CONFIGURATION / OFF / BOOT */
    maxon_disable();
    printf("State SAFE\n");
}

void state_maintenance_handler(void)
{
    /* Diagnostics / updates; motors may be enabled for testing (stub) */
    maxon_disable();  /* or maxon_enable() for diagnostic motion when implemented */
    printf("State MAINTENANCE\n");
}

void state_configuration_handler(void)
{
    /* Parameter set; no nominal motion */
    maxon_disable();
    printf("State CONFIGURATION\n");
}

void state_nominal_handler(void)
{
    /* Primary mission: operate motors according to data taking plan (stub) */
    maxon_enable();
    /* maxon_set_velocity(...) or maxon_set_position(...) when plan is active */
    printf("State NOMINAL\n");
}

/* -------------------------------------------------------------------------
 * System init (hardware, CYW43 for LED, maxon stubs)
 * ------------------------------------------------------------------------- */
void system_init(void)
{
    stdio_init_all();
    sleep_ms(5000);

    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return;
    }

    // Enable AP mode
    cyw43_arch_enable_ap_mode(
        "PICO_AP",
        "1234567890",
        CYW43_AUTH_WPA2_AES_PSK
    );

    printf("AP IP now: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));

    start_tcp_server();
    maxon_init();
}

/* -------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------- */
int main(void)
{
    system_init();

    /* Start in OFF. Transition OFF → BOOT is by "HPC from s/c command".
     * For bring-up without s/c, we go to BOOT once so the machine can run. */
    transition_to(STATE_BOOT);
    boot_done_at = make_timeout_time_ms(BOOT_TO_SAFE_DELAY_MS);

    while (true) {
        check_commands();
        check_autonomous_events();

        if (transition_requested)
            transition_to(requested_transition);

        switch (current_state) {
            case STATE_OFF: state_off_handler(); break;
            case STATE_BOOT: state_boot_handler(); break;
            case STATE_SAFE: state_safe_handler(); break;
            case STATE_MAINTENANCE: state_maintenance_handler(); break;
            case STATE_CONFIGURATION: state_configuration_handler(); break;
            case STATE_NOMINAL: state_nominal_handler(); break;
        }

        sleep_ms(1000);
    }
}
