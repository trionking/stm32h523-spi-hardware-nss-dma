/**
  ******************************************************************************
  * @file           : audio_channel.h
  * @brief          : Audio Channel Management with Double Buffering
  * @details        : Manages dual-buffer audio playback for DAC output
  *                   Handles buffer filling, swapping, and underrun detection
  ******************************************************************************
  * @attention
  *
  * Audio System:
  * - Sample Rate: 32kHz
  * - Buffer Size: 2048 samples = 64ms
  * - Resolution: 12-bit DAC (converted from 16-bit samples)
  * - Channels: 2 independent channels (DAC1, DAC2)
  *
  ******************************************************************************
  */

#ifndef __AUDIO_CHANNEL_H
#define __AUDIO_CHANNEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "spi_protocol.h"

/* ============================================================================ */
/* Audio Channel Structure */
/* ============================================================================ */

/**
 * @brief Audio Channel State
 */
typedef struct {
    // Double buffers
    uint16_t *buffer_a;         // Buffer A (2048 samples, 12-bit DAC values)
    uint16_t *buffer_b;         // Buffer B (2048 samples, 12-bit DAC values)

    // Buffer management
    uint16_t *active_buffer;    // Currently being output by DAC DMA
    uint16_t *fill_buffer;      // Currently being filled from SPI
    uint16_t fill_index;        // Current fill position (0~2047)

    // Playback state
    uint8_t is_playing;         // 0=stopped, 1=playing
    uint8_t underrun;           // Buffer underrun flag
    uint8_t volume;             // Volume level (0-100)

    // Statistics
    uint32_t total_samples;     // Total samples received
    uint32_t buffer_swaps;      // Number of buffer swaps
    uint32_t underrun_count;    // Number of underruns detected
} AudioChannel_t;

/* ============================================================================ */
/* Function Prototypes */
/* ============================================================================ */

/**
 * @brief Initialize audio channel with double buffers
 * @param ch Pointer to AudioChannel_t structure
 * @param buf_a Pointer to buffer A (must be 2048 * 2 bytes)
 * @param buf_b Pointer to buffer B (must be 2048 * 2 bytes)
 */
void audio_channel_init(AudioChannel_t *ch, uint16_t *buf_a, uint16_t *buf_b);

/**
 * @brief Fill audio channel buffer with samples
 * @param ch Pointer to AudioChannel_t structure
 * @param samples Pointer to 16-bit samples (little-endian)
 * @param count Number of samples to fill
 * @note  Automatically converts 16-bit samples to 12-bit DAC values
 *        Applies volume scaling
 *        Stops filling when buffer is full
 * @return Number of samples actually filled
 */
uint16_t audio_channel_fill(AudioChannel_t *ch, uint16_t *samples, uint16_t count);

/**
 * @brief Swap active and fill buffers
 * @param ch Pointer to AudioChannel_t structure
 * @note  Call this from DMA complete callback when fill buffer is ready
 * @return 1 if swap successful, 0 if fill buffer not ready
 */
uint8_t audio_channel_swap_buffers(AudioChannel_t *ch);

/**
 * @brief Check if channel is ready for playback
 * @param ch Pointer to AudioChannel_t structure
 * @return 1 if buffer has enough data to start playback
 */
uint8_t audio_channel_ready(AudioChannel_t *ch);

/**
 * @brief Reset audio channel to initial state
 * @param ch Pointer to AudioChannel_t structure
 */
void audio_channel_reset(AudioChannel_t *ch);

/**
 * @brief Get channel statistics
 * @param ch Pointer to AudioChannel_t structure
 * @param total_samples Output: Total samples received
 * @param buffer_swaps Output: Number of buffer swaps
 * @param underrun_count Output: Number of underruns
 */
void audio_channel_get_stats(AudioChannel_t *ch,
                             uint32_t *total_samples,
                             uint32_t *buffer_swaps,
                             uint32_t *underrun_count);

/**
 * @brief Clear underrun flag
 * @param ch Pointer to AudioChannel_t structure
 */
void audio_channel_clear_underrun(AudioChannel_t *ch);

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_CHANNEL_H */
