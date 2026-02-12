/*
 * Optical payload application – state machine (OFF / BOOT / SAFE / CALIBRATION / NOMINAL).
 * Integrates working Maxon driver with new ADC/Sensor logic.
 */

#include "app.h"
#include "time.h"
#include "maxon.h"
#include "tcp_server.h"
#include "pico/cyw43_arch.h"
#include "pico/stdio.h"
#include "pico/time.h"
#include <cyw43.h>
#include <cyw43_country.h>
#include <stdio.h>
#include <stdlib.h> // abs()
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/adc.h" // Obsługa ADC (czujniki)

// --------------------------------------------------------------------------
// HARDWARE CONFIGURATION
// --------------------------------------------------------------------------
#define SPI_PORT spi0
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7

// Piny czujników (zgodnie ze schematem)
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

// Define max samples for the buffer
#define MAX_SAMPLES 2000 

// Structure for raw data
typedef struct {
    int32_t position;
    uint16_t ir_value;
} ScanPoint_t;

// Zmienne globalne komend TCP
volatile int command = -1; // -1 oznacza brak nowej komendy
volatile int param1  = 0;
volatile int param2  = 0;

// Tablica na pozycje filtrów (kalibracja)
static int32_t filter_positions[4] = {0, 0, 0, 0};
static bool is_calibrated = false;

// --------------------------------------------------------------------------
// LOW-LEVEL MCP2515 DRIVER (Bez zmian - tak jak w działającym pliku)
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

bool mcp_message_available() {
    cs_select();
    uint8_t cmd = 0xA0; 
    spi_write_blocking(SPI_PORT, &cmd, 1);
    uint8_t status;
    spi_read_blocking(SPI_PORT, 0, &status, 1);
    cs_deselect();
    return (status & 0x01); 
}

void mcp_read_frame(uint32_t *can_id, uint8_t *data, uint8_t *len) {
    cs_select();
    uint8_t cmd = 0x90; 
    spi_write_blocking(SPI_PORT, &cmd, 1);
    
    uint8_t header[5];
    spi_read_blocking(SPI_PORT, 0, header, 5);
    
    *can_id = (header[0] << 3) | (header[1] >> 5);
    *len = header[4] & 0x0F;
    
    spi_read_blocking(SPI_PORT, 0, data, *len);
    cs_deselect();
    
    mcp_modify_reg(MCP_CANINTF, 0x01, 0x00); 
}

void mcp_send_frame(uint16_t can_id, uint8_t *data, uint8_t len) {
    cs_select();
    uint8_t cmd = 0x40; 
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
    
    cs_select();
    uint8_t rts = 0x81;
    spi_write_blocking(SPI_PORT, &rts, 1);
    cs_deselect();
}

void mcp_init_1mbit_8mhz() {
    mcp_reset();
    mcp_write_reg(MCP_CNF1, 0x00); 
    mcp_write_reg(MCP_CNF2, 0x90); 
    mcp_write_reg(MCP_CNF3, 0x02); 
    mcp_modify_reg(MCP_CANCTRL, 0xE0, 0x00); 
    sleep_ms(10);
}

// --------------------------------------------------------------------------
// EPOS4 FUNCTIONS (Oryginalne)
// --------------------------------------------------------------------------

int32_t epos_read_sdo16(uint16_t index, uint8_t subindex) {
    uint8_t request[8] = {0x40, index & 0xFF, (index >> 8) & 0xFF, subindex, 0, 0, 0, 0};
    mcp_send_frame(0x600 + NODE_ID, request, 8);
    
    for(int i=0; i<100; i++) {
        if(mcp_message_available()) {
            uint32_t rx_id;
            uint8_t rx_data[8];
            uint8_t rx_len;
            mcp_read_frame(&rx_id, rx_data, &rx_len);
            
            if(rx_id == 0x580 + NODE_ID) {
                if(rx_data[0] == 0x80) return -1; 
                return rx_data[4] | (rx_data[5] << 8);
            }
        }
        sleep_ms(1);
    }
    return -1; 
}

// Dodana funkcja do czytania pozycji (32-bit) potrzebna do kalibracji
int32_t epos_read_sdo32(uint16_t index, uint8_t subindex) {
    uint8_t request[8] = {0x40, index & 0xFF, (index >> 8) & 0xFF, subindex, 0, 0, 0, 0};
    mcp_send_frame(0x600 + NODE_ID, request, 8);
    
    for(int i=0; i<100; i++) {
        if(mcp_message_available()) {
            uint32_t rx_id;
            uint8_t rx_data[8];
            uint8_t rx_len;
            mcp_read_frame(&rx_id, rx_data, &rx_len);
            
            if(rx_id == 0x580 + NODE_ID) {
                return rx_data[4] | (rx_data[5] << 8) | (rx_data[6] << 16) | (rx_data[7] << 24);
            }
        }
        sleep_ms(1);
    }
    return -1; 
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

// --------------------------------------------------------------------------
// MOTOR MOVEMENT (Przywrócone i rozszerzone)
// --------------------------------------------------------------------------

// Twoja oryginalna funkcja ruchu względnego
void move_motor_degrees(float degrees) {
    int32_t target_inc = (int32_t)(degrees * INC_PER_DEGREE);
    printf("Ruch o %.2f stopni (%d inkrementow)...\n", degrees, target_inc);

    int32_t status = epos_read_sdo16(0x6041, 0x00);
    // Sprawdzenie czy włączony (Bit 2)
    if ((status & 0x0027) != 0x0027) {
        printf("BLAD: Silnik nie jest wlaczony (Status: 0x%04X). Pomijam ruch.\n", status);
        return;
    }

    epos_write_sdo32(0x607A, 0x00, target_inc);

    // Reset bitu New Setpoint
    epos_write_sdo16(0x6040, 0x00, 0x000F); 
    sleep_ms(10);

    // New Setpoint (0x10) + Relative (0x40) + Enable (0x0F) = 0x005F
    epos_write_sdo16(0x6040, 0x00, 0x005F);

    // Czekanie na zakończenie
    sleep_ms(100); 
    int timeout = 0;
    while(timeout < 100) { 
        status = epos_read_sdo16(0x6041, 0x00);
        if (status & 0x0008) {
             printf("BLAD SILNIKA w trakcie ruchu! Status: 0x%04X\n", status);
             return;
        }
        if (status & 0x0400) { // Target Reached
            break;
        }
        sleep_ms(100);
        timeout++;
    }
    epos_write_sdo16(0x6040, 0x00, 0x000F);
}

// Nowa funkcja ruchu absolutnego (do pozycji z kalibracji)
void move_motor_absolute(int32_t position_inc) {
    printf("Ruch absolutny do pozycji: %d...\n", position_inc);

    int32_t status = epos_read_sdo16(0x6041, 0x00);
    if ((status & 0x0027) != 0x0027) {
        printf("BLAD: Silnik nie gotowy.\n");
        return;
    }

    epos_write_sdo32(0x607A, 0x00, position_inc);

    epos_write_sdo16(0x6040, 0x00, 0x000F); 
    sleep_ms(10);

    // New Setpoint (0x10) + ABSOLUTE (Bit 6 = 0) + Enable (0x0F) = 0x001F
    epos_write_sdo16(0x6040, 0x00, 0x001F);

    sleep_ms(100);
    int timeout = 0;
    while(timeout < 100) {
        status = epos_read_sdo16(0x6041, 0x00);
        if (status & 0x0400) break;
        sleep_ms(100);
        timeout++;
    }
    epos_write_sdo16(0x6040, 0x00, 0x000F);
}

// --------------------------------------------------------------------------
// SENSORS (ADC)
// --------------------------------------------------------------------------

void init_sensors() {
    adc_init();
    adc_gpio_init(PIN_UV_SENSOR);
    adc_gpio_init(PIN_IR_SENSOR);
    printf("Sensors Initialized (UV=26, IR=27)\n");
}

uint16_t read_sensor_calibration(uint8_t id) {
    // id 0 = IR (27), id 1 = UV (26)
    if (id == 0) {
        adc_select_input(1); 
    } else {
        adc_select_input(0); 
    }
    return adc_read();
}

uint16_t read_sensor(uint8_t id) {
    // Wybór kanału (bez zmian)
    if (id == 0) {
        adc_select_input(1); // IR (GPIO 27)
    } else {
        adc_select_input(0); // UV (GPIO 26)
    }

    // --- SZUKANIE WARTOŚCI MINIMALNEJ (Peak Detection - Active Low) ---
    // Startujemy od maksymalnej możliwej wartości (4095 dla 12-bit ADC)
    uint16_t min_val = 4095; 
    
    // Zwiększyłem lekko czas próbkowania, żeby na pewno trafić w "mignięcie" pilota
    const int samples = 100000;

    for (int i = 0; i < samples; i++) {
        uint16_t current_val = adc_read();

        // Jeśli znaleźliśmy wartość niższą (silniejsze światło), zapamiętujemy ją
        if (current_val < min_val) {
            min_val = current_val;
        }

        // Krótkie opóźnienie, żeby rozłożyć pomiar w czasies
        sleep_us(10);
    }

    // Zwracamy najniższą napotkaną wartość (najsilniejszy sygnał w tym oknie czasu)
    return min_val;
}

// --------------------------------------------------------------------------
// INITIALIZATION (Przywrócona funkcja init_maxon)
// --------------------------------------------------------------------------

void init_maxon() {
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

    // 1. Reset Node
    epos_send_nmt(0x81, NODE_ID); 
    sleep_ms(5000); 
    epos_send_nmt(0x01, NODE_ID);
    sleep_ms(1000);

    // 2. Clear Faults
    printf("Clearing Faults...\n");
    epos_write_sdo16(0x6040, 0x00, 0x0080);
    sleep_ms(200);

    // 3. Configure Motion Profile (TO BYŁO KLUCZOWE)
    printf("Configuring Profile...\n");
    epos_write_sdo32(0x6081, 0x00, 5000);   // Velocity
    epos_write_sdo32(0x6083, 0x00, 50000);  // Accel
    epos_write_sdo32(0x6084, 0x00, 50000);  // Decel

    // 4. Set Operation Mode (PPM = 1)
    printf("Setting Mode to PPM...\n");
    uint8_t mode_data[8] = {0x2F, 0x60, 0x60, 0x00, 0x01, 0, 0, 0}; 
    mcp_send_frame(0x600 + NODE_ID, mode_data, 8);
    sleep_ms(100);

    // 5. Enable Power Stage (Sekwencja włączania)
    printf("Enabling...\n");
    epos_write_sdo16(0x6040, 0x00, 0x0006); // Shutdown
    sleep_ms(50);
    epos_write_sdo16(0x6040, 0x00, 0x0007); // Switch On
    sleep_ms(50);
    epos_write_sdo16(0x6040, 0x00, 0x000F); // Enable Operation
    sleep_ms(200);

    // Weryfikacja
    int32_t status = epos_read_sdo16(0x6041, 0x00);
    if ((status & 0x0004)) {
        printf("Silnik WLACZONY (Status: 0x%04X)\n", status);
    } else {
        printf("BLAD: Silnik nie wlaczyl sie poprawnie! (Status: 0x%04X)\n", status);
    }
}

// --------------------------------------------------------------------------
// STATE MACHINE
// --------------------------------------------------------------------------
static app_state_t current_state = STATE_OFF;
static app_state_t requested_transition = STATE_OFF;
static bool transition_requested;
static absolute_time_t boot_done_at;

void transition_to(app_state_t new_state) {
    current_state = new_state;
    transition_requested = false;
    requested_transition = current_state;
    if (current_state == STATE_BOOT)
        boot_done_at = make_timeout_time_ms(2000);
}

// --------------------------------------------------------------------------
// STATE HANDLERS
// --------------------------------------------------------------------------

void state_off_handler(void) { maxon_disable(); }
void state_boot_handler(void) { maxon_disable(); }

void state_safe_handler(void) {
    printf("State SAFE. Checking motor...\n");
    // Tutaj ewentualnie można sprawdzić status ponownie
    transition_to(STATE_CALIBRATION);
}

void state_calibration_handler(void) {
    printf("State CALIBRATION. Starting continuous 360 scan with 90deg logic...\n");

    // Static buffer to store the scan data
    static ScanPoint_t scan_data[MAX_SAMPLES];
    int sample_count = 0;

    // Reset positions
    for(int i=0; i<4; i++) filter_positions[i] = 0;

    // --- PHASE 1: ACQUISITION (Spin & Record) ---
    
    // Check motor status
    int32_t status = epos_read_sdo16(0x6041, 0x00);
    if ((status & 0x0027) != 0x0027) {
        printf("Error: Motor disabled. Go SAFE.\n");
        transition_to(STATE_SAFE);
        return;
    }

    // Move 360 degrees (Relative)
    int32_t target_inc = (int32_t)(360.0 * INC_PER_DEGREE);
    
    printf(">> Moving 360 deg and recording...\n");
    epos_write_sdo32(0x607A, 0x00, target_inc);
    epos_write_sdo16(0x6040, 0x00, 0x000F); 
    sleep_ms(10);
    epos_write_sdo16(0x6040, 0x00, 0x005F); // Start Motion

    // Polling loop
    absolute_time_t timeout = make_timeout_time_ms(12000); // 12s timeout
    while (sample_count < MAX_SAMPLES) {
        status = epos_read_sdo16(0x6041, 0x00);
        
        // Read Pos & IR
        scan_data[sample_count].position = epos_read_sdo32(0x6064, 0x00);
        scan_data[sample_count].ir_value = read_sensor_calibration(0);
        sample_count++;

        if (status & 0x0400) break; // Target reached
        if (absolute_time_diff_us(get_absolute_time(), timeout) < 0) break;
        
        sleep_ms(5); // Fast sampling (~200Hz)
    }
    // Stop & Clean flags
    epos_write_sdo16(0x6040, 0x00, 0x000F);

    // --- PHASE 2: ANALYSIS (Find 4 distinct peaks) ---
    printf(">> Analyzing %d samples...\n", sample_count);

    int found_peaks = 0;
    
    // Helper to store the 'strength' of the peaks to compare them
    uint16_t peak_min_values[4] = {4095, 4095, 4095, 4095};

    bool inside_window = false;
    uint16_t win_min_val = 4095;
    int32_t win_best_pos = 0;
    
    // Threshold: < 2000 means "Light Detected" (Window)
    const uint16_t THRESHOLD = 2000;
    
    // Minimum distance between windows to be considered distinct
    // 60 degrees * increments_per_degree
    const int32_t MIN_DIST_INC = (int32_t)(45.0 * INC_PER_DEGREE);

    for (int i = 0; i < sample_count; i++) {
        uint16_t val = scan_data[i].ir_value;
        int32_t pos = scan_data[i].position;

        if (val < THRESHOLD) {
            // INSIDE WINDOW
            if (!inside_window) {
                inside_window = true;
                win_min_val = val;
                win_best_pos = pos;
            } else {
                // Tracking the lowest value (strongest light) in this window
                if (val < win_min_val) {
                    win_min_val = val;
                    win_best_pos = pos;
                    printf("sensor values: %.2d ", val);
                }
            }
        } else {
            // OUTSIDE WINDOW (Signal went High / Dark)
            if (inside_window) {
                // We just finished passing a window. Now analyze it.
                inside_window = false;

                bool is_distinct = true;

                // CHECK: Is this window close to the previous one?
                if (found_peaks > 0 && found_peaks < 4) {
                     int32_t dist = abs(win_best_pos - filter_positions[found_peaks - 1]);
                     
                     if (dist < MIN_DIST_INC) {
                         // It is TOO CLOSE to be a new window (< 60 deg).
                         // It might be noise or the same window split in two.
                         is_distinct = false;
                         
                         // Logic: Keep the one with the STRONGER signal (lower value)
                         if (win_min_val < peak_min_values[found_peaks - 1]) {
                             printf("   -> Updating Window #%d (Better signal found at %d)\n", found_peaks-1, win_best_pos);
                             filter_positions[found_peaks - 1] = win_best_pos;
                             peak_min_values[found_peaks - 1] = win_min_val;
                         } else {
                             printf("   -> Ignoring noise at %d (We have better signal already)\n", win_best_pos);
                         }
                     }
                }

                // If distinct and we have space, add it
                if (is_distinct && found_peaks < 4) {
                    filter_positions[found_peaks] = win_best_pos;
                    peak_min_values[found_peaks] = win_min_val;
                    printf("  -> FOUND VALID WINDOW #%d at %d (Val: %d)\n", found_peaks, win_best_pos, win_min_val);
                    found_peaks++;
                }
            }
        }
    }

    if (found_peaks == 4) {
        printf("Calibration SUCCESS. 4 positions saved.\n");
        is_calibrated = true;
        transition_to(STATE_NOMINAL);
    } else {
        printf("Calibration FAILED. Found %d/4 windows.\n", found_peaks);
        transition_to(STATE_NOMINAL);
    }
}

void state_nominal_handler(void) {
    if (command != -1) {
        
        // 1. MOVE (0)
        if (command == 0) {
            uint8_t wheel_num = param1;
            uint8_t pos_idx = param2;
            printf("CMD: MOVE Wheel %d to Idx %d\n", wheel_num, pos_idx);
            
            uint8_t status_code = 1;
            if (wheel_num == 0 && pos_idx < 4) {
                move_motor_absolute(filter_positions[pos_idx]);
                status_code = 0;
            }
            send_telemetry(status_code, pos_idx, to_ms_since_boot(get_absolute_time()));
        }
        
        // 2. READ (1)
        else if (command == 1) {
            uint8_t sensor_id = param1; // 0=IR, 1=UV
            printf("CMD: READ Sensor %d\n", sensor_id);
            
            uint16_t val = 0;
            uint8_t status_code = 0;
            if (sensor_id <= 1) val = read_sensor(sensor_id);
            else status_code = 1;
            
            send_telemetry(status_code, val, to_ms_since_boot(get_absolute_time()));
        }

        // 3. CONTROL (0)
        if (command == 2) {
            uint8_t wheel_num = param1;
            uint8_t degrees = param2;
            printf("CMD: MOVE Wheel %d about %d degrees\n", wheel_num, degrees);
            
            uint8_t status_code = 1;
            if (wheel_num == 0) {
                move_motor_degrees(degrees);
                status_code = 0;
            }
            send_telemetry(status_code, degrees, to_ms_since_boot(get_absolute_time()));
        }
        
        command = -1; // Reset komendy
    }
    sleep_ms(10);
}

// --------------------------------------------------------------------------
// MAIN
// --------------------------------------------------------------------------

void system_init(void) {
    stdio_init_all();
    sleep_ms(2000);
    printf("--- SYSTEM START ---\n");

    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return;
    }
    cyw43_arch_enable_ap_mode("PICO_AP", "1234567890", CYW43_AUTH_WPA2_AES_PSK);
    start_tcp_server();

    init_sensors();
    
    // TUTAJ wywołujemy pełną inicjalizację silnika z Twojego starego pliku
    init_maxon();
}

void check_autonomous_events(void) {
    if (current_state == STATE_BOOT) {
        if (absolute_time_diff_us(get_absolute_time(), boot_done_at) <= 0) {
            transition_to(STATE_SAFE);
            transition_requested = true;
        }
    }
}

// Stubs
void check_commands(void) { }
void state_maintenance_handler(void) { }
void state_configuration_handler(void) { }

int main(void) {
    system_init();
    transition_to(STATE_BOOT);
    boot_done_at = make_timeout_time_ms(2000);

    while (true) {
        check_autonomous_events();
        if (transition_requested)
            transition_to(requested_transition);

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