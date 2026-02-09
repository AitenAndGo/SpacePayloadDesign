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
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/adc.h" // Added ADC support

// --------------------------------------------------------------------------
// HARDWARE CONFIGURATION
// --------------------------------------------------------------------------
#define SPI_PORT spi0
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7

// Sensor Pins (Based on image_79949e.png)
#define PIN_UV_SENSOR 26 // ADC0
#define PIN_IR_SENSOR 27 // ADC1

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
// GLOBALS
// --------------------------------------------------------------------------
volatile int command = CMD_NONE; // Init to NONE
volatile int param1  = 0;
volatile int param2  = 0;

// Filter Positions (calibrated encoder counts)
static int32_t filter_positions[4] = {0, 0, 0, 0};
static bool is_calibrated = false;

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

// --- MOTOR CONTROL HELPERS ---

void wait_for_target_reached() {
    int timeout = 0;
    while(timeout < 100) { // ~10 seconds
        int32_t status = epos_read_sdo16(0x6041, 0x00);
        if (status & 0x0008) { printf("FAULT during move!\n"); return; }
        if (status & 0x0400) { break; } // Target reached
        sleep_ms(100);
        timeout++;
    }
}

// Move Relative (degrees)
void move_motor_degrees(float degrees) {
    int32_t target_inc = (int32_t)(degrees * INC_PER_DEGREE);
    printf("REL Move: %.2f deg (%d inc)\n", degrees, target_inc);
    
    epos_write_sdo32(0x607A, 0x00, target_inc); // Target Position
    
    // Relative (Bit 6=1), New Setpoint (Bit 4=1)
    epos_write_sdo16(0x6040, 0x00, 0x000F); // Reset control
    sleep_ms(10);
    epos_write_sdo16(0x6040, 0x00, 0x005F); // Execute
    
    wait_for_target_reached();
    epos_write_sdo16(0x6040, 0x00, 0x000F); // Clear New Setpoint
}

// Move Absolute (encoder counts) - NEW
void move_motor_absolute(int32_t position) {
    printf("ABS Move to: %d\n", position);
    
    epos_write_sdo32(0x607A, 0x00, position); // Target Position
    
    // Absolute (Bit 6=0), New Setpoint (Bit 4=1)
    epos_write_sdo16(0x6040, 0x00, 0x000F); 
    sleep_ms(10);
    epos_write_sdo16(0x6040, 0x00, 0x001F); // Execute (Note 0x1F not 0x5F)
    
    wait_for_target_reached();
    epos_write_sdo16(0x6040, 0x00, 0x000F); 
}

// --------------------------------------------------------------------------
// SENSOR DRIVER
// --------------------------------------------------------------------------

void init_sensors() {
    adc_init();
    adc_gpio_init(PIN_UV_SENSOR);
    adc_gpio_init(PIN_IR_SENSOR);
    printf("Sensors Initialized (UV=26, IR=27)\n");
}

uint16_t read_sensor(uint8_t id) {
    // id 0 = IR, id 1 = UV
    if (id == 0) {
        adc_select_input(1); // GPIO 27 is ADC1
    } else {
        adc_select_input(0); // GPIO 26 is ADC0
    }
    return adc_read();
}

// --------------------------------------------------------------------------
// STATE MACHINE
// --------------------------------------------------------------------------
static app_state_t current_state = STATE_OFF;
static app_state_t requested_transition = STATE_OFF;
static bool transition_requested;
static absolute_time_t boot_done_at;

static bool is_valid_transition(app_state_t from, app_state_t to) {
    if (from == STATE_OFF && to == STATE_BOOT) return true;
    if (from == STATE_BOOT && to == STATE_SAFE) return true;
    if (from == STATE_SAFE && to == STATE_CALIBRATION) return true; // SAFE -> CALIB
    if (from == STATE_CALIBRATION && to == STATE_NOMINAL) return true; // CALIB -> NOMINAL
    if (from == STATE_NOMINAL && to == STATE_SAFE) return true; // For safety/reset
    return false; 
    // Simplified for this snippet
}

void transition_to(app_state_t new_state) {
    if (!is_valid_transition(current_state, new_state)) return;
    current_state = new_state;
    transition_requested = false;
    requested_transition = current_state;
    if (current_state == STATE_BOOT) boot_done_at = make_timeout_time_ms(2000);
}

// --------------------------------------------------------------------------
// STATE HANDLERS
// --------------------------------------------------------------------------

void state_off_handler(void) { maxon_disable(); }
void state_boot_handler(void) { maxon_disable(); }

void state_safe_handler(void) {
    printf("State SAFE. Initializing hardware...\n");
    
    // Ensure motor is enabled and ready
    int32_t status = epos_read_sdo16(0x6041, 0x00);
    if ((status & 0x0004) == 0) {
        printf("Enabling Motor...\n");
        epos_write_sdo16(0x6040, 0x00, 0x0006);
        epos_write_sdo16(0x6040, 0x00, 0x000F);
        sleep_ms(500);
    }
    
    // Prepare for calibration
    printf("Transitioning to CALIBRATION...\n");
    transition_to(STATE_CALIBRATION);
}

void state_calibration_handler(void) {
    printf("State CALIBRATION. Starting 360 scan...\n");

    // "Change from uint8 so values above 256 could be used" -> using int loop
    int found_peaks = 0;
    
    // We will scan in 5-degree increments to find the rough area of the windows
    // In a real app, you might want finer resolution or velocity mode + polling
    for (int i = 0; i < 72; i++) { 
        move_motor_degrees(5.0); // Move 5 degrees
        sleep_ms(50); // Wait for settle
        
        uint16_t ir_val = read_sensor(0); // Read IR (id 0)
        
        // Simple Threshold Logic for "Window" detection
        // Assuming 'Window' allows IR light through -> High Value
        // Adjust 2000 to your actual threshold
        if (ir_val > 2000) { 
            // We found a high signal. Is this a new peak?
            // Simple logic: If we haven't found 4 yet, store this position.
            // In reality, you'd want to find the CENTER of the high signal.
            
            // Debounce: check if this is far enough from the last one
            int32_t current_pos = epos_read_sdo16(0x6064, 0x00); // Read actual position
            
            bool new_peak = true;
            for(int k=0; k<found_peaks; k++) {
                if (abs(current_pos - filter_positions[k]) < (20.0 * INC_PER_DEGREE)) {
                    new_peak = false; // Too close to previous
                    break;
                }
            }
            
            if (new_peak && found_peaks < 4) {
                printf("Window %d found at pos %d (IR: %d)\n", found_peaks, current_pos, ir_val);
                filter_positions[found_peaks] = current_pos;
                found_peaks++;
            }
        }
    }
    
    if (found_peaks == 4) {
        printf("Calibration Complete. 4 Positions stored.\n");
        is_calibrated = true;
        transition_to(STATE_NOMINAL);
    } else {
        printf("Calibration Failed (Found %d/4). Retrying...\n", found_peaks);
        // Depending on logic, retry or stay here. 
        // For now, force transition to Nominal to allow testing commands
        transition_to(STATE_NOMINAL); 
    }
}

void state_nominal_handler(void) {
    // Wait for TCP commands
    if (command != CMD_NONE) {
        
        // 1. MOVE COMMAND (0b0000)
        if (command == 0) {
            uint8_t wheel_num = param1;
            uint8_t pos_idx = param2;
            
            printf("CMD: MOVE Wheel %d to Idx %d\n", wheel_num, pos_idx);
            
            uint8_t status_code = 1; // Default error
            
            if (wheel_num == 0 && pos_idx < 4) {
                // Execute Move
                move_motor_absolute(filter_positions[pos_idx]);
                status_code = 0; // OK
            } else {
                printf("Invalid Params\n");
            }
            
            // Send Telemetry response (using random ID 0 for generic ack)
            send_telemetry(server_pcb, status_code, pos_idx, to_ms_since_boot(get_absolute_time()));
        }
        
        // 2. READ COMMAND (0b0001)
        else if (command == 1) {
            uint8_t sensor_id = param1; // 0=IR, 1=UV
            // param2 unused
            
            printf("CMD: READ Sensor %d\n", sensor_id);
            
            uint16_t val = 0;
            uint8_t status_code = 0;
            
            if (sensor_id == 0 || sensor_id == 1) {
                val = read_sensor(sensor_id);
            } else {
                status_code = 1; // Error
            }
            
            send_telemetry(server_pcb, status_code, val, to_ms_since_boot(get_absolute_time()));
        }
        
        // Reset command
        command = CMD_NONE;
    }
    sleep_ms(10);
}

// --------------------------------------------------------------------------
// MAIN
// --------------------------------------------------------------------------

void system_init(void) {
    stdio_init_all();
    sleep_ms(20000);
    
    init_sensors(); // Init ADC
    
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return;
    }
    cyw43_arch_enable_ap_mode("PICO_AP", "1234567890", CYW43_AUTH_WPA2_AES_PSK);
    start_tcp_server();
    
    // Init SPI/CAN/EPOS
    spi_init(SPI_PORT, 1000 * 1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_CS); gpio_set_dir(PIN_CS, GPIO_OUT); gpio_put(PIN_CS, 1);
    
    mcp_init_1mbit_8mhz();
    
    // EPOS Init Sequence (Simplified)
    epos_send_nmt(0x81, NODE_ID); sleep_ms(100);
    epos_send_nmt(0x01, NODE_ID); sleep_ms(100);
    epos_write_sdo16(0x6040, 0x00, 0x0080); // Fault reset
    sleep_ms(100);

    printf("Configuring Motion Profile...\n");
    epos_write_sdo32(0x6081, 0x00, 10000);   // Profile Velocity: 500 RPM
    epos_write_sdo32(0x6083, 0x00, 500000);  // Profile Acceleration
    epos_write_sdo32(0x6084, 0x00, 500000);  // Profile Deceleration

    // Set Profile Velocity Mode (PPM)
    uint8_t mode_data[8] = {0x2F, 0x60, 0x60, 0x00, 0x01, 0, 0, 0}; 
    mcp_send_frame(0x600 + NODE_ID, mode_data, 8);
    
    printf("System Init Done.\n");
}

void check_autonomous_events(void) {
    if (current_state == STATE_BOOT) {
        if (absolute_time_diff_us(get_absolute_time(), boot_done_at) <= 0) {
            transition_to(STATE_SAFE);
            transition_requested = true;
        }
    }
}

void check_commands(void) {
    // Stub
}

// Stubs for other states
void state_maintenance_handler(void) { }
void state_configuration_handler(void) { }

int main(void) {
    system_init();
    transition_to(STATE_BOOT);
    
    while (true) {
        check_autonomous_events();
        if (transition_requested) transition_to(requested_transition);

        switch (current_state) {
            case STATE_OFF: state_off_handler(); break;
            case STATE_BOOT: state_boot_handler(); break;
            case STATE_SAFE: state_safe_handler(); break;
            case STATE_CALIBRATION: state_calibration_handler(); break;
            case STATE_NOMINAL: state_nominal_handler(); break;
            default: break;
        }
    }
}