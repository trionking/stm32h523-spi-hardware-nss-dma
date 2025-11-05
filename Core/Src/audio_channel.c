/**
  ******************************************************************************
  * @file           : audio_channel.c
  * @brief          : Audio Channel Management Implementation
  ******************************************************************************
  */

#include "audio_channel.h"
#include <string.h>

/* ============================================================================ */
/* Initialization */
/* ============================================================================ */

void audio_channel_init(AudioChannel_t *ch, uint16_t *buf_a, uint16_t *buf_b)
{
    // Set buffer pointers
    ch->buffer_a = buf_a;
    ch->buffer_b = buf_b;
    ch->active_buffer = buf_a;
    ch->fill_buffer = buf_b;
    ch->fill_index = 0;

    // Initialize state
    ch->is_playing = 0;
    ch->underrun = 0;
    ch->volume = 100;  // Default: 100% volume

    // Clear statistics
    ch->total_samples = 0;
    ch->buffer_swaps = 0;
    ch->underrun_count = 0;

    // Clear buffers (set to mid-scale for 12-bit DAC: 2048)
    for (uint16_t i = 0; i < AUDIO_BUFFER_SIZE; i++)
    {
        buf_a[i] = 2048;  // Mid-scale (0V for AC-coupled output)
        buf_b[i] = 2048;
    }
}

/* ============================================================================ */
/* Buffer Management */
/* ============================================================================ */

uint16_t audio_channel_fill(AudioChannel_t *ch, uint16_t *samples, uint16_t count)
{
    uint16_t filled = 0;

    for (uint16_t i = 0; i < count; i++)
    {
        // Check if buffer is full
        if (ch->fill_index >= AUDIO_BUFFER_SIZE)
        {
            break;  // Buffer full, stop filling
        }

        // Convert 16-bit sample to 12-bit DAC value
        uint16_t sample_16bit = samples[i];
        uint16_t dac_12bit = SAMPLE_TO_DAC12(sample_16bit);

        // Apply volume scaling
        if (ch->volume < 100)
        {
            // Scale DAC value around mid-point (2048)
            int32_t offset = (int32_t)dac_12bit - 2048;
            offset = (offset * ch->volume) / 100;
            dac_12bit = (uint16_t)(2048 + offset);
        }

        // Clamp to 12-bit range (0-4095)
        if (dac_12bit > 4095)
        {
            dac_12bit = 4095;
        }

        // Fill buffer
        ch->fill_buffer[ch->fill_index++] = dac_12bit;
        filled++;
    }

    // Update statistics
    ch->total_samples += filled;

    return filled;
}

uint8_t audio_channel_swap_buffers(AudioChannel_t *ch)
{
    // Check if fill buffer is ready (full or close to full)
    if (ch->fill_index < AUDIO_BUFFER_SIZE)
    {
        return 0;  // Not ready to swap
    }

    // Swap buffers
    uint16_t *temp = ch->active_buffer;
    ch->active_buffer = ch->fill_buffer;
    ch->fill_buffer = temp;

    // Reset fill index
    ch->fill_index = 0;

    // Update statistics
    ch->buffer_swaps++;

    return 1;  // Swap successful
}

uint8_t audio_channel_ready(AudioChannel_t *ch)
{
    // Ready if fill buffer has at least half full
    // This provides some margin before starting playback
    return (ch->fill_index >= (AUDIO_BUFFER_SIZE / 2));
}

void audio_channel_reset(AudioChannel_t *ch)
{
    // Stop playback
    ch->is_playing = 0;

    // Reset buffer state
    ch->fill_index = 0;
    ch->underrun = 0;

    // Clear buffers
    for (uint16_t i = 0; i < AUDIO_BUFFER_SIZE; i++)
    {
        ch->buffer_a[i] = 2048;
        ch->buffer_b[i] = 2048;
    }

    // Don't reset statistics - keep for debugging
}

/* ============================================================================ */
/* Statistics */
/* ============================================================================ */

void audio_channel_get_stats(AudioChannel_t *ch,
                             uint32_t *total_samples,
                             uint32_t *buffer_swaps,
                             uint32_t *underrun_count)
{
    if (total_samples)
    {
        *total_samples = ch->total_samples;
    }

    if (buffer_swaps)
    {
        *buffer_swaps = ch->buffer_swaps;
    }

    if (underrun_count)
    {
        *underrun_count = ch->underrun_count;
    }
}

void audio_channel_clear_underrun(AudioChannel_t *ch)
{
    ch->underrun = 0;
}
