#include "main.h"

// Phase 1: Hardware Verification Tests
void test_led_blink(void);           // Test 1: LED blink test
void test_dac_sine_wave(void);       // Test 2: DAC sine wave test (TIM6 + DMA)
void test_dac_quick(void);           // Test 3: DAC quick test (5 sec)
void test_uart_loopback(void);       // Test 4: UART loopback test (HW test)
void test_uart_echo(void);           // Test 5: UART echo test (RX test)
void test_rdy_pin(void);             // Test 6: RDY pin toggle test
void test_dac_dma_sine(void);        // Test 7: DAC DMA sine wave (buffer fill)

// Legacy test functions
void test_gpio(void);
void test_dac_sine(void);
void init_sine_table(void);

void proc_uart3(void);

// Main application
void init_proc(void);
void run_proc(void);

// Slave mode (main application)
void run_slave_mode(void);

// Test menu
void show_test_menu(void);
void run_test_menu(void);



