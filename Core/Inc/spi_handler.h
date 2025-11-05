/**
  ******************************************************************************
  * @file           : spi_handler.h
  * @brief          : SPI Packet Reception and Processing
  * @details        : State machine for receiving command and data packets
  *                   Processes packets and controls audio channels
  ******************************************************************************
  * @attention
  *
  * SPI Reception Flow:
  * 1. Wait for header byte (0xC0 or 0xDA)
  * 2. Receive remaining packet bytes
  * 3. Process command or data
  * 4. Return to step 1
  *
  * RDY Pin Control:
  * - HIGH: Ready to receive
  * - LOW: Processing packet
  *
  ******************************************************************************
  */

#ifndef __SPI_HANDLER_H
#define __SPI_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "spi_protocol.h"
#include "audio_channel.h"

/* ============================================================================ */
/* SPI Reception State Machine */
/* ============================================================================ */

/**
 * @brief SPI Reception States
 */
typedef enum {
    SPI_STATE_WAIT_HEADER,          // Waiting for packet header (0xC0 or 0xDA)
    SPI_STATE_RECEIVE_CMD,          // Receiving command packet (5 more bytes)
    SPI_STATE_RECEIVE_DATA_HEADER,  // Receiving data header (4 more bytes)
    SPI_STATE_RECEIVE_DATA_SAMPLES, // Receiving sample data
    SPI_STATE_PROCESS_PACKET        // Processing received packet
} SPI_RxState_t;

/**
 * @brief Error Statistics
 */
typedef struct {
    uint32_t spi_error_count;       // SPI hardware errors
    uint32_t invalid_header_count;  // Invalid packet headers
    uint32_t invalid_id_count;      // Packets for wrong slave ID
    uint32_t overflow_count;        // Buffer overflow errors
    uint32_t dma_rx_complete_count; // DMA RX complete callbacks
} SPI_ErrorStats_t;

/* ============================================================================ */
/* Function Prototypes */
/* ============================================================================ */

/**
 * @brief Initialize SPI handler
 * @param hspi SPI handle
 * @param dac1_ch Pointer to DAC1 audio channel
 * @param dac2_ch Pointer to DAC2 audio channel
 */
void spi_handler_init(SPI_HandleTypeDef *hspi,
                      AudioChannel_t *dac1_ch,
                      AudioChannel_t *dac2_ch);

/**
 * @brief Start SPI reception
 * @note Call this after initialization to begin receiving packets
 */
void spi_handler_start(void);

/**
 * @brief SPI RX complete callback (called from HAL_SPI_RxCpltCallback)
 * @param hspi SPI handle
 * @note This function implements the state machine for packet reception
 */
void spi_handler_rx_callback(SPI_HandleTypeDef *hspi);

/**
 * @brief SPI error callback (called from HAL_SPI_ErrorCallback)
 * @param hspi SPI handle
 */
void spi_handler_error_callback(SPI_HandleTypeDef *hspi);

/**
 * @brief Get current reception state
 * @return Current SPI_RxState_t
 */
SPI_RxState_t spi_handler_get_state(void);

/**
 * @brief Get error statistics
 * @param stats Output: Error statistics structure
 */
void spi_handler_get_errors(SPI_ErrorStats_t *stats);

/**
 * @brief Reset error counters
 */
void spi_handler_reset_errors(void);

/**
 * @brief Set RDY pin state
 * @param ready 1=HIGH (ready), 0=LOW (busy)
 */
void spi_handler_set_ready(uint8_t ready);

/**
 * @brief CS pin falling edge handler
 * @note Called from EXTI interrupt when CS pin detects falling edge
 *       Starts SPI DMA reception
 */
void spi_handler_cs_falling(void);

/**
 * @brief CS pin rising edge handler
 * @note Called from EXTI interrupt when CS pin detects rising edge
 *       Checks DMA completion and aborts if incomplete
 */
void spi_handler_cs_rising(void);

/**
 * @brief Get last received packet (for debugging)
 * @param buffer Output buffer (must be at least 5 bytes)
 * @return 1 if valid packet available, 0 otherwise
 */
uint8_t spi_handler_get_last_packet(uint8_t *buffer);

/**
 * @brief Get address of SPI RX DMA buffer (for debugging)
 * @return Address of g_rx_cmd_packet
 */
uint32_t spi_handler_get_rx_buffer_addr(void);

#ifdef __cplusplus
}
#endif

#endif /* __SPI_HANDLER_H */
