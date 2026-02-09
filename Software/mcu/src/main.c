// /*
//  * Optical payload application – state machine (OFF / BOOT / SAFE / MAINTENANCE / CONFIGURATION / NOMINAL).
//  * Transitions: commanded (s/c) and autonomous (delay, data taking plan, hard limit).
//  * Maxon motor control is stubbed for now.
//  */

// #include "app.h"
// #include "maxon.h"
// #include "tcp_server.h"
// #include "pico/cyw43_arch.h"
// #include "pico/stdio.h"
// #include "pico/time.h"
// #include <cyw43.h>
// #include <cyw43_country.h>
// #include <stdio.h>

// /* -------------------------------------------------------------------------
//  * State and transition request (set by check_commands / check_autonomous_events)
//  * ------------------------------------------------------------------------- */
// static app_state_t current_state = STATE_OFF;
// static app_state_t requested_transition = STATE_OFF;  /* no request when equal to current_state */
// static bool transition_requested;

// /* Boot: delay before BOOT → SAFE (automatic if application OK and not prevented by s/c) */
// #define BOOT_TO_SAFE_DELAY_MS 2000
// static absolute_time_t boot_done_at;

// /* -------------------------------------------------------------------------
//  * Valid transitions (from state machine diagram)
//  * ------------------------------------------------------------------------- */
// static bool is_valid_transition(app_state_t from, app_state_t to)
// {
//     switch (from) {
//         case STATE_OFF:
//             return to == STATE_BOOT;
//         case STATE_BOOT:
//             return to == STATE_SAFE;
//         case STATE_SAFE:
//             return to == STATE_OFF || to == STATE_BOOT || to == STATE_MAINTENANCE || to == STATE_CONFIGURATION;
//         case STATE_MAINTENANCE:
//             return to == STATE_SAFE || to == STATE_CONFIGURATION;
//         case STATE_CONFIGURATION:
//             return to == STATE_MAINTENANCE || to == STATE_NOMINAL;
//         case STATE_NOMINAL:
//             return to == STATE_CONFIGURATION;
//         default:
//             return false;
//     }
// }

// void transition_to(app_state_t new_state)
// {
//     if (!is_valid_transition(current_state, new_state))
//         return;
//     current_state = new_state;
//     transition_requested = false;
//     requested_transition = current_state;
//     if (current_state == STATE_BOOT)
//         boot_done_at = make_timeout_time_ms(BOOT_TO_SAFE_DELAY_MS);
// }

// app_state_t app_get_state(void)
// {
//     return current_state;
// }

// /* -------------------------------------------------------------------------
//  * Command / autonomous checks (stubs: set requested_transition for testing)
//  * In flight: check_commands() would parse s/c commands; check_autonomous_events()
//  * would evaluate delay, data taking plan, hard limits.
//  * ------------------------------------------------------------------------- */
// void check_commands(void)
// {
//     /* TODO: parse spacecraft command interface (e.g. UART, USB, CAN).
//      * For now: no command source; could add a simple trigger (e.g. GPIO or USB) for testing. */
//     (void)requested_transition;
// }

// void check_autonomous_events(void)
// {
//     if (current_state == STATE_BOOT) {
//         if (absolute_time_diff_us(get_absolute_time(), boot_done_at) <= 0) {
//             /* Automatic BOOT → SAFE after delay if application OK and not prevented by s/c */
//             requested_transition = STATE_SAFE;
//             transition_requested = true;
//         }
//     }
//     /* TODO: CONFIGURATION ↔ NOMINAL from preloaded data taking plan.
//      * TODO: CONFIGURATION → MAINTENANCE, MAINTENANCE → SAFE on hard limit exceeded. */
// }

// /* -------------------------------------------------------------------------
//  * State handlers (each runs once per main loop while in that state)
//  * ------------------------------------------------------------------------- */
// void state_off_handler(void)
// {
//     /* Powered down; motors not driven */
//     maxon_disable();
//     printf("State OFF\n");
// }

// void state_boot_handler(void)
// {
//     /* Start-up software: init, then wait for BOOT_TO_SAFE_DELAY_MS */
//     maxon_disable();
//     printf("State BOOT\n");
// }

// void state_safe_handler(void)
// {
//     /* Stand-by: low power, motors disabled, ready for MAINTENANCE / CONFIGURATION / OFF / BOOT */
//     maxon_disable();
//     printf("State SAFE\n");
// }

// void state_maintenance_handler(void)
// {
//     /* Diagnostics / updates; motors may be enabled for testing (stub) */
//     maxon_disable();  /* or maxon_enable() for diagnostic motion when implemented */
//     printf("State MAINTENANCE\n");
// }

// void state_configuration_handler(void)
// {
//     /* Parameter set; no nominal motion */
//     maxon_disable();
//     printf("State CONFIGURATION\n");
// }

// void state_nominal_handler(void)
// {
//     /* Primary mission: operate motors according to data taking plan (stub) */
//     maxon_enable();
//     /* maxon_set_velocity(...) or maxon_set_position(...) when plan is active */
//     printf("State NOMINAL\n");
// }

// /* -------------------------------------------------------------------------
//  * System init (hardware, CYW43 for LED, maxon stubs)
//  * ------------------------------------------------------------------------- */
// void system_init(void)
// {
//     stdio_init_all();
//     sleep_ms(2000);

//     if (cyw43_arch_init()) {
//         printf("Wi-Fi init failed\n");
//         return;
//     }

//     // Enable AP mode
//     cyw43_arch_enable_ap_mode(
//         "PICO_AP",
//         "1234567890",
//         CYW43_AUTH_WPA2_AES_PSK
//     );

//     printf("AP IP now: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));

//     start_tcp_server();
//     maxon_init();
// }

// /* -------------------------------------------------------------------------
//  * Main
//  * ------------------------------------------------------------------------- */
// int main(void)
// {
//     system_init();

//     /* Start in OFF. Transition OFF → BOOT is by "HPC from s/c command".
//      * For bring-up without s/c, we go to BOOT once so the machine can run. */
//     transition_to(STATE_BOOT);
//     boot_done_at = make_timeout_time_ms(BOOT_TO_SAFE_DELAY_MS);

//     while (true) {
//         check_commands();
//         check_autonomous_events();

//         if (transition_requested)
//             transition_to(requested_transition);

//         switch (current_state) {
//             case STATE_OFF: state_off_handler(); break;
//             case STATE_BOOT: state_boot_handler(); break;
//             case STATE_SAFE: state_safe_handler(); break;
//             case STATE_MAINTENANCE: state_maintenance_handler(); break;
//             case STATE_CONFIGURATION: state_configuration_handler(); break;
//             case STATE_NOMINAL: state_nominal_handler(); break;
//         }

//         sleep_ms(100);
//     }
// }



#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// --------------------------------------------------------------------------
// HARDWARE CONFIGURATION
// --------------------------------------------------------------------------
#define SPI_PORT spi0
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7

// EPOS4 Settings
#define NODE_ID 1

// MCP2515 Registers
#define MCP_RESET       0xC0
#define MCP_WRITE       0x02
#define MCP_READ        0x03
#define MCP_BITMOD      0x05
#define MCP_CANCTRL     0x0F
#define MCP_CNF1        0x2A
#define MCP_CNF2        0x29
#define MCP_CNF3        0x28
#define MCP_RXB0CTRL    0x60
#define MCP_CANINTF     0x2C

#define INC_PER_DEGREE 3356.4444444

// --------------------------------------------------------------------------
// LOW-LEVEL MCP2515 DRIVER
// --------------------------------------------------------------------------

void cs_select() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 0);
    asm volatile("nop \n nop \n nop");
}

void cs_deselect() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 1);
    asm volatile("nop \n nop \n nop");
}

void mcp_reset() {
    cs_select();
    uint8_t cmd = MCP_RESET;
    spi_write_blocking(SPI_PORT, &cmd, 1);
    cs_deselect();
    sleep_ms(10);
}

void mcp_write_reg(uint8_t reg, uint8_t value) {
    cs_select();
    uint8_t data[3] = {MCP_WRITE, reg, value};
    spi_write_blocking(SPI_PORT, data, 3);
    cs_deselect();
}

void mcp_modify_reg(uint8_t reg, uint8_t mask, uint8_t data) {
    cs_select();
    uint8_t buf[4] = {MCP_BITMOD, reg, mask, data};
    spi_write_blocking(SPI_PORT, buf, 4);
    cs_deselect();
}

// Check if a message is available in RX Buffer 0
bool mcp_message_available() {
    cs_select();
    uint8_t cmd = 0xA0; // Read Status command
    spi_write_blocking(SPI_PORT, &cmd, 1);
    uint8_t status;
    spi_read_blocking(SPI_PORT, 0, &status, 1);
    cs_deselect();
    // Bit 0 = Message in RX Buffer 0
    return (status & 0x01); 
}

// Read the CAN Frame from RX Buffer 0
void mcp_read_frame(uint32_t *can_id, uint8_t *data, uint8_t *len) {
    cs_select();
    uint8_t cmd = 0x90; // Read RX Buffer 0
    spi_write_blocking(SPI_PORT, &cmd, 1);
    
    uint8_t header[5];
    spi_read_blocking(SPI_PORT, 0, header, 5);
    
    // Standard ID (11-bit) logic
    *can_id = (header[0] << 3) | (header[1] >> 5);
    *len = header[4] & 0x0F;
    
    // Read Data Payload
    spi_read_blocking(SPI_PORT, 0, data, *len);
    cs_deselect();
    
    // Clear Interrupt Flag (Unlock buffer for next message)
    mcp_modify_reg(MCP_CANINTF, 0x01, 0x00); 
}

void mcp_send_frame(uint16_t can_id, uint8_t *data, uint8_t len) {
    cs_select();
    uint8_t cmd = 0x40; // Load TX Buffer 0
    spi_write_blocking(SPI_PORT, &cmd, 1);
    
    uint8_t header[5];
    header[0] = (can_id >> 3) & 0xFF;
    header[1] = (can_id << 5) & 0xE0;
    header[2] = 0; 
    header[3] = 0; 
    header[4] = len & 0x0F; 
    
    spi_write_blocking(SPI_PORT, header, 5);
    spi_write_blocking(SPI_PORT, data, len);
    cs_deselect();
    
    // Request to Send (RTS) Buffer 0
    cs_select();
    uint8_t rts = 0x81;
    spi_write_blocking(SPI_PORT, &rts, 1);
    cs_deselect();
}

// Initialize for 1Mbit/s (Assuming 8MHz Crystal)
void mcp_init_1mbit_8mhz() {
    mcp_reset();
    mcp_write_reg(MCP_CNF1, 0x00); 
    mcp_write_reg(MCP_CNF2, 0x90); 
    mcp_write_reg(MCP_CNF3, 0x02); 
    mcp_modify_reg(MCP_CANCTRL, 0xE0, 0x00); // Normal Mode
    sleep_ms(10);
}

// --------------------------------------------------------------------------
// EPOS4 FUNCTIONS
// --------------------------------------------------------------------------

// Read 16-bit SDO (Waits for response)
int32_t epos_read_sdo16(uint16_t index, uint8_t subindex) {
    uint8_t request[8] = {0x40, index & 0xFF, (index >> 8) & 0xFF, subindex, 0, 0, 0, 0};
    mcp_send_frame(0x600 + NODE_ID, request, 8);
    
    // Wait for response (Timeout ~100ms)
    for(int i=0; i<100; i++) {
        if(mcp_message_available()) {
            uint32_t rx_id;
            uint8_t rx_data[8];
            uint8_t rx_len;
            mcp_read_frame(&rx_id, rx_data, &rx_len);
            
            if(rx_id == 0x580 + NODE_ID) {
                if(rx_data[0] == 0x80) return -1; // Error
                return rx_data[4] | (rx_data[5] << 8);
            }
        }
        sleep_ms(1);
    }
    return -1; // Timeout
}

void epos_write_sdo16(uint16_t index, uint8_t subindex, int16_t value) {
    uint8_t data[8] = {0x2B, index & 0xFF, (index >> 8) & 0xFF, subindex, value & 0xFF, (value >> 8) & 0xFF, 0, 0};
    mcp_send_frame(0x600 + NODE_ID, data, 8);
    sleep_ms(5);
}

void epos_write_sdo32(uint16_t index, uint8_t subindex, int32_t value) {
    uint8_t data[8];
    data[0] = 0x22;
    data[1] = index & 0xFF;
    data[2] = (index >> 8) & 0xFF;
    data[3] = subindex;
    data[4] = value & 0xFF;
    data[5] = (value >> 8) & 0xFF;
    data[6] = (value >> 16) & 0xFF;
    data[7] = (value >> 24) & 0xFF;
    mcp_send_frame(0x600 + NODE_ID, data, 8);
    sleep_ms(5);
}

void epos_send_nmt(uint8_t command, uint8_t node) {
    uint8_t data[2] = {command, node};
    mcp_send_frame(0x000, data, 2);
    sleep_ms(10);
}

int32_t check_fault_code() {
    return epos_read_sdo16(0x603F, 0x00);
}

// Funkcja wykonująca ruch o zadaną liczbę stopni
// degrees: liczba stopni (np. 90.0, -45.5).
void move_motor_degrees(float degrees) {
    // 1. Przelicz stopnie na inkrementy (rzutowanie na int32)
    int32_t target_inc = (int32_t)(degrees * INC_PER_DEGREE);

    printf("Ruch o %.2f stopni (%d inkrementow)...\n", degrees, target_inc);

    // 2. Sprawdź status (czy silnik jest włączony?)
    int32_t status = epos_read_sdo16(0x6041, 0x00);
    if ((status & 0x0027) != 0x0027) {
        printf("BLAD: Silnik nie jest wlaczony (Status: 0x%04X). Pomijam ruch.\n", status);
        return;
    }

    // 3. Wyślij pozycję docelową do obiektu 0x607A
    epos_write_sdo32(0x607A, 0x00, target_inc);

    // 4. Wyzwól ruch (ControlWord)
    // Bit 4 (0x10) = New Setpoint (musi przejść z 0 na 1)
    // Bit 6 (0x40) = 1 (Ruch RELATYWNY - o zadaną odległość od obecnej)
    // Jeśli wolisz ruch absolutny (do konkretnego kąta), ustaw bit 6 na 0.
    
    // Krok A: Reset bitu New Setpoint (na wszelki wypadek)
    epos_write_sdo16(0x6040, 0x00, 0x000F); 
    sleep_ms(10);

    // Krok B: Ustawienie New Setpoint (0x10) + Relative (0x40) + Enable (0x0F) = 0x005F
    epos_write_sdo16(0x6040, 0x00, 0x005F);

    // 5. Czekaj na zakończenie ruchu (Opcjonalne - blokuje program do końca ruchu)
    printf("Czekam na zakonczenie ruchu...\n");
    sleep_ms(100); // Daj chwilę na rozpoczęcie, żeby bit Target Reached zdążył zgasnąć
    
    int timeout = 0;
    while(timeout < 100) { // Timeout ok. 10 sekund (100 * 100ms)
        status = epos_read_sdo16(0x6041, 0x00);
        
        // Sprawdź błąd (Fault)
        if (status & 0x0008) {
             printf("BLAD SILNIKA w trakcie ruchu! Status: 0x%04X\n", status);
             return;
        }

        // Bit 10 (0x0400) = Target Reached (Cel osiągnięty)
        if (status & 0x0400) {
            printf("Cel osiagniety.\n");
            break;
        }
        
        sleep_ms(100);
        timeout++;
    }
    
    // Po zakończeniu zresetuj bit New Setpoint, żeby być gotowym na kolejny ruch
    epos_write_sdo16(0x6040, 0x00, 0x000F);
}

// --------------------------------------------------------------------------
// MAIN
// --------------------------------------------------------------------------

int main() {
    stdio_init_all();
    sleep_ms(20000); 
    printf("--- EPOS4 Controller Started ---\n");

    // SPI Init
    spi_init(SPI_PORT, 1000 * 1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    // MCP2515 Init
    mcp_init_1mbit_8mhz();
    printf("MCP2515 Initialized.\n");


    // 1. Reset Node & Start Operational
    epos_send_nmt(0x81, NODE_ID); 
    sleep_ms(15000); // Increased delay to prevent 0xFFFFFFFF timeout
    epos_send_nmt(0x01, NODE_ID);
    sleep_ms(1000);

    // 2. Clear Faults
    printf("Clearing Faults...\n");
    epos_write_sdo16(0x6040, 0x00, 0x0080);
    sleep_ms(200);

    // --- NEW: CONFIGURE MOTION PROFILE ---
    // Lower these values if the motor still faults!
    printf("Configuring Profile...\n");
    epos_write_sdo32(0x6081, 0x00, 10000);   // Profile Velocity: 500 RPM (Default is often 25000!)
    epos_write_sdo32(0x6083, 0x00, 500000);  // Profile Acceleration: 1000 RPM/s
    epos_write_sdo32(0x6084, 0x00, 500000);  // Profile Deceleration: 1000 RPM/s
    // -------------------------------------

    // 5. Set Operation Mode (PPM = 1)
    printf("Setting Mode to PPM...\n");
    uint8_t mode_data[8] = {0x2F, 0x60, 0x60, 0x00, 0x01, 0, 0, 0}; // 1 byte write
    mcp_send_frame(0x600 + NODE_ID, mode_data, 8);
    sleep_ms(100);

    // 4. Enable Power Stage
    printf("Enabling...\n");
    epos_write_sdo16(0x6040, 0x00, 0x0006); 
    sleep_ms(50);
    epos_write_sdo16(0x6040, 0x00, 0x0007);
    sleep_ms(50);
    epos_write_sdo16(0x6040, 0x00, 0x000F);
    sleep_ms(200);

    while (true) {
        // Check Status
        int32_t status = epos_read_sdo16(0x6041, 0x00);
        
        // If motor is NOT enabled (Look for Bit 2 "Operation Enabled")
        if ((status & 0x0004) == 0) {
             printf("ERROR: Motor stopped! Status: 0x%04X\n", status);
             
             // Check if it's a Fault (Bit 3)
             if (status & 0x0008) {
                 int32_t errorCode = epos_read_sdo16(0x603F, 0x00);
                 printf("FAULT CODE: 0x%04X\n", errorCode);
                 
                 // Auto-Reset Fault
                 printf("Attempting to Clear Fault...\n");
                 epos_write_sdo16(0x6040, 0x00, 0x0080); // Fault Reset
                 sleep_ms(200);
                 epos_write_sdo16(0x6040, 0x00, 0x0006); // Shutdown
                 epos_write_sdo16(0x6040, 0x00, 0x000F); // Re-Enable
             } else {
                 // Try to re-enable
                 epos_write_sdo16(0x6040, 0x00, 0x000F);
             }
             sleep_ms(1000);
             continue;
        }

        // Obróć o 90 stopni
        move_motor_degrees(90.0);
        sleep_ms(5000);

        // Obróć o 180 stopni w drugą stronę
        move_motor_degrees(-180.0);
        sleep_ms(5000);
        
        // Obróć o 1 stopień (precyzyjnie)
        move_motor_degrees(90.0);
        sleep_ms(5000);
    }
    return 0;
}
