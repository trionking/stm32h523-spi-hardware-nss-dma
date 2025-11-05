/**
  ******************************************************************************
  * @file           : spi_handler.c
  * @brief          : SPI Packet Reception and Processing Implementation
  ******************************************************************************
  */

#include "spi_handler.h"
#include <stdio.h>
#include <string.h>

// Debug verbosity level (0=none, 1=errors only, 2=all)
#define SPI_DEBUG_LEVEL 1

/* ============================================================================ */
/* Private Variables */
/* ============================================================================ */

// SPI handle
static SPI_HandleTypeDef *g_hspi = NULL;

// Audio channels
static AudioChannel_t *g_dac1_channel = NULL;
static AudioChannel_t *g_dac2_channel = NULL;

// Reception state
static SPI_RxState_t g_rx_state = SPI_STATE_WAIT_HEADER;

// Reception buffers - must be 32-byte aligned for DMA!
static uint8_t g_rx_header;

// DMA RX buffer - placed in non-cacheable RAM for DCACHE compatibility
__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
static CommandPacket_t g_rx_cmd_packet;

// Data packet buffer (aligned for DMA, max size)
// NOTE: Not in .dma_buffer section (too large, ~4KB)
//       Data packets (0xDA) not implemented yet - will add cache handling when needed
__attribute__((aligned(32)))
static uint8_t g_rx_data_buffer[5 + (MAX_SAMPLES_PER_PACKET * 2)];  // 4101 bytes max

static DataPacketHeader_t *g_rx_data_header = (DataPacketHeader_t*)g_rx_data_buffer;
static uint16_t *g_rx_data_samples = (uint16_t*)(g_rx_data_buffer + sizeof(DataPacketHeader_t));

// Error statistics
static SPI_ErrorStats_t g_error_stats = {0};

// Debug: DMA RX complete counter
static volatile uint32_t g_dma_rx_complete_count = 0;

// Debug: Last received packet (for debugging without printf in ISR)
static volatile uint8_t g_last_rx_packet[5] = {0};
static volatile uint8_t g_last_rx_valid = 0;

// Dummy TX buffer for full-duplex DMA (slave doesn't care about TX data) - non-cacheable RAM
__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
static uint8_t g_dummy_tx[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* ============================================================================ */
/* External DAC/TIM handles (from main.c) */
/* ============================================================================ */

extern DAC_HandleTypeDef hdac1;
extern TIM_HandleTypeDef htim7;

/* ============================================================================ */
/* Private Function Prototypes */
/* ============================================================================ */

static void process_command_packet(CommandPacket_t *cmd);
static void process_data_packet(DataPacketHeader_t *header, uint16_t *samples);

/* ============================================================================ */
/* Initialization */
/* ============================================================================ */

void spi_handler_init(SPI_HandleTypeDef *hspi,
                      AudioChannel_t *dac1_ch,
                      AudioChannel_t *dac2_ch)
{
    g_hspi = hspi;
    g_dac1_channel = dac1_ch;
    g_dac2_channel = dac2_ch;
    g_rx_state = SPI_STATE_WAIT_HEADER;

    // Clear error statistics
    memset(&g_error_stats, 0, sizeof(g_error_stats));

    printf("[SPI] Handler initialized (Hardware NSS mode)\r\n");

    // Structure size check (simplified)
    printf("[SPI] CommandPacket=%d bytes, DataHeader=%d bytes\r\n",
           sizeof(CommandPacket_t), sizeof(DataPacketHeader_t));
}

void spi_handler_start(void)
{
    // Set RDY pin HIGH (ready to receive)
    spi_handler_set_ready(1);

    g_rx_state = SPI_STATE_WAIT_HEADER;

    printf("[SPI] Handler ready (Hardware NSS + DMA mode)\r\n");
    printf("[SPI] PA15 = SPI1_NSS (H/W controlled, no EXTI)\r\n");

    // Calculate pin number for RDY pin
    uint32_t pin_num = 0;
    uint32_t pin_mask = IN_RDY_Pin;
    while (pin_mask >>= 1) pin_num++;
    printf("[SPI] RDY Pin: PA%lu (0x%04X) = HIGH\r\n", pin_num, IN_RDY_Pin);

    // Clear RX buffer
    memset(&g_rx_cmd_packet, 0xFF, sizeof(CommandPacket_t));

    printf("[SPI] Starting DMA reception in continuous mode\r\n");
    printf("[SPI] H/W NSS enables data transfer when CS is LOW\r\n");

    // Start DMA reception (continuous mode)
    // H/W NSS will control when data is actually transferred
    g_rx_state = SPI_STATE_RECEIVE_CMD;
    HAL_StatusTypeDef status = HAL_SPI_Receive_DMA(g_hspi, (uint8_t*)&g_rx_cmd_packet, sizeof(CommandPacket_t));

    if (status != HAL_OK)
    {
        printf("[SPI] ERROR: Failed to start DMA reception (status=%d)\r\n", status);
    }
    else
    {
        printf("[SPI] DMA reception started successfully\r\n");
    }
}

/* ============================================================================ */
/* RDY Pin Control */
/* ============================================================================ */

void spi_handler_set_ready(uint8_t ready)
{
    GPIO_PinState state = ready ? GPIO_PIN_SET : GPIO_PIN_RESET;
    HAL_GPIO_WritePin(IN_RDY_GPIO_Port, IN_RDY_Pin, state);
}

/* ============================================================================ */
/* SPI Reception State Machine */
/* ============================================================================ */

void spi_handler_rx_callback(SPI_HandleTypeDef *hspi)
{
    // Count DMA RX complete
    g_dma_rx_complete_count++;

#if (SPI_DEBUG_LEVEL >= 1)
    // Debug: First 10 RX callbacks
    static uint32_t debug_rx_count = 0;
    if (debug_rx_count < 10)
    {
        debug_rx_count++;
        printf("[RX_CALLBACK] #%lu: DMA mode, RX data: %02X %02X %02X %02X %02X\r\n",
               debug_rx_count,
               ((uint8_t*)&g_rx_cmd_packet)[0],
               ((uint8_t*)&g_rx_cmd_packet)[1],
               ((uint8_t*)&g_rx_cmd_packet)[2],
               ((uint8_t*)&g_rx_cmd_packet)[3],
               ((uint8_t*)&g_rx_cmd_packet)[4]);
    }
#endif

    // H/W NSS + DMA mode: 5 bytes received
    // Process command packet
    if (g_rx_state == SPI_STATE_RECEIVE_CMD)
    {
        // Debug: Save last received packet
        memcpy((void*)g_last_rx_packet, &g_rx_cmd_packet, 5);
        g_last_rx_valid = 1;

        // Validate header
        if (g_rx_cmd_packet.header != HEADER_CMD)
        {
            g_error_stats.invalid_header_count++;
        }
        else
        {
            // Process command
            process_command_packet(&g_rx_cmd_packet);
        }

        // Restart DMA reception for next packet (continuous mode)
        memset(&g_rx_cmd_packet, 0xFF, sizeof(CommandPacket_t));
        g_rx_state = SPI_STATE_RECEIVE_CMD;
        HAL_StatusTypeDef status = HAL_SPI_Receive_DMA(hspi, (uint8_t*)&g_rx_cmd_packet, sizeof(CommandPacket_t));

        if (status != HAL_OK)
        {
            g_error_stats.spi_error_count++;
#if (SPI_DEBUG_LEVEL >= 1)
            printf("[RX_CALLBACK] ERROR: Failed to restart DMA (status=%d)\r\n", status);
#endif
        }
    }

    // Note: Data packets (0xDA) not implemented in H/W NSS + DMA mode yet
    // Can be added later if needed
}

void spi_handler_error_callback(SPI_HandleTypeDef *hspi)
{
    g_error_stats.spi_error_count++;

    uint32_t error = HAL_SPI_GetError(hspi);
    printf("[SPI] ERROR CALLBACK: HAL error 0x%lX, State=0x%02lX\r\n", error, (uint32_t)hspi->State);

    // Detailed error breakdown
    if (error & HAL_SPI_ERROR_MODF) printf("  - Mode Fault\r\n");
    if (error & HAL_SPI_ERROR_CRC) printf("  - CRC Error\r\n");
    if (error & HAL_SPI_ERROR_OVR) printf("  - Overrun\r\n");
    if (error & HAL_SPI_ERROR_FRE) printf("  - Frame Error\r\n");
    if (error & HAL_SPI_ERROR_DMA) printf("  - DMA Error\r\n");
    if (error & HAL_SPI_ERROR_ABORT) printf("  - Abort Error\r\n");

    // Reset SPI and restart reception
    HAL_SPI_DeInit(hspi);
    HAL_SPI_Init(hspi);

    // Reset state - wait for next CS falling edge
    g_rx_state = SPI_STATE_WAIT_HEADER;
}

/* ============================================================================ */
/* Packet Processing */
/* ============================================================================ */

static void process_command_packet(CommandPacket_t *cmd)
{
    // Validate channel
    if (!IS_VALID_CHANNEL(cmd->channel))
    {
#if (SPI_DEBUG_LEVEL >= 1)
        printf("[CMD] ERROR: Invalid channel %d\r\n", cmd->channel);
#endif
        return;
    }

    // Select target channel
    AudioChannel_t *channel = (cmd->channel == CHANNEL_DAC1) ? g_dac1_channel : g_dac2_channel;
    uint32_t dac_channel = (cmd->channel == CHANNEL_DAC1) ? DAC_CHANNEL_1 : DAC_CHANNEL_2;

    // Decode parameter
    uint16_t param = GET_PARAM(cmd);

    // Process command
    switch (cmd->command)
    {
        /* ------------------------------------------------------------------ */
        case CMD_PLAY:
        /* ------------------------------------------------------------------ */
        {
            if (!channel->is_playing)
            {
                channel->is_playing = 1;
                channel->underrun = 0;

                // Start TIM7 (DAC trigger, 32kHz)
                HAL_TIM_Base_Start(&htim7);

                // TEMP: Start DAC without DMA first to test
                HAL_DAC_Start(&hdac1, dac_channel);

                // Set a test value
                HAL_DAC_SetValue(&hdac1, dac_channel, DAC_ALIGN_12B_R, 2048);  // Mid-scale

                // TODO: Re-enable DMA after fixing GPDMA Linked List issue
                // HAL_StatusTypeDef status = HAL_DAC_Start_DMA(&hdac1, dac_channel,
                //                  (uint32_t*)channel->active_buffer,
                //                  AUDIO_BUFFER_SIZE,
                //                  DAC_ALIGN_12B_R);

#if (SPI_DEBUG_LEVEL >= 2)
                printf("[CMD] PLAY CH%d\r\n", cmd->channel);
#endif
            }
            break;
        }

        /* ------------------------------------------------------------------ */
        case CMD_STOP:
        /* ------------------------------------------------------------------ */
        {
            if (channel->is_playing)
            {
                channel->is_playing = 0;

                // Stop DAC DMA
                HAL_DAC_Stop_DMA(&hdac1, dac_channel);

                // Stop TIM7 if both channels stopped
                if (!g_dac1_channel->is_playing && !g_dac2_channel->is_playing)
                {
                    HAL_TIM_Base_Stop(&htim7);
                }

#if (SPI_DEBUG_LEVEL >= 2)
                printf("[CMD] STOP CH%d\r\n", cmd->channel);
#endif
            }
            break;
        }

        /* ------------------------------------------------------------------ */
        case CMD_VOLUME:
        /* ------------------------------------------------------------------ */
        {
            // Clamp volume to 0-100
            if (param > 100)
            {
                param = 100;
            }

            channel->volume = (uint8_t)param;
#if (SPI_DEBUG_LEVEL >= 2)
            printf("[CMD] VOLUME=%d CH%d\r\n", param, cmd->channel);
#endif
            break;
        }

        /* ------------------------------------------------------------------ */
        case CMD_RESET:
        /* ------------------------------------------------------------------ */
        {
            // Stop playback
            if (channel->is_playing)
            {
                channel->is_playing = 0;
                HAL_DAC_Stop_DMA(&hdac1, dac_channel);
            }

            // Reset channel
            audio_channel_reset(channel);

#if (SPI_DEBUG_LEVEL >= 2)
            printf("[CMD] RESET CH%d\r\n", cmd->channel);
#endif
            break;
        }

        /* ------------------------------------------------------------------ */
        default:
        /* ------------------------------------------------------------------ */
        {
#if (SPI_DEBUG_LEVEL >= 1)
            printf("[CMD] ERROR: Unknown command 0x%02X\r\n", cmd->command);
#endif
            break;
        }
    }
}

static void process_data_packet(DataPacketHeader_t *header, uint16_t *samples)
{
    // Validate channel
    if (!IS_VALID_CHANNEL(header->channel))
    {
#if (SPI_DEBUG_LEVEL >= 1)
        printf("[DATA] ERROR: Invalid channel %d\r\n", header->channel);
#endif
        return;
    }

    // Select target channel
    AudioChannel_t *channel = (header->channel == CHANNEL_DAC1) ? g_dac1_channel : g_dac2_channel;

    // Check if channel is playing
    if (!channel->is_playing)
    {
        // Ignore data if not playing
        return;
    }

    // Get sample count
    uint16_t num_samples = GET_SAMPLE_COUNT(header);

    // Fill channel buffer
    uint16_t filled = audio_channel_fill(channel, samples, num_samples);

#if (SPI_DEBUG_LEVEL >= 2)
    // Debug output (throttled)
    static uint32_t last_print_tick = 0;
    if ((HAL_GetTick() - last_print_tick) > 1000)
    {
        last_print_tick = HAL_GetTick();
        printf("[DATA] RX %d samples CH%d (fill_idx=%d)\r\n",
               filled, header->channel, channel->fill_index);
    }
#else
    (void)filled;  // Suppress unused warning
#endif
}

/* ============================================================================ */
/* Status and Diagnostics */
/* ============================================================================ */

SPI_RxState_t spi_handler_get_state(void)
{
    return g_rx_state;
}

void spi_handler_get_errors(SPI_ErrorStats_t *stats)
{
    if (stats)
    {
        memcpy(stats, &g_error_stats, sizeof(SPI_ErrorStats_t));
        // Add DMA RX complete counter
        stats->dma_rx_complete_count = g_dma_rx_complete_count;
    }
}

void spi_handler_reset_errors(void)
{
    memset(&g_error_stats, 0, sizeof(SPI_ErrorStats_t));
}

/* ============================================================================ */
/* CS Falling Edge Handler (EXTI Callback) */
/* ============================================================================ */

/**
 * @brief CS pin falling edge handler
 * @note NOT USED: Hardware NSS mode handles CS detection automatically
 *       This function is kept for potential future use with Software NSS + EXTI
 */
void spi_handler_cs_falling(void)
{
    // Hardware NSS mode: This function is not called
    // Kept for potential future use with Software NSS + EXTI mode
    (void)g_hspi;  // Suppress unused warning
}

/**
 * @brief NSS rising edge handler (transmission end)
 * @note NOT USED: Hardware NSS mode handles CS detection automatically
 *       This function is kept for potential future use with Software NSS + EXTI
 */
void spi_handler_cs_rising(void)
{
    // Hardware NSS mode: This function is not called
    // Kept for potential future use with Software NSS + EXTI mode
}

/**
 * @brief Get last received packet (for debugging)
 */
uint8_t spi_handler_get_last_packet(uint8_t *buffer)
{
    if (buffer && g_last_rx_valid)
    {
        memcpy(buffer, (void*)g_last_rx_packet, 5);
        return 1;
    }
    return 0;
}

/**
 * @brief Get address of SPI RX DMA buffer (for debugging)
 */
uint32_t spi_handler_get_rx_buffer_addr(void)
{
    return (uint32_t)&g_rx_cmd_packet;
}
