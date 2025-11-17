/**
  ******************************************************************************
  * @file           : spi_handler.c
  * @brief          : SPI Packet Reception and Processing Implementation
  * @version        : Protocol v1.2 (Slave ID removed, CS pin selection)
  * @date           : 2025-11-07
  ******************************************************************************
  */

#include "spi_handler.h"
#include <stdio.h>
#include <string.h>

// Debug verbosity level (0=none, 1=errors only, 2=all)
// IMPORTANT: Must be 0 for real-time operation! Printf in ISR causes data loss!
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

// Debug: CS edge counters (for monitoring without printf in ISR)
static volatile uint32_t g_cs_falling_count = 0;
static volatile uint32_t g_cs_rising_count = 0;
static volatile uint32_t g_last_received_bytes = 0;

// Debug: Last received packet (for debugging without printf in ISR)
static volatile uint8_t g_last_rx_packet[5] = {0};
static volatile uint8_t g_last_rx_valid = 0;

// Dummy TX buffer for full-duplex DMA (slave doesn't care about TX data) - non-cacheable RAM
__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
static uint8_t g_dummy_tx[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Large RX buffer for variable-length packets
// Size: DataPacketHeader_t (5 bytes) + MAX_SAMPLES_PER_PACKET * 2 bytes
// = 5 + 4100*2 = 8205 bytes, rounded to 8300 for safety
// NOTE: In regular RAM (not .dma_buffer) to save DMA space.
//       Requires cache invalidation after DMA receive (handled in code).
__attribute__((aligned(32)))
static uint8_t g_rx_large_buffer[8300];

// DUAL DAC MODE: Combined 32-bit buffer for simultaneous CH1+CH2 output
// Format: buffer[i] = (CH2_sample << 16) | CH1_sample
// Must be in non-cacheable RAM for DMA + DCACHE compatibility
__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
static uint32_t g_dual_dac_buffer[AUDIO_BUFFER_SIZE];  // 4096 32-bit words = 16KB

/* ============================================================================ */
/* External DAC/TIM handles (from main.c) */
/* ============================================================================ */

extern DAC_HandleTypeDef hdac1;
extern TIM_HandleTypeDef htim1;  // Used for DUAL DAC mode
extern TIM_HandleTypeDef htim7;  // Legacy (not used in dual mode)

/* ============================================================================ */
/* Private Function Prototypes */
/* ============================================================================ */

static void process_command_packet(CommandPacket_t *cmd);
static void process_data_packet(DataPacketHeader_t *header, uint16_t *samples);
static void safe_stop_dac_dma(uint32_t dac_channel);

/* ============================================================================ */
/* Helper Functions */
/* ============================================================================ */

/**
 * @brief  Safely stop DAC DMA without HAL_DMA_Abort hang
 * @param  dac_channel: DAC_CHANNEL_1 or DAC_CHANNEL_2
 * @note   HAL_DAC_Stop_DMA calls HAL_DMA_Abort which hangs waiting for SUSPF
 *         This function directly disables DMA and clears DAC DMA enable bit
 */
static void safe_stop_dac_dma(uint32_t dac_channel)
{
    DMA_HandleTypeDef *hdma = (dac_channel == DAC_CHANNEL_1) ? hdac1.DMA_Handle1 : hdac1.DMA_Handle2;

    if (hdma == NULL || hdma->Instance == NULL)
    {
        return;  // No DMA configured
    }

    DMA_Channel_TypeDef *dma_ch = (DMA_Channel_TypeDef *)hdma->Instance;

    // Disable DMA channel directly (avoid HAL_DMA_Abort hang)
    dma_ch->CCR &= ~DMA_CCR_EN;

    // Wait for DMA to stop (with timeout)
    uint32_t timeout = 10000;
    while ((dma_ch->CCR & DMA_CCR_EN) && (timeout > 0))
    {
        timeout--;
    }

    // Disable DAC DMA request
    if (dac_channel == DAC_CHANNEL_1)
    {
        CLEAR_BIT(DAC1->CR, DAC_CR_DMAEN1);
        hdac1.State = HAL_DAC_STATE_READY;
    }
    else
    {
        CLEAR_BIT(DAC1->CR, DAC_CR_DMAEN2);
    }

    // Reset DMA state
    hdma->State = HAL_DMA_STATE_READY;
}

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

    printf("[SPI] Handler initialized (Protocol v1.2, CS pin selection)\r\n");

    // Structure size check (simplified)
    printf("[SPI] CommandPacket=%d bytes, DataHeader=%d bytes (Slave ID removed)\r\n",
           sizeof(CommandPacket_t), sizeof(DataPacketHeader_t));
}

void spi_handler_start(void)
{
    // Set RDY pin LOW (ready to receive, Active Low)
    spi_handler_set_ready(1);

    g_rx_state = SPI_STATE_WAIT_HEADER;

    printf("[SPI] Handler ready (Hardware NSS + DMA mode)\r\n");
    printf("[SPI] PA15 = SPI1_NSS (H/W controlled, no EXTI)\r\n");

    // Calculate pin number for RDY pin
    uint32_t pin_num = 0;
    uint32_t pin_mask = OT_nRDY_Pin;
    while (pin_mask >>= 1) pin_num++;
    printf("[SPI] nRDY Pin: PA%lu (0x%04X) = LOW (ready)\r\n", pin_num, OT_nRDY_Pin);

    // Clear RX buffer
    memset(g_rx_large_buffer, 0xFF, sizeof(g_rx_large_buffer));

    printf("[SPI] Ready for DMA reception with large buffer (4100 bytes)\r\n");
    printf("[SPI] Software NSS + EXTI mode enabled\r\n");
    printf("[SPI] EXTI falling edge: Start DMA reception\r\n");
    printf("[SPI] EXTI rising edge: Stop DMA and process received data\r\n");

    // Clear any pending EXTI flags before enabling interrupt
    EXTI->FPR1 = (1U << 15);  // Clear falling edge pending
    EXTI->RPR1 = (1U << 15);  // Clear rising edge pending

    // Debug: Check PA15 pin configuration and EXTI settings
    GPIO_PinState pa15_state = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15);
    printf("[DEBUG] PA15 pin state: %d (0=LOW, 1=HIGH)\r\n", pa15_state);
    printf("[DEBUG] EXTI->FTSR1 bit 15: %lu (falling trigger)\r\n", (EXTI->FTSR1 >> 15) & 1);
    printf("[DEBUG] EXTI->RTSR1 bit 15: %lu (rising trigger)\r\n", (EXTI->RTSR1 >> 15) & 1);
    printf("[DEBUG] EXTI->IMR1 bit 15: %lu (interrupt mask)\r\n", (EXTI->IMR1 >> 15) & 1);
    printf("[DEBUG] NVIC EXTI15 enabled: %lu\r\n", (NVIC->ISER[EXTI15_IRQn >> 5] >> (EXTI15_IRQn & 0x1F)) & 1);

    // Enable EXTI15 interrupt
    HAL_NVIC_EnableIRQ(EXTI15_IRQn);

    printf("[SPI] EXTI15 interrupt enabled, waiting for CS falling edge...\r\n");
    printf("[SPI] Please send SPITEST PLAY 0 0 from Master\r\n");

    // Software NSS mode: DMA will be started by CS falling edge (EXTI callback)
    // Do NOT start DMA here - wait for CS LOW
    g_rx_state = SPI_STATE_RECEIVE_CMD;
}

/* ============================================================================ */
/* RDY Pin Control (Active Low) */
/* ============================================================================ */

void spi_handler_set_ready(uint8_t ready)
{
    // Active Low: ready=1 → LOW, ready=0 → HIGH
    GPIO_PinState state = ready ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(OT_nRDY_GPIO_Port, OT_nRDY_Pin, state);
}

void spi_handler_update_rdy(void)
{
    // IMPORTANT: During playback, always keep RDY=LOW
    // Rationale: DAC DMA continuously consumes data from active_buffer,
    //            so fill_buffer will be available after buffer swap (max 64ms)
    //            This prevents Main from waiting unnecessarily between chunks
    if (g_dac1_channel->is_playing || g_dac2_channel->is_playing)
    {
        // Always ready during playback (double buffering handles overflow)
        spi_handler_set_ready(1);
        return;
    }

    // Not playing - check buffer status for pre-buffering
    uint8_t dac1_ready = (g_dac1_channel->fill_index < AUDIO_BUFFER_SIZE);
    uint8_t dac2_ready = (g_dac2_channel->fill_index < AUDIO_BUFFER_SIZE);

    // Both channels must be ready for RDY=LOW
    // If either channel is full, RDY=HIGH (busy)
    spi_handler_set_ready(dac1_ready && dac2_ready);
}

/* ============================================================================ */
/* EXTI NSS Rising Edge Detection */
/* ============================================================================ */

void spi_handler_init_nss_exti(void)
{
    // EXTI15 is already configured in HAL_SPI_MspInit() (stm32h5xx_hal_msp.c)
    // PA15 is set to GPIO_MODE_IT_RISING_FALLING with pull-up
    // This function is kept for API compatibility but does nothing

    printf("[EXTI] PA15 EXTI already configured in SPI MSP Init\r\n");
    printf("[EXTI] Both edges enabled: Falling=Start DMA, Rising=Stop & Process\r\n");
}

/* ============================================================================ */
/* SPI Reception State Machine */
/* ============================================================================ */

void spi_handler_rx_callback(SPI_HandleTypeDef *hspi)
{
    // Count DMA RX complete
    g_dma_rx_complete_count++;

    // NOTE: In CS edge-based mode, packet processing is done in spi_handler_cs_rising()
    // This callback is only called if DMA completes the full 4100 bytes (rare)
    // We should NOT restart DMA here - let CS edge handlers manage it

    // Just return - CS rising edge handler will process the packet
    return;

    // OLD CODE BELOW (kept for reference, never executed)
    // ========================================================

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
    // Process command packet or data packet header
    if (g_rx_state == SPI_STATE_RECEIVE_CMD)
    {
        // Debug: Save last received packet
        memcpy((void*)g_last_rx_packet, &g_rx_cmd_packet, 5);
        g_last_rx_valid = 1;

        uint8_t header = g_rx_cmd_packet.header;

        // Command Packet (0xC0)
        if (header == HEADER_CMD)
        {
            // Process command
            process_command_packet(&g_rx_cmd_packet);

            // Restart DMA reception for next packet
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
        // Data Packet Header (0xDA) - Phase 2-2 implementation
        else if (header == HEADER_DATA)
        {
            // Parse data packet header (first 4 bytes received as CommandPacket_t)
            DataPacketHeader_t *hdr = (DataPacketHeader_t *)&g_rx_cmd_packet;
            uint16_t sample_count = GET_SAMPLE_COUNT(hdr);
            uint8_t channel = hdr->channel;

            // Validate sample count
            if (sample_count > 0 && sample_count <= MAX_SAMPLES_PER_PACKET)
            {
                printf("[DATA] Ch=%d, Samples=%d\r\n", channel, sample_count);

                // Note: In DMA mode, we can't easily receive variable-length data packets
                // For now, just log and ignore the sample data
                // TODO: Implement state machine for data packet reception if needed

                printf("[DMA] Data packet detected but sample data reception not implemented in DMA mode\r\n");
                printf("[DMA] Use Menu 5 (Polling Mode) for full data packet support\r\n");
            }
            else
            {
                g_error_stats.invalid_header_count++;
                printf("[ERROR] Invalid sample count: %d\r\n", sample_count);
            }

            // Restart DMA reception for next packet
            memset(&g_rx_cmd_packet, 0xFF, sizeof(CommandPacket_t));
            g_rx_state = SPI_STATE_RECEIVE_CMD;
            HAL_StatusTypeDef status = HAL_SPI_Receive_DMA(hspi, (uint8_t*)&g_rx_cmd_packet, sizeof(CommandPacket_t));

            if (status != HAL_OK)
            {
                g_error_stats.spi_error_count++;
            }
        }
        else
        {
            // Unknown header
            g_error_stats.invalid_header_count++;

            // Restart DMA reception
            memset(&g_rx_cmd_packet, 0xFF, sizeof(CommandPacket_t));
            g_rx_state = SPI_STATE_RECEIVE_CMD;
            HAL_SPI_Receive_DMA(hspi, (uint8_t*)&g_rx_cmd_packet, sizeof(CommandPacket_t));
        }
    }
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
    if (error & HAL_SPI_ERROR_DMA) {
        printf("  - DMA Error\r\n");
        if (hspi->hdmarx != NULL) {
            printf("    RX DMA ErrorCode: 0x%08lX\r\n", hspi->hdmarx->ErrorCode);
            printf("    RX DMA Buffer: 0x%08lX\r\n", (uint32_t)g_rx_large_buffer);
        }
        if (hspi->hdmatx != NULL) {
            printf("    TX DMA ErrorCode: 0x%08lX\r\n", hspi->hdmatx->ErrorCode);
        }
    }
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
            // CRITICAL FIX: Check if DMA is already running and stop it first
            // This prevents "DMA BUSY" error when receiving multiple PLAY commands
            DMA_HandleTypeDef *hdma_check = (dac_channel == DAC_CHANNEL_1) ?
                                             hdac1.DMA_Handle1 : hdac1.DMA_Handle2;

            if (hdma_check != NULL && channel->is_playing)
            {
                // Channel is already playing - stop it first
                printf("[CMD_PLAY] WARNING: Channel already playing - stopping first\r\n");

                // Stop DMA and DAC properly (avoid HAL_DMA_Abort hang)
                safe_stop_dac_dma(dac_channel);

                // Force DMA state reset (HAL_DAC_Stop_DMA sometimes fails to clear state)
                DMA_Channel_TypeDef *dma_ch_stop = (DMA_Channel_TypeDef *)hdma_check->Instance;

                // Disable DMA
                dma_ch_stop->CCR &= ~DMA_CCR_EN;

                // Wait for DMA to stop
                uint32_t timeout = 10000;
                while ((dma_ch_stop->CCR & DMA_CCR_EN) && timeout--) __NOP();

                // Clear all DMA flags
                dma_ch_stop->CFCR = 0x00000FFF;

                // Reset handle states
                hdma_check->State = HAL_DMA_STATE_READY;
                hdma_check->ErrorCode = HAL_DMA_ERROR_NONE;
                hdac1.State = HAL_DAC_STATE_READY;
                hdac1.ErrorCode = HAL_DAC_ERROR_NONE;

                channel->is_playing = 0;

                printf("  DMA stopped and reset - ready for new PLAY command\r\n");
            }

            // Check buffer readiness and swap if ready
            if (channel->fill_index >= AUDIO_BUFFER_SIZE)
            {
                // Fill buffer is ready - swap before playback
                // This resets fill_index to 0, making RDY=LOW after update
                if (audio_channel_swap_buffers(channel))
                {
#if (SPI_DEBUG_LEVEL >= 1)
                    printf("[CMD_PLAY] Buffer swapped (fill_index reset to 0)\r\n");
#endif
                }
            }
            else if (channel->fill_index == 0)
            {
#if (SPI_DEBUG_LEVEL >= 1)
                printf("[CMD_PLAY] WARNING: Buffer empty (fill_index=0)\r\n");
                printf("            Starting with initialized buffer (may produce silence or garbage)\r\n");
#endif
            }
            else
            {
#if (SPI_DEBUG_LEVEL >= 1)
                printf("[CMD_PLAY] WARNING: Buffer partially filled (%d/%d samples)\r\n",
                       channel->fill_index, AUDIO_BUFFER_SIZE);
                printf("            Recommend waiting for full buffer to avoid underrun\r\n");
#endif
            }

            // Start playback
            channel->is_playing = 1;
            channel->underrun = 0;

            // CRITICAL: Timer will be started AFTER DMA setup to prevent SUSPEND state
            // Do NOT start timer here - it will be started after HAL_DAC_Start_DMA succeeds

            // Check if DMA is configured for this DAC channel
            DMA_HandleTypeDef *hdma = (dac_channel == DAC_CHANNEL_1) ?
                                      hdac1.DMA_Handle1 : hdac1.DMA_Handle2;

            if (hdma != NULL)
            {
                // DMA configured - use DMA mode
                // DEBUG: Always show DAC2 start info
                if (cmd->channel == CHANNEL_DAC2)
                {
                    printf("[CMD_PLAY] DAC2 Starting - DMA=0x%08lX, Buf=0x%08lX, Size=%d\r\n",
                           (uint32_t)hdma, (uint32_t)channel->active_buffer, AUDIO_BUFFER_SIZE);
                }
#if (SPI_DEBUG_LEVEL >= 1)
                else
                {
                    printf("[CMD_PLAY] DAC CH%d, DMA=0x%08lX, Buf=0x%08lX, Size=%d\r\n",
                           cmd->channel, (uint32_t)hdma,
                           (uint32_t)channel->active_buffer, AUDIO_BUFFER_SIZE);
                }
#endif

                // INDEPENDENT DAC MODE - Each channel uses its own DMA and trigger
                HAL_StatusTypeDef status;

#if (SPI_DEBUG_LEVEL >= 1)
                printf("[CMD_PLAY] INDEPENDENT MODE: CH%d using 16-bit buffer directly\r\n", cmd->channel);
#endif

                // Start DAC DMA with HAL (automatically uses DHR12R1 or DHR12R2)
                status = HAL_DAC_Start_DMA(&hdac1, dac_channel,
                                           (uint32_t*)channel->active_buffer,
                                           AUDIO_BUFFER_SIZE,
                                           DAC_ALIGN_12B_R);

                if (status == HAL_OK)
                {
                    // NOTE: HAL_DAC_Start_DMA with DAC_ALIGN_12B_R correctly sets DHR12R2
                    // DHR12R2 address: 0x42028414 (offset 0x14 from DAC1 base)
                    // No manual CDAR fix needed for 12-bit right-aligned mode
                    if (dac_channel == DAC_CHANNEL_2)
                    {
                        uint32_t dhr12r2_addr = (uint32_t)&(DAC1->DHR12R2);
                        printf("[DEBUG] DAC CH2: DHR12R2 address = 0x%08lX\r\n", dhr12r2_addr);
                    }

#if (SPI_DEBUG_LEVEL >= 1)
                    printf("[CMD_PLAY] INDEPENDENT MODE: DAC DMA started successfully\r\n");

                    // Debug: Check DMA registers
                    DMA_Channel_TypeDef *dma_ch = (DMA_Channel_TypeDef *)hdma->Instance;
                    printf("  DMA CCR: 0x%08lX (EN=%d)\r\n", dma_ch->CCR, (dma_ch->CCR & DMA_CCR_EN) ? 1 : 0);
                    printf("  DMA CSR: 0x%08lX\r\n", dma_ch->CSR);
                    printf("  DMA CBR1: %lu items\r\n", dma_ch->CBR1 & 0xFFFF);
                    printf("  DMA CSAR: 0x%08lX\r\n", dma_ch->CSAR);
                    printf("  DMA CDAR: 0x%08lX\r\n", dma_ch->CDAR);
#endif
                }

                if (status != HAL_OK)
                {
                    // Always show error for any channel
                    printf("[CMD_PLAY] ERROR: HAL_DAC_Start_DMA failed (CH%d): 0x%02X\r\n",
                           cmd->channel, status);
                    printf("  DAC State: 0x%02X, ErrorCode: 0x%08lX\r\n",
                           hdac1.State, hdac1.ErrorCode);
                    printf("  DMA State: 0x%02X, ErrorCode: 0x%08lX\r\n",
                           hdma->State, hdma->ErrorCode);

                    // Read DMA registers directly
                    DMA_Channel_TypeDef *dma_ch = (DMA_Channel_TypeDef *)hdma->Instance;
                    printf("  DMA CCR: 0x%08lX (EN=%d)\r\n", dma_ch->CCR, (dma_ch->CCR & DMA_CCR_EN) ? 1 : 0);
                    printf("  DMA CSR: 0x%08lX\r\n", dma_ch->CSR);
                    printf("  DMA CTR1: 0x%08lX\r\n", dma_ch->CTR1);
                    printf("  DMA CBR1: 0x%08lX\r\n", dma_ch->CBR1);
                    printf("  DMA CSAR: 0x%08lX\r\n", dma_ch->CSAR);
                    printf("  DMA CDAR: 0x%08lX\r\n", dma_ch->CDAR);

                    // Check DAC registers
                    printf("  DAC CR: 0x%08lX\r\n", DAC1->CR);
                    printf("  DAC SR: 0x%08lX\r\n", DAC1->SR);

                    // DMA start failed - fall back to simple mode
                    HAL_DAC_Start(&hdac1, dac_channel);
                    HAL_DAC_SetValue(&hdac1, dac_channel, DAC_ALIGN_12B_R, 2048);
                }
                else
                {
                    // DMA started successfully - NOW start the timer
                    // INDEPENDENT MODE: Each channel uses its own timer
                    extern TIM_HandleTypeDef htim1;
                    extern TIM_HandleTypeDef htim7;

                    if (dac_channel == DAC_CHANNEL_1)
                    {
                        HAL_TIM_Base_Start(&htim1);  // CH1 uses TIM1
#if (SPI_DEBUG_LEVEL >= 1)
                        printf("[CMD_PLAY] Started TIM1 for DAC CH1\r\n");
#endif
                    }
                    else
                    {
                        HAL_TIM_Base_Start(&htim7);  // CH2 uses TIM7
                        printf("[CMD_PLAY] Started TIM7 for DAC CH2\r\n");

                        // DEBUG: Check TIM7 is actually running
                        uint32_t tim7_cnt_before = TIM7->CNT;
                        for (volatile uint32_t i = 0; i < 100000; i++) __NOP();
                        uint32_t tim7_cnt_after = TIM7->CNT;

                        printf("[DEBUG] TIM7 CNT (before): %lu, (after): %lu\r\n", tim7_cnt_before, tim7_cnt_after);
                        if (tim7_cnt_after != tim7_cnt_before) {
                            printf("  ✓ TIM7 is running!\r\n");
                        } else {
                            printf("  ✗ TIM7 is NOT running!\r\n");
                        }

                        // DEBUG: Check TIM7 TRGO configuration
                        uint32_t tim7_cr2 = TIM7->CR2;
                        uint32_t mms = (tim7_cr2 >> 4) & 0x7;  // MMS bits [6:4]
                        printf("[DEBUG] TIM7 CR2: 0x%08lX, MMS: %lu (should be 2 for Update event)\r\n",
                               tim7_cr2, mms);

                        // DEBUG: Check DAC CH2 register bits
                        uint32_t dac_cr = DAC1->CR;
                        printf("[DEBUG] DAC CH2 settings:\r\n");
                        printf("  EN2=%d (bit 16)\r\n", (dac_cr & (1 << 16)) ? 1 : 0);
                        printf("  TEN2=%d (bit 17)\r\n", (dac_cr & (1 << 17)) ? 1 : 0);
                        printf("  TSEL2=%lu (bits 21-18, should be 6 for TIM7 TRGO)\r\n", (dac_cr >> 18) & 0xF);
                        printf("  DMAEN2=%d (bit 28)\r\n", (dac_cr & (1 << 28)) ? 1 : 0);

                        // DEBUG: Check GPDMA2_Channel1 status
                        DMA_Channel_TypeDef *dma_ch2 = (DMA_Channel_TypeDef *)hdma->Instance;
                        printf("[DEBUG] GPDMA2_Channel1:\r\n");
                        printf("  CCR: 0x%08lX (EN=%d)\r\n", dma_ch2->CCR, (dma_ch2->CCR & DMA_CCR_EN) ? 1 : 0);
                        printf("  CSR: 0x%08lX\r\n", dma_ch2->CSR);
                        printf("  CBR1: %lu items\r\n", dma_ch2->CBR1 & 0xFFFF);
                        printf("  CSAR: 0x%08lX (source)\r\n", dma_ch2->CSAR);
                        printf("  CDAR: 0x%08lX (dest, should be DHR12R2=0x42028414)\r\n", dma_ch2->CDAR);
                    }

#if (SPI_DEBUG_LEVEL >= 2)
                    // Debug info for CH1
                    if (dac_channel == DAC_CHANNEL_1)
                    {
                        DMA_Channel_TypeDef *dma_ch = (DMA_Channel_TypeDef *)hdma->Instance;
                        printf("[CMD_PLAY] CH%d: DMA EN=%d, CBR1=%lu items\r\n",
                               cmd->channel,
                               (dma_ch->CCR & DMA_CCR_EN) ? 1 : 0,
                               dma_ch->CBR1 & 0xFFFF);
                    }
#endif
                }
            }
            else
            {
#if (SPI_DEBUG_LEVEL >= 1)
                printf("[CMD_PLAY] No DMA configured - using simple mode\r\n");
#endif
                // No DMA configured - use simple mode (constant value)
                HAL_DAC_Start(&hdac1, dac_channel);
                HAL_DAC_SetValue(&hdac1, dac_channel, DAC_ALIGN_12B_R, 2048);
            }

            // Update RDY pin after starting playback
            // If buffer was swapped, fill_index is now 0 → RDY will be LOW (ready for more data)
            spi_handler_update_rdy();

#if (SPI_DEBUG_LEVEL >= 2)
            printf("[CMD] PLAY CH%d\r\n", cmd->channel);
#endif
            break;
        }

        /* ------------------------------------------------------------------ */
        case CMD_STOP:
        /* ------------------------------------------------------------------ */
        {
            if (channel->is_playing)
            {
                channel->is_playing = 0;

                // Stop DAC DMA (avoid HAL_DMA_Abort hang)
                safe_stop_dac_dma(dac_channel);

                // Stop TIM1 if both channels stopped (DUAL DAC MODE)
                if (!g_dac1_channel->is_playing && !g_dac2_channel->is_playing)
                {
                    extern TIM_HandleTypeDef htim1;
                    HAL_TIM_Base_Stop(&htim1);
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
                safe_stop_dac_dma(dac_channel);
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

    // Accept data regardless of playing state (for pre-buffering)
    // If not playing, data will be buffered and ready for PLAY command

    // Get sample count
    uint16_t num_samples = GET_SAMPLE_COUNT(header);

#if (SPI_DEBUG_LEVEL >= 1)
    // Debug: Read RDY pin state before processing
    GPIO_PinState rdy_before = HAL_GPIO_ReadPin(OT_nRDY_GPIO_Port, OT_nRDY_Pin);
#endif

    // Fill channel buffer
    uint16_t filled = audio_channel_fill(channel, samples, num_samples);

    // Update RDY pin based on buffer status
    spi_handler_update_rdy();

#if (SPI_DEBUG_LEVEL >= 1)
    // Debug: Show first 5 DATA packets with RDY state changes
    static uint32_t data_packet_debug_count = 0;
    if (data_packet_debug_count < 5)
    {
        data_packet_debug_count++;
        GPIO_PinState rdy_after = HAL_GPIO_ReadPin(OT_nRDY_GPIO_Port, OT_nRDY_Pin);
        uint16_t free_space = AUDIO_BUFFER_SIZE - channel->fill_index;

        printf("[DATA #%lu] DAC%d: %d samples\r\n",
               data_packet_debug_count, header->channel + 1, filled);
        printf("           RDY: %d → %d (%s)\r\n",
               rdy_before, rdy_after,
               rdy_after == 0 ? "Ready" : "Busy");
        printf("           Buffer free: %d samples (fill_idx=%d)\r\n",
               free_space, channel->fill_index);
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
        // Add CS edge counters
        stats->cs_falling_count = g_cs_falling_count;
        stats->cs_rising_count = g_cs_rising_count;
        // Add last received bytes
        stats->last_received_bytes = g_last_received_bytes;
        // DMA start failures already in g_error_stats
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
 * @brief CS pin falling edge handler (packet start)
 * @note Called from EXTI15 interrupt when CS goes LOW (Master starts transmission)
 * @note Starts DMA reception with large buffer for variable-length packets
 */
void spi_handler_cs_falling(void)
{
    // CS falling edge = Master has asserted CS = packet transmission starting

    // Increment counter (for debugging without printf)
    g_cs_falling_count++;

    // Ensure SPI is in READY state before starting new DMA
    // NOTE: HAL_SPI_Abort() can take time and cause DMA start to fail!
    // Force READY state directly instead
    if (g_hspi->State != HAL_SPI_STATE_READY)
    {
        // Force stop DMA
        if (g_hspi->hdmarx != NULL)
        {
            HAL_DMA_Abort(g_hspi->hdmarx);
        }
        // Force state to READY
        g_hspi->State = HAL_SPI_STATE_READY;
        g_error_stats.spi_error_count++;
    }

    // Change EXTI to rising edge (to detect packet end)
    // Disable falling edge, enable rising edge
    EXTI->FTSR1 &= ~(1U << 15);  // Disable falling trigger
    EXTI->RTSR1 |= (1U << 15);   // Enable rising trigger

    // Start DMA reception with maximum buffer size
    // NSS rising edge will stop DMA and determine actual received bytes
    // NOTE: memset removed - causes race condition with DMA!
    // Old data in buffer doesn't matter, only received bytes are processed
    // memset(g_rx_large_buffer, 0xFF, sizeof(g_rx_large_buffer));

    HAL_StatusTypeDef status = HAL_SPI_Receive_DMA(g_hspi, g_rx_large_buffer, sizeof(g_rx_large_buffer));
    if (status != HAL_OK)
    {
        // DMA start failed - record state for debugging
        g_error_stats.spi_error_count++;
        g_error_stats.dma_start_fail_count++;
        g_error_stats.last_spi_state = g_hspi->State;
    }
}

/**
 * @brief NSS rising edge handler (transmission end)
 * @note Called from EXTI15 interrupt when NSS goes HIGH (CS deasserted)
 * @note This function stops DMA, checks received bytes, and processes packet
 */
void spi_handler_cs_rising(void)
{
    // NSS rising edge = Master has deasserted CS = packet transfer complete

    // Increment counter (for debugging without printf)
    g_cs_rising_count++;

    // 1. Calculate actual bytes received FIRST (before aborting DMA!)
    // DMA counter counts DOWN from initial value to 0
    // Received bytes = Total size - Remaining counter value
    uint32_t total_size = sizeof(g_rx_large_buffer);  // 8300 bytes
    uint32_t remaining = __HAL_DMA_GET_COUNTER(g_hspi->hdmarx);
    uint32_t received = total_size - remaining;

    // Save for debugging (can be read from main loop)
    g_last_received_bytes = received;

    // NOTE: DCACHE is disabled, so no cache invalidation needed
    // If data is still wrong, it means buffer is not in non-cacheable RAM!

    // 2. Stop ongoing DMA transfer and FULLY reset SPI
    if (g_hspi->State != HAL_SPI_STATE_READY)
    {
        // Force stop DMA
        if (g_hspi->hdmarx != NULL)
        {
            HAL_DMA_Abort(g_hspi->hdmarx);
        }

        // CRITICAL: Fully reset SPI to clean state
        // This ensures next reception works correctly
        HAL_SPI_DeInit(g_hspi);
        HAL_SPI_Init(g_hspi);
    }

    // 3. Process packet if valid data received
    if (received >= 4)  // Minimum: 4-byte header
    {
        // NOTE: g_rx_large_buffer cache handling:
        // Buffer is in regular (cacheable) RAM to save DMA space.
        // For STM32H5, DCACHE1 is managed by peripheral - manual invalidation not needed.
        // This buffer is only used for infrequent command packets, not realtime audio.

        uint8_t header = g_rx_large_buffer[0];

        // DEBUG: Print buffer info for DATA packets
        if (received > 100) {
            printf("[SPI_RX] Buffer=0x%08lX, Received=%lu, Header=0x%02X\r\n",
                   (uint32_t)g_rx_large_buffer, received, header);
            printf("         First 8 bytes: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                   g_rx_large_buffer[0], g_rx_large_buffer[1],
                   g_rx_large_buffer[2], g_rx_large_buffer[3],
                   g_rx_large_buffer[4], g_rx_large_buffer[5],
                   g_rx_large_buffer[6], g_rx_large_buffer[7]);
        }

        // Command Packet (0xC0, 5 bytes)
        if (header == HEADER_CMD && received >= 5)
        {
            CommandPacket_t *cmd = (CommandPacket_t*)g_rx_large_buffer;
            process_command_packet(cmd);

            // Update statistics (for main loop debugging)
            memcpy((void*)g_last_rx_packet, cmd, 5);
            g_last_rx_valid = 1;
            g_error_stats.cmd_packet_count++;
        }
        // Data Packet (0xDA, 4 + N*2 bytes)
        else if (header == HEADER_DATA)
        {
            DataPacketHeader_t *hdr = (DataPacketHeader_t*)g_rx_large_buffer;
            uint16_t sample_count = GET_SAMPLE_COUNT(hdr);
            uint16_t expected_size = 4 + (sample_count * 2);

            // Check if all sample data received
            if (received >= expected_size)
            {
                uint16_t *samples = (uint16_t*)(g_rx_large_buffer + 4);
                process_data_packet(hdr, samples);

                // Update statistics (for main loop debugging)
                memcpy((void*)g_last_rx_packet, hdr, 4);
                g_last_rx_valid = 1;
                g_error_stats.data_packet_count++;
            }
            else
            {
                // Incomplete packet - increment error counter (no printf!)
                g_error_stats.spi_error_count++;
            }
        }
        else
        {
            // Unknown header or invalid size - increment error counter (no printf!)
            g_error_stats.spi_error_count++;
        }
    }
    else if (received > 0)
    {
        // Insufficient data - increment error counter (no printf!)
        g_error_stats.spi_error_count++;
    }
    else
    {
        // No data received (received == 0) - DMA didn't start or failed
        g_error_stats.spi_error_count++;
    }

    // 4. Change EXTI back to falling edge (to detect next packet start)
    // Disable rising edge, enable falling edge
    EXTI->RTSR1 &= ~(1U << 15);  // Disable rising trigger
    EXTI->FTSR1 |= (1U << 15);   // Enable falling trigger
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
