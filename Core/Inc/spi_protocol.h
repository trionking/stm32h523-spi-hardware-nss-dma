/**
  ******************************************************************************
  * @file           : spi_protocol.h
  * @brief          : SPI Protocol definitions for Slave MCU
  * @details        : Packet structures and protocol constants for Master-Slave
  *                   SPI communication in audio streaming system
  ******************************************************************************
  * @attention
  *
  * SPI Protocol Specification v2.0
  * - Command Packet: 5 bytes (0xC0 header) - slave_id removed
  * - Data Packet: 4 bytes header + N*2 bytes samples (max 2048 samples)
  * - Handshake: RDY pin control
  * - Hardware CS pin selects slave (no software slave_id needed)
  *
  ******************************************************************************
  */

#ifndef __SPI_PROTOCOL_H
#define __SPI_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ============================================================================ */
/* Configuration */
/* ============================================================================ */

/**
 * @brief Slave ID Configuration (DEPRECATED in v2.0)
 * @note  Hardware CS pin now selects slave - no software ID needed
 *        This definition kept for backward compatibility only
 */
// #define MY_SLAVE_ID             0  // Deprecated - hardware CS selects slave

/**
 * @brief Audio buffer size (samples per buffer)
 * @note  2048 samples @ 32kHz = 64ms playback time
 */
#define AUDIO_BUFFER_SIZE       2048

/**
 * @brief Maximum samples per data packet
 */
#define MAX_SAMPLES_PER_PACKET  2048

/* ============================================================================ */
/* Protocol Constants */
/* ============================================================================ */

/**
 * @brief Packet header identifiers
 */
#define HEADER_CMD              0xC0    // Command packet header
#define HEADER_DATA             0xDA    // Data packet header

/**
 * @brief Command codes
 */
#define CMD_PLAY                0x01    // Start playback
#define CMD_STOP                0x02    // Stop playback
#define CMD_VOLUME              0x03    // Set volume (0-100)
#define CMD_RESET               0xFF    // Reset channel

/**
 * @brief DAC channel identifiers
 */
#define CHANNEL_DAC1            0       // DAC1_CH1
#define CHANNEL_DAC2            1       // DAC1_CH2

/* ============================================================================ */
/* Packet Structures */
/* ============================================================================ */

/**
 * @brief Command Packet Structure (5 bytes) - Protocol v2.0
 *
 * Byte Layout:
 * [0] header    : 0xC0
 * [1] channel   : 0=DAC1, 1=DAC2
 * [2] command   : CMD_PLAY, CMD_STOP, CMD_VOLUME, CMD_RESET
 * [3] param_h   : Parameter high byte
 * [4] param_l   : Parameter low byte
 *
 * NOTE: slave_id removed - hardware CS pin selects slave
 */
typedef struct __attribute__((packed)) {
    uint8_t header;         // 0xC0
    uint8_t channel;        // 0=DAC1, 1=DAC2
    uint8_t command;        // Command code
    uint8_t param_h;        // Parameter high byte
    uint8_t param_l;        // Parameter low byte
} CommandPacket_t;

/**
 * @brief Data Packet Header Structure (4 bytes) - Protocol v2.0
 *
 * Byte Layout:
 * [0] header      : 0xDA
 * [1] channel     : 0=DAC1, 1=DAC2
 * [2] length_h    : Number of samples (high byte, big-endian)
 * [3] length_l    : Number of samples (low byte, big-endian)
 * [4~] samples[]  : Audio samples (16-bit little-endian each)
 *
 * Total Size: 4 + (num_samples * 2) bytes
 * Maximum Size: 4 + (2048 * 2) = 4100 bytes
 *
 * NOTE: slave_id removed - hardware CS pin selects slave
 */
typedef struct __attribute__((packed)) {
    uint8_t header;         // 0xDA
    uint8_t channel;        // 0=DAC1, 1=DAC2
    uint8_t length_h;       // Sample count high byte
    uint8_t length_l;       // Sample count low byte
} DataPacketHeader_t;

/**
 * @brief Complete Data Packet (variable size)
 * @note  This structure is used for buffer allocation only.
 *        Actual samples follow the header in memory.
 */
typedef struct __attribute__((packed)) {
    DataPacketHeader_t header;
    uint16_t samples[MAX_SAMPLES_PER_PACKET];  // Audio samples
} DataPacket_t;

/* ============================================================================ */
/* Helper Macros */
/* ============================================================================ */

/**
 * @brief Decode 16-bit parameter from command packet
 */
#define GET_PARAM(cmd) ((uint16_t)(((cmd)->param_h << 8) | (cmd)->param_l))

/**
 * @brief Decode sample count from data packet header
 */
#define GET_SAMPLE_COUNT(hdr) ((uint16_t)(((hdr)->length_h << 8) | (hdr)->length_l))

/**
 * @brief Convert 16-bit sample to 12-bit DAC value
 */
#define SAMPLE_TO_DAC12(sample) ((uint16_t)((sample) >> 4))

/* ============================================================================ */
/* Validation Macros */
/* ============================================================================ */

/**
 * @brief Check if packet is for this slave (DEPRECATED in v2.0)
 * @note Hardware CS pin now selects slave
 */
// #define IS_FOR_ME(pkt) ((pkt)->slave_id == MY_SLAVE_ID)  // Deprecated

/**
 * @brief Validate slave ID (DEPRECATED in v2.0)
 */
// #define IS_VALID_SLAVE_ID(id) ((id) <= 2)  // Deprecated

/**
 * @brief Validate channel
 */
#define IS_VALID_CHANNEL(ch) ((ch) <= 1)

/**
 * @brief Validate sample count
 */
#define IS_VALID_SAMPLE_COUNT(cnt) ((cnt) > 0 && (cnt) <= MAX_SAMPLES_PER_PACKET)

#ifdef __cplusplus
}
#endif

#endif /* __SPI_PROTOCOL_H */
