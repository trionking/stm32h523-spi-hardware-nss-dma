#include  <errno.h>
#include  <sys/unistd.h> // STDOUT_FILENO, STDERR_FILENO
#include "string.h"
#include "stdio.h"

#define _USE_MATH_DEFINES
#include "math.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "main.h"
#include "user_def.h"
#include "spi_protocol.h"
#include "audio_channel.h"
#include "spi_handler.h"
#include "user_com.h"
#include "stm32h5xx_it.h"  // For DAC DMA debug counters

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart3;
extern DAC_HandleTypeDef hdac1;
extern TIM_HandleTypeDef htim7;
extern SPI_HandleTypeDef hspi1;

extern Queue rx_UART3_queue;
extern Queue tx_UART3_queue;

extern struct uart_Stat_ST uart1_stat_ST;
extern struct uart_Stat_ST uart3_stat_ST;


// ============================================================================
// Audio Buffers (aligned for DMA)
// ============================================================================

// DAC1 (CH0) buffers - placed in non-cacheable RAM_DMA for cache coherency
__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
static uint16_t dac1_buffer_a[AUDIO_BUFFER_SIZE];

__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
static uint16_t dac1_buffer_b[AUDIO_BUFFER_SIZE];

// DAC2 (CH1) buffers - placed in non-cacheable RAM_DMA for cache coherency
__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
static uint16_t dac2_buffer_a[AUDIO_BUFFER_SIZE];

__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
static uint16_t dac2_buffer_b[AUDIO_BUFFER_SIZE];

// Audio channels (global, used by interrupt callbacks)
AudioChannel_t g_dac1_channel;
AudioChannel_t g_dac2_channel;

// ============================================================================
// Legacy Test Code - Sine Wave Lookup Table
// ============================================================================

// 사인파 룩업 테이블 (256 샘플, 12비트: 0-4095)
#define SINE_TABLE_SIZE 256
static uint16_t sine_table[SINE_TABLE_SIZE];

// 사인파 테이블 초기화
void init_sine_table(void)
{
    for (int i = 0; i < SINE_TABLE_SIZE; i++)
    {
        // sin 함수는 -1 ~ 1 범위, 12비트 DAC는 0 ~ 4095 범위
        // 중심값 2048, 진폭 2047
        sine_table[i] = (uint16_t)(2048 + 2047 * sin(2.0 * M_PI * i / SINE_TABLE_SIZE));
    }
}

#ifdef __GNUC__
 #define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
 #define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif

/**
 * @brief Printf character output redirection (UART3 DMA TX)
 * @details Non-blocking printf implementation using Queue + DMA
 *
 * Operation Modes:
 * 1. Queue Uninitialized (buf_size == 0):
 *    - Blocking UART transmission (only during early initialization)
 *    - Prevents NULL pointer crash before init_UART_COM() is called
 *
 * 2. Queue Initialized (buf_size > 0):
 *    - Non-blocking: Enqueue character and return immediately
 *    - DMA transmits queue data in background (via UART3_Process_TX_Queue)
 *
 * 3. Queue Nearly Full:
 *    - Drop character to prevent blocking (maintains real-time performance)
 *
 * @param ch Character to output
 * @return Always returns ch
 */
PUTCHAR_PROTOTYPE
{
	// ========================================================================
	// Case 1: Queue not initialized yet (시스템 시작 초기에만 발생)
	// ========================================================================
	if (tx_UART3_queue.buf_size == 0)
	{
		// Blocking mode: 초기화 과정의 디버그 메시지를 안전하게 출력
		// (init_UART_COM() 호출 전에 printf가 실행되는 경우 대비)
		HAL_UART_Transmit(&huart3, (uint8_t *)&ch, 1, 10);
		return ch;
	}

	// ========================================================================
	// Case 2: Queue 초기화 완료 - Non-blocking 모드 (정상 동작)
	// ========================================================================
	// Queue에 여유 공간이 있으면 문자 저장 (10바이트 여유 확보)
	if (Len_queue(&tx_UART3_queue) < (tx_UART3_queue.buf_size - 10))
	{
		Enqueue(&tx_UART3_queue, (uint8_t)ch);
	}
	// Queue가 거의 가득 차면 문자 버림 (블로킹 방지)
	// → SPI 수신 등 실시간 작업에 영향 없음

	// 즉시 복귀 (DMA가 백그라운드에서 전송 처리)
	return ch;
}

int _write(int file, char *data, int len)
{
    int bytes_written;

    if ((file != STDOUT_FILENO) && (file != STDERR_FILENO))
    {
        errno = EBADF;
        return -1;
    }

    for (bytes_written = 0; bytes_written < len; bytes_written++)
    {
        __io_putchar(*data);
        data++;
    }

    return bytes_written;
}


void test_gpio(void)
{
    uint32_t tick_user;

    printf("\r\n=== Legacy GPIO Test ===\r\n");
    printf("Press ESC or 'q' to exit\r\n\r\n");

    tick_user = HAL_GetTick();

    while(1)
    {
        // ESC 또는 'q' 키 입력 체크
        if (Len_queue(&rx_UART3_queue) > 0)
        {
            uint8_t key = Dequeue(&rx_UART3_queue);
            if (key == 0x1B || key == 'q' || key == 'Q')
            {
                printf("\r\n[EXIT] GPIO Test stopped by user\r\n");
                return;
            }
        }

        if (HAL_GetTick() - tick_user > 500)
        {
            tick_user = HAL_GetTick();
            HAL_GPIO_TogglePin(OT_LD_SYS_GPIO_Port, OT_LD_SYS_Pin);
            //HAL_GPIO_TogglePin(OT_LD_REV_GPIO_Port, OT_LD_REV_Pin);

            printf("GPIO Toggled\r\n");
        }
    }

}

// DAC 사인파 출력 테스트 함수
// 채널 1: 10kHz 사인파
// 채널 2: 5kHz 사인파
void test_dac_sine(void)
{
    // 샘플링 주파수: 100kHz (10us per sample)
    // 10kHz: 10 샘플/주기 -> 인덱스 증가량 = 256/10 = 25.6
    // 5kHz: 20 샘플/주기 -> 인덱스 증가량 = 256/20 = 12.8

    // 고정소수점 연산 (8비트 소수부)
    uint32_t phase_10khz = 0;  // 10kHz 위상 누산기
    uint32_t phase_5khz = 0;   // 5kHz 위상 누산기
    uint32_t phase_inc_10khz = (uint32_t)(25.6 * 256);  // 25.6 << 8
    uint32_t phase_inc_5khz = (uint32_t)(12.8 * 256);   // 12.8 << 8

    uint16_t index_10khz, index_5khz;
    uint32_t tick_user;
    uint32_t sample_count = 0;

    // DAC 시작
    HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
    HAL_DAC_Start(&hdac1, DAC_CHANNEL_2);

    printf("DAC Sine Wave Test Started\r\n");
    printf("Channel 1: 10kHz, Channel 2: 5kHz\r\n");
    printf("Press ESC or 'q' to exit\r\n\r\n");

    tick_user = HAL_GetTick();

    while(1)
    {
        // ESC 또는 'q' 키 입력 체크 (1000 샘플마다 체크)
        if (sample_count % 1000 == 0 && Len_queue(&rx_UART3_queue) > 0)
        {
            uint8_t key = Dequeue(&rx_UART3_queue);
            if (key == 0x1B || key == 'q' || key == 'Q')
            {
                printf("\r\n[EXIT] DAC Sine Test stopped by user\r\n");
                HAL_DAC_Stop(&hdac1, DAC_CHANNEL_1);
                HAL_DAC_Stop(&hdac1, DAC_CHANNEL_2);
                return;
            }
        }

        // 채널 1: 10kHz 사인파
        index_10khz = (phase_10khz >> 8) & 0xFF;  // 상위 8비트를 인덱스로 사용
        HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, sine_table[index_10khz]);

        // 채널 2: 5kHz 사인파
        index_5khz = (phase_5khz >> 8) & 0xFF;
        HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, sine_table[index_5khz]);

        // 위상 누산
        phase_10khz += phase_inc_10khz;
        phase_5khz += phase_inc_5khz;

        // 위상 오버플로우 처리 (256 * 256 = 65536)
        if (phase_10khz >= (SINE_TABLE_SIZE << 8))
            phase_10khz -= (SINE_TABLE_SIZE << 8);
        if (phase_5khz >= (SINE_TABLE_SIZE << 8))
            phase_5khz -= (SINE_TABLE_SIZE << 8);

        sample_count++;

        // 1초마다 상태 출력
        if (HAL_GetTick() - tick_user >= 1000)
        {
            tick_user = HAL_GetTick();
            printf("DAC Running - %lu samples/sec\r\n", sample_count);
            sample_count = 0;
            HAL_GPIO_TogglePin(OT_LD_SYS_GPIO_Port, OT_LD_SYS_Pin);
        }

        // 샘플링 주파수 100kHz = 10us 딜레이
        // 정확한 타이밍을 위해서는 타이머 인터럽트 사용 권장
        // 여기서는 간단하게 루프로 구현 (실제 주파수는 다를 수 있음)
        for (volatile int i = 0; i < 250; i++);  // 대략적인 딜레이 (250MHz 기준)
    }
}

// ============================================================================
// Phase 1: Hardware Verification Tests
// ============================================================================

// Test 1: LED 블링크 테스트
void test_led_blink(void)
{
    printf("\r\n=== Phase 1-1: LED Blink Test ===\r\n");
    printf("Testing both LEDs (1Hz toggle)\r\n");
    printf("Press ESC or 'q' to exit\r\n\r\n");

    uint32_t tick_start = HAL_GetTick();

    while(1)
    {
        // ESC 또는 'q' 키 입력 체크
        if (Len_queue(&rx_UART3_queue) > 0)
        {
            uint8_t key = Dequeue(&rx_UART3_queue);
            if (key == 0x1B || key == 'q' || key == 'Q')  // ESC or q/Q
            {
                printf("\r\n[EXIT] LED Blink Test stopped by user\r\n");
                return;
            }
        }

        if (HAL_GetTick() - tick_start >= 500)
        {
            tick_start = HAL_GetTick();

            // 두 LED를 번갈아가며 토글
            HAL_GPIO_TogglePin(OT_LD_SYS_GPIO_Port, OT_LD_SYS_Pin);
            HAL_GPIO_TogglePin(OT_LD_REV_GPIO_Port, OT_LD_REV_Pin);

            // 상태 출력
            GPIO_PinState sys_state = HAL_GPIO_ReadPin(OT_LD_SYS_GPIO_Port, OT_LD_SYS_Pin);
            GPIO_PinState rev_state = HAL_GPIO_ReadPin(OT_LD_REV_GPIO_Port, OT_LD_REV_Pin);

            printf("LED_SYS: %s, LED_REV: %s\r\n",
                   sys_state ? "ON" : "OFF",
                   rev_state ? "ON" : "OFF");
        }
    }
}

// Test 2: 사이렌 소리 테스트
void test_dac_sine_wave(void)
{
    printf("\r\n[ENTRY] test_dac_sine_wave() called\r\n");

    printf("\r\n=== Siren Sound Test ===\r\n");
    printf("DAC1_CH1 (PA4): Siren sound (500Hz~2kHz sweep)\r\n");
    printf("DAC1_CH2 (PA5): Siren sound (inverted phase)\r\n");
    printf("Press ESC or 'q' to exit\r\n\r\n");

    printf("[DIAG] About to read DAC registers...\r\n");

    // DIAGNOSTIC: Check DAC status before starting
    printf("[DIAG] DAC CR before start: 0x%08lX\r\n", DAC1->CR);
    printf("[DIAG] DAC SR before start: 0x%08lX\r\n", DAC1->SR);
    printf("[DIAG] DAC MCR: 0x%08lX\r\n", DAC1->MCR);

    // FIX: Reconfigure DAC to NO TRIGGER mode for manual control
    printf("[FIX] Reconfiguring DAC to NO TRIGGER mode...\r\n");
    DAC_ChannelConfTypeDef sConfig = {0};
    sConfig.DAC_HighFrequency = DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC;
    sConfig.DAC_Trigger = DAC_TRIGGER_NONE;  // No trigger - manual control
    sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
    sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_EXTERNAL;
    sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;

    if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1) != HAL_OK) {
        printf("[ERROR] DAC CH1 config failed!\r\n");
        return;
    }
    if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_2) != HAL_OK) {
        printf("[ERROR] DAC CH2 config failed!\r\n");
        return;
    }
    printf("[FIX] DAC reconfigured successfully\r\n");

    // 정현파 테이블 생성 (256 샘플)
    uint16_t sine_256[256];
    for (int i = 0; i < 256; i++)
    {
        sine_256[i] = (uint16_t)(2048 + 2047 * sin(2.0 * M_PI * i / 256));
    }

    // DAC 시작
    HAL_StatusTypeDef status1 = HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
    HAL_StatusTypeDef status2 = HAL_DAC_Start(&hdac1, DAC_CHANNEL_2);

    printf("[DIAG] HAL_DAC_Start CH1: %d, CH2: %d (0=OK)\r\n", status1, status2);
    printf("[DIAG] DAC CR after start: 0x%08lX\r\n", DAC1->CR);
    printf("[DIAG] DAC SR after start: 0x%08lX\r\n", DAC1->SR);

    uint32_t tick_start = HAL_GetTick();
    uint32_t sample_count = 0;

    // 사이렌 주파수 제어 (500Hz ~ 2000Hz 스윕)
    float frequency = 500.0f;  // 시작 주파수
    float freq_direction = 1.0f;  // 1: 올라가는 중, -1: 내려가는 중
    float phase = 0.0f;
    const float freq_step = 0.025f;  // 주파수 변화 속도 (약 1.5초에 걸쳐 변화)
    const float sample_rate = 32000.0f;  // 32kHz 샘플레이트

    printf("Siren sound started (500Hz~2kHz sweep)\r\n");

    while(1)
    {
        // ESC 또는 'q' 키 입력 체크 (1000 샘플마다 체크)
        if (sample_count % 1000 == 0 && Len_queue(&rx_UART3_queue) > 0)
        {
            uint8_t key = Dequeue(&rx_UART3_queue);
            if (key == 0x1B || key == 'q' || key == 'Q')
            {
                printf("\r\n[EXIT] Siren Sound Test stopped by user\r\n");
                HAL_DAC_Stop(&hdac1, DAC_CHANNEL_1);
                HAL_DAC_Stop(&hdac1, DAC_CHANNEL_2);

                // RESTORE: Reconfigure DAC back to TIM1 trigger mode for DMA operation
                printf("[RESTORE] Reconfiguring DAC to TIM1 TRIGGER mode...\r\n");
                DAC_ChannelConfTypeDef sConfig_restore = {0};
                sConfig_restore.DAC_HighFrequency = DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC;
                sConfig_restore.DAC_Trigger = DAC_TRIGGER_T1_TRGO;  // TIM1 trigger
                sConfig_restore.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
                sConfig_restore.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_EXTERNAL;
                sConfig_restore.DAC_UserTrimming = DAC_TRIMMING_FACTORY;

                if (HAL_DAC_ConfigChannel(&hdac1, &sConfig_restore, DAC_CHANNEL_1) != HAL_OK) {
                    printf("[ERROR] DAC CH1 restore failed!\r\n");
                }
                if (HAL_DAC_ConfigChannel(&hdac1, &sConfig_restore, DAC_CHANNEL_2) != HAL_OK) {
                    printf("[ERROR] DAC CH2 restore failed!\r\n");
                }
                printf("[RESTORE] DAC restored to TIM1 trigger mode\r\n");

                return;
            }
        }

        // 위상을 테이블 인덱스로 변환
        uint16_t index = (uint16_t)phase & 0xFF;

        // CH1: 사이렌 소리
        HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, sine_256[index]);

        // CH2: 역위상 사이렌 (스테레오 효과)
        uint16_t index_inv = (index + 128) & 0xFF;
        HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, sine_256[index_inv]);

        // 위상 증가 (주파수에 비례)
        phase += (frequency * 256.0f) / sample_rate;
        if (phase >= 256.0f)
            phase -= 256.0f;

        // 주파수 스윕 (500Hz ~ 2000Hz)
        frequency += freq_direction * freq_step;
        if (frequency >= 2000.0f)
        {
            frequency = 2000.0f;
            freq_direction = -1.0f;  // 내려가기 시작
        }
        else if (frequency <= 500.0f)
        {
            frequency = 500.0f;
            freq_direction = 1.0f;   // 올라가기 시작
        }

        sample_count++;

        // 1초마다 상태 출력
        if (HAL_GetTick() - tick_start >= 1000)
        {
            tick_start = HAL_GetTick();
            printf("Siren: %.0fHz | Samples: %lu/sec\r\n", frequency, sample_count);
            sample_count = 0;
            HAL_GPIO_TogglePin(OT_LD_SYS_GPIO_Port, OT_LD_SYS_Pin);
        }

        // 대략 32kHz 샘플레이트
        for (volatile int i = 0; i < 100; i++);
    }
}

// Test 3: DAC DMA Sine Wave Test (버퍼를 사인파로 채우기)
void test_dac_dma_sine(void)
{
    printf("\r\n=== DAC DMA Sine Wave Test (5 seconds) ===\r\n");
    printf("DAC1 (PA4): 1kHz sine wave\r\n");
    printf("DAC2 (PA5): 500Hz sine wave\r\n");
    printf("Playing for 5 seconds...\r\n\r\n");

    // DAC1 버퍼를 1kHz 사인파로 채우기
    // 샘플레이트: 32kHz, 1kHz = 32 샘플/주기
    for (int i = 0; i < AUDIO_BUFFER_SIZE; i++)
    {
        uint16_t table_index = (i * SINE_TABLE_SIZE / 32) % SINE_TABLE_SIZE;
        dac1_buffer_a[i] = sine_table[table_index];
        dac1_buffer_b[i] = sine_table[table_index];
    }

    // DAC2 버퍼를 500Hz 사인파로 채우기 (다른 주파수)
    // 500Hz = 64 샘플/주기
    for (int i = 0; i < AUDIO_BUFFER_SIZE; i++)
    {
        uint16_t table_index = (i * SINE_TABLE_SIZE / 64) % SINE_TABLE_SIZE;
        dac2_buffer_a[i] = sine_table[table_index];
        dac2_buffer_b[i] = sine_table[table_index];
    }

    printf("[INIT] Buffers filled with sine waves\r\n");

    // Start TIM1 (DAC trigger, 32kHz)
    extern TIM_HandleTypeDef htim1;
    HAL_TIM_Base_Start(&htim1);
    printf("[INIT] TIM1 started (32kHz trigger)\r\n");

    // Start DAC DMA for both channels
    HAL_StatusTypeDef status1 = HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1,
                                                   (uint32_t*)dac1_buffer_a,
                                                   AUDIO_BUFFER_SIZE,
                                                   DAC_ALIGN_12B_R);

    HAL_StatusTypeDef status2 = HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_2,
                                                   (uint32_t*)dac2_buffer_a,
                                                   AUDIO_BUFFER_SIZE,
                                                   DAC_ALIGN_12B_R);

    if (status1 == HAL_OK && status2 == HAL_OK)
    {
        printf("[PLAY] DAC1 & DAC2 DMA started successfully\r\n");
        printf("[PLAY] Playing...\r\n");

        // Play for 5 seconds
        HAL_Delay(5000);

        // Stop DAC DMA
        HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_1);
        HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_2);

        printf("\r\n[STOP] Playback stopped after 5 seconds\r\n");
        printf("Test completed.\r\n");
    }
    else
    {
        printf("[ERROR] DAC DMA start failed: CH1=%d, CH2=%d\r\n", status1, status2);
    }
}

// Test 4: DAC Quick Test (5초 출력)
void test_dac_quick(void)
{
    printf("\r\n=== DAC Quick Test (5 seconds) ===\r\n");
    printf("DAC1_CH1 (PA4): 1kHz sine wave for 5 seconds\r\n");
    printf("Check output with oscilloscope\r\n\r\n");

    // 정현파 테이블 생성 (32 샘플)
    uint16_t sine_32[32];
    for (int i = 0; i < 32; i++)
    {
        sine_32[i] = (uint16_t)(2048 + 2047 * sin(2.0 * M_PI * i / 32));
    }

    // DAC 시작
    HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);

    uint32_t tick_start = HAL_GetTick();
    uint32_t index = 0;
    uint32_t sample_count = 0;

    printf("DAC output started...\r\n");

    // 5초간 실행
    while((HAL_GetTick() - tick_start) < 5000)
    {
        // CH1: 1kHz (32kHz / 32 = 1kHz)
        HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, sine_32[index]);
        index = (index + 1) % 32;
        sample_count++;

        // 대략 32kHz 샘플레이트 (250MHz / 약 7800 = 32kHz)
        for (volatile int i = 0; i < 100; i++);

        // LED 토글 (1초마다)
        if (sample_count % 32000 == 0)
        {
            HAL_GPIO_TogglePin(OT_LD_SYS_GPIO_Port, OT_LD_SYS_Pin);
        }
    }

    // DAC 정지
    HAL_DAC_Stop(&hdac1, DAC_CHANNEL_1);

    printf("DAC output stopped.\r\n");
    printf("Total samples: %lu (approx. %lu samples/sec)\r\n", sample_count, sample_count / 5);
    printf("\r\nTest completed. Returning to menu...\r\n");
}

// Test 4: UART 하드웨어 루프백 테스트
void test_uart_loopback(void)
{
    printf("\r\n=== UART Hardware Loopback Test ===\r\n");
    printf("Short PB10 (TX) and PB1 (RX) together with a wire!\r\n");
    printf("Press any key to start...\r\n\r\n");

    // 키 입력 대기
    while(!__HAL_UART_GET_FLAG(&huart3, UART_FLAG_RXNE))
    {
        HAL_Delay(10);
    }
    (void)(huart3.Instance->RDR);  // 버퍼 클리어

    printf("Starting loopback test...\r\n\r\n");

    uint32_t success = 0;
    uint32_t fail = 0;

    for(uint8_t test_char = 'A'; test_char <= 'Z'; test_char++)
    {
        // TX로 문자 전송
        HAL_UART_Transmit(&huart3, &test_char, 1, 100);

        // RX로 수신 대기 (100ms)
        uint32_t timeout = HAL_GetTick();
        uint8_t rx_char = 0;
        uint8_t received = 0;

        while((HAL_GetTick() - timeout) < 100)
        {
            if(__HAL_UART_GET_FLAG(&huart3, UART_FLAG_RXNE))
            {
                rx_char = huart3.Instance->RDR & 0xFF;
                received = 1;
                break;
            }
        }

        if(received && (rx_char == test_char))
        {
            printf("✓ TX='%c' RX='%c' - OK\r\n", test_char, rx_char);
            success++;
        }
        else
        {
            printf("✗ TX='%c' RX='%c' (0x%02X) - FAIL\r\n", test_char, rx_char, rx_char);
            fail++;
        }

        HAL_Delay(50);
    }

    printf("\r\n=== Test Results ===\r\n");
    printf("Success: %lu\r\n", success);
    printf("Fail: %lu\r\n", fail);

    if(success == 26 && fail == 0)
    {
        printf("\r\n** UART HARDWARE IS WORKING! **\r\n");
        printf("** Problem is likely with your terminal connection **\r\n");
    }
    else
    {
        printf("\r\n** UART HARDWARE PROBLEM **\r\n");
        printf("Check UART3 GPIO configuration (PB1/PB10)\r\n");
    }

    printf("\r\nReturning to menu...\r\n");
    HAL_Delay(3000);
}

// Test 5: UART Echo 테스트
void test_uart_echo(void)
{
    printf("\r\n=== UART Echo Test ===\r\n");
    printf("Type any character and it will be echoed back\r\n");
    printf("Press ESC to exit\r\n\r\n");

    uint8_t rxChar;
    uint32_t char_count = 0;

    while(1)
    {
        // 무한 대기로 문자 받기
        HAL_StatusTypeDef status = HAL_UART_Receive(&huart3, &rxChar, 1, HAL_MAX_DELAY);

        if (status == HAL_OK)
        {
            char_count++;

            // ESC (0x1B) 입력 시 종료
            if (rxChar == 0x1B)
            {
                printf("\r\n\r\nEcho test ended. Received %lu characters.\r\n", char_count);
                break;
            }

            // 받은 문자 에코백
            printf("RX: '%c' (0x%02X)\r\n", rxChar, rxChar);

            // LED 토글
            HAL_GPIO_TogglePin(OT_LD_SYS_GPIO_Port, OT_LD_SYS_Pin);
        }
        else
        {
            printf("[ERROR] UART receive failed: %d\r\n", status);
            break;
        }
    }

    printf("Returning to menu...\r\n");
    HAL_Delay(1000);
}

// Test 5: SPI 통신 테스트
void test_spi_communication(void)
{
    printf("\r\n=== SPI Communication Test ===\r\n");
    UART3_Process_TX_Queue();  // 즉시 출력
    HAL_Delay(10);

    printf("Protocol v1.2 - Slave ID removed (CS pin selection)\r\n");
    printf("Phase 2-2: Data Packet + DAC Output Test\r\n\r\n");
    UART3_Process_TX_Queue();  // 즉시 출력
    HAL_Delay(10);

    printf("IMPORTANT: Master must send SPI packets!\r\n");
    printf("- Refer to SLAVE_DATA_PACKET_TEST_GUIDE_20251107.md\r\n");
    printf("- Master should send: SPITEST DATA 0\r\n\r\n");
    UART3_Process_TX_Queue();  // 즉시 출력
    HAL_Delay(10);

    printf("Waiting for SPI packets from Master...\r\n");
    printf("Press ESC or 'q' to exit\r\n\r\n");
    UART3_Process_TX_Queue();  // 즉시 출력
    HAL_Delay(10);

    // SPI 수신 버퍼
    uint8_t rx_buffer[10];
    uint32_t packet_count = 0;
    uint32_t cmd_packet_count = 0;
    uint32_t data_packet_count = 0;
    uint32_t error_count = 0;
    uint32_t timeout_count = 0;
    uint32_t tick_start = HAL_GetTick();

    // TIM7 시작 (DAC 트리거용, 32kHz)
    HAL_TIM_Base_Start(&htim7);
    printf("[INIT] TIM7 started for DAC trigger (32kHz)\r\n");
    UART3_Process_TX_Queue();
    HAL_Delay(10);

    // RDY 핀 LOW (준비됨, Active Low)
    HAL_GPIO_WritePin(OT_nRDY_GPIO_Port, OT_nRDY_Pin, GPIO_PIN_RESET);
    printf("[INIT] RDY pin set LOW (ready)\r\n");
    UART3_Process_TX_Queue();
    HAL_Delay(10);

    printf("\r\n[READY] Slave is now ready to receive SPI packets\r\n");
    printf("[READY] Listening on SPI1...\r\n\r\n");
    UART3_Process_TX_Queue();
    HAL_Delay(10);

    while(1)
    {
        // SPI 수신에만 집중! (UART 처리는 패킷 수신 후에만)

        // SPI 헤더 1바이트 수신 (타임아웃 10ms)
        HAL_StatusTypeDef status = HAL_SPI_Receive(&hspi1, rx_buffer, 1, 10);

        if (status == HAL_OK)
        {
            uint8_t header = rx_buffer[0];

            // Command Packet (0xC0)
            if (header == HEADER_CMD)
            {
                // 나머지 5바이트 수신 (총 6바이트, 타임아웃 500ms로 증가)
                status = HAL_SPI_Receive(&hspi1, &rx_buffer[1], 5, 500);
                if (status == HAL_OK)
                {
                    CommandPacket_t *cmd = (CommandPacket_t *)rx_buffer;
                    packet_count++;
                    cmd_packet_count++;

                    printf("[CMD #%lu] Ch=%d, Cmd=0x%02X, Param=%d\r\n",
                           cmd_packet_count,
                           cmd->channel,
                           cmd->command,
                           GET_PARAM(cmd));

                    // Hardware CS already selected this slave
                    printf("  -> ");
                    switch(cmd->command)
                    {
                        case CMD_PLAY:
                            printf("PLAY command\r\n");
                            break;
                        case CMD_STOP:
                            printf("STOP command\r\n");
                            break;
                        case CMD_VOLUME:
                            printf("VOLUME command (vol=%d)\r\n", GET_PARAM(cmd));
                            break;
                        case CMD_RESET:
                            printf("RESET command\r\n");
                            break;
                        default:
                            printf("Unknown command\r\n");
                    }

                    // 명령 패킷 처리 완료, UART 출력
                    UART3_Process_TX_Queue();
                }
                else
                {
                    error_count++;
                    printf("[ERROR] Received CMD header (0xC0) but body failed (status=%d)\r\n", status);
                    UART3_Process_TX_Queue();
                }
            }
            // Data Packet (0xDA)
            else if (header == HEADER_DATA)
            {
                // 나머지 4바이트 헤더 수신
                status = HAL_SPI_Receive(&hspi1, &rx_buffer[1], 4, 100);
                if (status == HAL_OK)
                {
                    DataPacketHeader_t *hdr = (DataPacketHeader_t *)rx_buffer;
                    uint16_t sample_count = GET_SAMPLE_COUNT(hdr);
                    packet_count++;
                    data_packet_count++;

                    printf("[DATA #%lu] Ch=%d, Samples=%d\r\n",
                           data_packet_count,
                           hdr->channel,
                           sample_count);

                    // Hardware CS already selected this slave
                    printf("  -> (%d bytes audio data)\r\n", sample_count * 2);

                    // 샘플 데이터 버리기 (읽지만 처리하지 않음)
                    uint8_t dummy[256];
                    uint16_t bytes_to_read = sample_count * 2;
                    while (bytes_to_read > 0)
                    {
                        uint16_t chunk = (bytes_to_read > 256) ? 256 : bytes_to_read;
                        HAL_SPI_Receive(&hspi1, dummy, chunk, 1000);
                        bytes_to_read -= chunk;
                    }

                    // 데이터 패킷 처리 완료, UART 출력
                    UART3_Process_TX_Queue();
                }
                else
                {
                    error_count++;
                    printf("[ERROR] Failed to receive DATA packet header (status=%d)\r\n", status);
                    UART3_Process_TX_Queue();
                }
            }
            // Unknown Header (0xFF는 노이즈이므로 조용히 무시)
            else if (header != 0xFF)
            {
                error_count++;
                printf("[ERROR] Unknown header: 0x%02X\r\n", header);
                UART3_Process_TX_Queue();
            }

            // LED 토글 (패킷 수신 시에만)
            if (header == HEADER_CMD || header == HEADER_DATA)
            {
                HAL_GPIO_TogglePin(OT_LD_SYS_GPIO_Port, OT_LD_SYS_Pin);
            }
        }
        else if (status != HAL_TIMEOUT)
        {
            // 타임아웃이 아닌 실제 SPI 에러
            error_count++;
            printf("[ERROR] SPI receive error (status=%d)\r\n", status);
            UART3_Process_TX_Queue();  // 에러 메시지만 즉시 출력
        }
        // HAL_TIMEOUT은 조용히 무시 (정상, Master가 아직 전송 안 함)

        // ESC 키 체크 (10ms마다 한 번만)
        static uint32_t last_key_check = 0;
        if ((HAL_GetTick() - last_key_check) >= 10)
        {
            last_key_check = HAL_GetTick();

            if (Len_queue(&rx_UART3_queue) > 0)
            {
                uint8_t key = Dequeue(&rx_UART3_queue);
                if (key == 0x1B || key == 'q' || key == 'Q')
                {
                    printf("\r\n[EXIT] SPI Test stopped. Total=%lu, CMD=%lu, DATA=%lu\r\n",
                           packet_count, cmd_packet_count, data_packet_count);
                    UART3_Process_TX_Queue();
                    HAL_Delay(100);
                    return;
                }
            }
        }
    }
}

// Test 6: RDY 핀 토글 테스트
void test_rdy_pin(void)
{
    printf("\r\n=== Phase 1-4: RDY Pin Toggle Test ===\r\n");
    printf("RDY Pin (PA8) toggling at 1Hz\r\n");
    printf("Check with oscilloscope or LED\r\n");
    printf("Press ESC or 'q' to exit\r\n\r\n");

    // RDY 핀을 출력으로 재설정 (테스트용)
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = OT_nRDY_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(OT_nRDY_GPIO_Port, &GPIO_InitStruct);

    // 초기값 LOW
    HAL_GPIO_WritePin(OT_nRDY_GPIO_Port, OT_nRDY_Pin, GPIO_PIN_RESET);

    printf("RDY pin configured as output\r\n");

    uint32_t tick_start = HAL_GetTick();
    uint32_t toggle_count = 0;

    while(1)
    {
        // ESC 또는 'q' 키 입력 체크
        if (Len_queue(&rx_UART3_queue) > 0)
        {
            uint8_t key = Dequeue(&rx_UART3_queue);
            if (key == 0x1B || key == 'q' || key == 'Q')
            {
                printf("\r\n[EXIT] RDY Pin Test stopped by user\r\n");
                return;
            }
        }

        if (HAL_GetTick() - tick_start >= 100)
        {
            tick_start = HAL_GetTick();

            // RDY 핀 토글
            HAL_GPIO_TogglePin(OT_nRDY_GPIO_Port, OT_nRDY_Pin);
            HAL_GPIO_TogglePin(OT_LD_SYS_GPIO_Port, OT_LD_SYS_Pin);

            toggle_count++;

            GPIO_PinState rdy_state = HAL_GPIO_ReadPin(OT_nRDY_GPIO_Port, OT_nRDY_Pin);

            if (toggle_count % 10 == 0)
            {
                printf("RDY: %s (toggles: %lu)\r\n",
                       rdy_state ? "HIGH" : "LOW",
                       toggle_count);
            }
        }
    }
}

// ============================================================================
// Test Menu System
// ============================================================================

// ============================================================================
// Slave Mode Functions
// ============================================================================

/**
 * @brief Initialize and run slave mode
 * @note This is the main application mode for audio streaming
 */
void run_slave_mode(void)
{
    printf("\r\n");
    printf("========================================\r\n");
    printf("  STM32H523 Slave MCU - Audio Streaming\r\n");
    printf("========================================\r\n");
    printf("Protocol v2.0 - Hardware CS selection\r\n");
    printf("Channels: DAC1 (CH0), DAC2 (CH1)\r\n");
    printf("Sample Rate: 32kHz\r\n");
    printf("Buffer Size: %d samples (64ms)\r\n", AUDIO_BUFFER_SIZE);
    printf("========================================\r\n\r\n");

    // Initialize audio channels
    audio_channel_init(&g_dac1_channel, dac1_buffer_a, dac1_buffer_b);
    audio_channel_init(&g_dac2_channel, dac2_buffer_a, dac2_buffer_b);
    printf("[INIT] Audio channels initialized\r\n");

    // Initialize SPI handler
    spi_handler_init(&hspi1, &g_dac1_channel, &g_dac2_channel);
    printf("[INIT] SPI handler initialized\r\n");

    // Note: EXTI for PA15 (CS pin) is already configured by CubeMX
    // No need to call spi_handler_init_nss_exti() anymore

    // Start SPI reception
    spi_handler_start();
    printf("[INIT] SPI reception started\r\n");

    printf("\r\n** Slave ready - waiting for Master commands **\r\n");
    printf("** Press ESC or 'q' to exit to menu **\r\n\r\n");

    // Main loop - monitor status
    uint32_t last_status_tick = 0;
    uint32_t led_toggle_tick = 0;

    while(1)
    {
        uint32_t now = HAL_GetTick();

        // Process TX queue for non-blocking printf (DMA-based)
        UART3_Process_TX_Queue();

        // ESC 또는 'q' 키 입력 체크
        if (Len_queue(&rx_UART3_queue) > 0)
        {
            uint8_t key = Dequeue(&rx_UART3_queue);
            if (key == 0x1B || key == 'q' || key == 'Q')
            {
                printf("\r\n[EXIT] Slave Mode stopped by user\r\n");
                return;
            }
        }

        // Toggle LED to show alive
        if ((now - led_toggle_tick) >= 500)
        {
            led_toggle_tick = now;
            HAL_GPIO_TogglePin(OT_LD_SYS_GPIO_Port, OT_LD_SYS_Pin);
        }

        // Print status every 10 seconds (reduced frequency to avoid blocking SPI)
        if ((now - last_status_tick) >= 10000)
        {
            last_status_tick = now;

            // Get statistics
            uint32_t dac1_samples, dac1_swaps, dac1_underruns;
            uint32_t dac2_samples, dac2_swaps, dac2_underruns;

            audio_channel_get_stats(&g_dac1_channel, &dac1_samples, &dac1_swaps, &dac1_underruns);
            audio_channel_get_stats(&g_dac2_channel, &dac2_samples, &dac2_swaps, &dac2_underruns);

            SPI_ErrorStats_t spi_errors;
            spi_handler_get_errors(&spi_errors);

            printf("\r\n[STATUS] --------------------\r\n");
            printf("DAC1: %s | Samples: %lu | Swaps: %lu | Underruns: %lu\r\n",
                   g_dac1_channel.is_playing ? "PLAY" : "STOP",
                   dac1_samples, dac1_swaps, dac1_underruns);
            printf("  DMA IRQ: HalfCplt=%lu | Cplt=%lu\r\n",
                   g_dac1_half_cplt_count, g_dac1_cplt_count);
            printf("DAC2: %s | Samples: %lu | Swaps: %lu | Underruns: %lu\r\n",
                   g_dac2_channel.is_playing ? "PLAY" : "STOP",
                   dac2_samples, dac2_swaps, dac2_underruns);
            printf("  DMA IRQ: HalfCplt=%lu | Cplt=%lu\r\n",
                   g_dac2_half_cplt_count, g_dac2_cplt_count);
            printf("SPI:  CS_Fall: %lu | CS_Rise: %lu\r\n",
                   spi_errors.cs_falling_count,
                   spi_errors.cs_rising_count);
            printf("      CMD: %lu | DATA: %lu | Errors: %lu\r\n",
                   spi_errors.cmd_packet_count,
                   spi_errors.data_packet_count,
                   spi_errors.spi_error_count);
            printf("      Last RX: %lu bytes | DMA Fail: %lu\r\n",
                   spi_errors.last_received_bytes,
                   spi_errors.dma_start_fail_count);
            printf("      SPI State: 0x%02X | Last Fail State: 0x%02lX\r\n",
                   (unsigned int)hspi1.State,
                   spi_errors.last_spi_state);

            // Show last received packet
            uint8_t last_pkt[5];
            if (spi_handler_get_last_packet(last_pkt))
            {
                printf("LAST_RX: %02X %02X %02X %02X %02X\r\n",
                       last_pkt[0], last_pkt[1], last_pkt[2], last_pkt[3], last_pkt[4]);
            }

            printf("----------------------------\r\n");
        }

        // Low power mode (wake on interrupt)
        // HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    }
}

// ============================================================================
// Test Menu System
// ============================================================================

void show_test_menu(void)
{
    printf("\r\n");
    printf("========================================\r\n");
    printf("  STM32H523 Slave - Test Menu\r\n");
    printf("========================================\r\n");
    printf("0. Run Slave Mode (Main Application)\r\n");
    printf("1. LED Blink Test\r\n");
    printf("2. Siren Sound Test (DAC)\r\n");
    printf("3. DAC Quick Test (1kHz, 5 sec)\r\n");
    printf("4. RDY Pin Toggle Test\r\n");
    printf("5. SPI Communication Test\r\n");
    printf("6. DAC DMA Sine Wave Test (5 sec playback)\r\n");
    printf("----------------------------------------\r\n");
    printf("Select test (0-6): ");
}

void run_test_menu(void)
{
    COM_Idy_Typ rcv_rtn_stat = NOT_LINE;
    char rcv_cmd[20] = "";
    uint32_t led_toggle = 0;

    // 처음 한 번만 메뉴 출력
    show_test_menu();
    printf("\r\nType 'help' to show menu again.\r\n");
    printf("Ready for commands...\r\n\r\n");

    while(1)
    {
        // Process TX queue for non-blocking printf (DMA-based)
        UART3_Process_TX_Queue();

        // UART3 IDLE 인터럽트로 수신된 명령어 확인
        rcv_rtn_stat = UART3_GetLine(uart3_stat_ST.rcv_line_buf);

        if (rcv_rtn_stat == RCV_LINE)
        {
            // 수신된 명령어 파싱
            printf("[DEBUG] Raw RX: '%s' (len=%d)\r\n", uart3_stat_ST.rcv_line_buf, strlen((char*)uart3_stat_ST.rcv_line_buf));
            sscanf((char *)uart3_stat_ST.rcv_line_buf, "%s", rcv_cmd);
            printf("[DEBUG] Parsed: '%s' (len=%d)\r\n", rcv_cmd, strlen(rcv_cmd));

            // 빈 명령어 무시
            if (strlen(rcv_cmd) == 0)
            {
                printf("[DEBUG] Empty command, skipping\r\n");
                continue;
            }

            // help 명령어 - 메뉴 재출력
            if (strcmp(rcv_cmd, "help") == 0)
            {
                show_test_menu();
                printf("\r\nReady for commands...\r\n\r\n");
            }
            // 테스트 명령어 (0-6)
            else if (strlen(rcv_cmd) == 1 && rcv_cmd[0] >= '0' && rcv_cmd[0] <= '6')
            {
                printf("[DEBUG] Received command: '%c'\r\n", rcv_cmd[0]);
                switch(rcv_cmd[0])
                {
                    case '0':
                        printf("[CMD] 0: Run Slave Mode (Main Application)\r\n");
                        run_slave_mode();
                        break;

                    case '1':
                        printf("[CMD] 1: LED Blink Test\r\n");
                        test_led_blink();
                        break;

                    case '2':
                        printf("[CMD] 2: Siren Sound Test (DAC)\r\n");
                        test_dac_sine_wave();
                        break;

                    case '3':
                        printf("[CMD] 3: DAC Quick Test (1kHz, 5 sec)\r\n");
                        test_dac_quick();
                        break;

                    case '4':
                        printf("[CMD] 4: RDY Pin Toggle Test\r\n");
                        test_rdy_pin();
                        break;

                    case '5':
                        printf("[CMD] 5: SPI Communication Test\r\n");
                        test_spi_communication();
                        break;

                    case '6':
                        printf("[CMD] 6: DAC DMA Sine Wave Test\r\n");
                        test_dac_dma_sine();
                        break;
                }

                // 테스트 종료 후 안내 메시지
                printf("\r\nTest completed. Type 'help' for menu.\r\n\r\n");
            }
            // stvc 명령어 - 속도 제어 (dev_num, dir, speed)
            else if (strncmp(rcv_cmd, "stvc", 4) == 0)
            {
                char rcv_dummy_data[3][20];
                uint8_t dev_num = 0;
                int sc_rtn = sscanf((char *)&uart3_stat_ST.rcv_line_buf[5], "%s %s %s",
                                    rcv_dummy_data[0], rcv_dummy_data[1], rcv_dummy_data[2]);
                if (sc_rtn == 3)
                {
                    sscanf((char *)&uart3_stat_ST.rcv_line_buf[5], "%hhd", &dev_num);
                    printf("STVC command: dev=%d, dir=%s, speed=%s\r\n",
                           dev_num, rcv_dummy_data[1], rcv_dummy_data[2]);
                    // TODO: 실제 속도 제어 로직 구현
                }
                else
                {
                    printf("Invalid STVC format. Usage: stvc <dev_num> <dir> <speed>\r\n");
                }
                printf("\r\n");
            }
            // stst 명령어 - 강제 정지 (dev_num)
            else if (strncmp(rcv_cmd, "stst", 4) == 0)
            {
                char rcv_dummy_data[20];
                uint8_t dev_num = 0;
                int sc_rtn = sscanf((char *)&uart3_stat_ST.rcv_line_buf[5], "%s", rcv_dummy_data);
                if (sc_rtn == 1)
                {
                    sscanf((char *)&uart3_stat_ST.rcv_line_buf[5], "%hhd", &dev_num);
                    printf("STST command: dev=%d (force stop)\r\n", dev_num);
                    // TODO: 실제 정지 로직 구현
                }
                else
                {
                    printf("Invalid STST format. Usage: stst <dev_num>\r\n");
                }
                printf("\r\n");
            }
            // 알 수 없는 명령어
            else
            {
                printf("Unknown command: '%s'\r\n", rcv_cmd);
                printf("Type 'help' to show available commands.\r\n\r\n");
            }

            // 명령어 버퍼 클리어
            memset(rcv_cmd, 0, sizeof(rcv_cmd));
        }

        // LED 토글 (시스템 동작 표시, 1초마다)
        if ((HAL_GetTick() - led_toggle) >= 1000)
        {
            led_toggle = HAL_GetTick();
            HAL_GPIO_TogglePin(OT_LD_SYS_GPIO_Port, OT_LD_SYS_Pin);
        }

        // CPU 부하 감소를 위한 짧은 딜레이
        // (IDLE 인터럽트가 동작하므로 명령어 수신에는 영향 없음)
        HAL_Delay(10);
    }
}

// ============================================================================
// Legacy Functions
// ============================================================================


void proc_uart3(void)
{
	COM_Idy_Typ rcv_rtn_stat = NOT_LINE;
	char rcv_cmd[20]="";
	char rcv_dummy_data[20]="";
	uint8_t dev_num=0;
	int sc_rtn;

	rcv_rtn_stat = UART3_GetLine(uart3_stat_ST.rcv_line_buf);

	if (rcv_rtn_stat == RCV_LINE)
	{
		printf_UARTC(&huart3,PR_YEL,"%s\r\n",(char *)uart3_stat_ST.rcv_line_buf);
		sscanf((char *)uart3_stat_ST.rcv_line_buf,"%s",rcv_cmd);
		printf_UARTC(&huart3,PR_YEL,"cmd : %s\r\n",rcv_cmd);

		if (strncmp((char *)rcv_cmd,"stvc",4) == 0)	// 속도 제어 명령 (dev_num,dir,speed)
		{
			sc_rtn = sscanf((char *)&uart3_stat_ST.rcv_line_buf[5],"%s %s %s",rcv_dummy_data,rcv_dummy_data,rcv_dummy_data);
			if (sc_rtn == 3)
			{
				sscanf((char *)&uart3_stat_ST.rcv_line_buf[5],"%hhd",&dev_num);
			}
		}
		else if (strncmp((char *)rcv_cmd,"stst",4) == 0)	// 강제 정지 명령 (dev_num)
		{
			sc_rtn = sscanf((char *)&uart3_stat_ST.rcv_line_buf[5],"%s",rcv_dummy_data);
			if (sc_rtn == 1)
			{
				sscanf((char *)&uart3_stat_ST.rcv_line_buf[5],"%hhd",&dev_num);
			}
		}
	}
}

void init_proc(void)
{
	init_UART_COM();
    // 사인파 룩업 테이블 초기화 (레거시 테스트용)
    init_sine_table();

    // DAC Calibration (CRITICAL for DMA operation)
    printf("\r\n[DAC_INIT] Performing calibration (INDEPENDENT MODE)...\r\n");

    // INDEPENDENT MODE: Each channel has its own trigger
    DAC_ChannelConfTypeDef sConfig = {0};
    sConfig.DAC_HighFrequency = DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC;
    sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;

    // DAC CH1: TIM1_TRGO trigger
    sConfig.DAC_Trigger = DAC_TRIGGER_T1_TRGO;
    HAL_StatusTypeDef cal1 = HAL_DACEx_SelfCalibrate(&hdac1, &sConfig, DAC_CHANNEL_1);

    // DAC CH2: TIM7_TRGO trigger (different from CH1!)
    sConfig.DAC_Trigger = DAC_TRIGGER_T7_TRGO;  // TSEL=6 → TIM7 TRGO
    HAL_StatusTypeDef cal2 = HAL_DACEx_SelfCalibrate(&hdac1, &sConfig, DAC_CHANNEL_2);

    printf("[DAC_INIT] Calibration CH1: %d, CH2: %d (0=OK)\r\n", cal1, cal2);
    printf("[DAC_INIT] DAC SR: 0x%08lX, CR: 0x%08lX\r\n", DAC1->SR, DAC1->CR);
    printf("[DAC_INIT] INDEPENDENT MODE: CH1=TIM1, CH2=TIM7\r\n\r\n");

    printf("\r\n========================================\r\n");
    printf("  STM32H523 Slave MCU Firmware v1.0\r\n");
    printf("  Audio Streaming via SPI\r\n");
    printf("========================================\r\n");
    printf("Protocol v2.0 - Hardware CS selection\r\n");
    printf("System Clock: %lu MHz\r\n", HAL_RCC_GetSysClockFreq() / 1000000);
    printf("Buffer Size: %d samples x 2 buffers\r\n", AUDIO_BUFFER_SIZE);
    printf("Total RAM: ~%d KB\r\n", (AUDIO_BUFFER_SIZE * 2 * 2 * 2) / 1024);
    printf("========================================\r\n");

    // UART3 상태 확인
    printf("\r\n[UART3 Status]\r\n");
    printf("Baudrate: %lu\r\n", huart3.Init.BaudRate);
    printf("RX Pin: PB1, TX Pin: PB10\r\n");
    printf("State: %lu (0=READY, 1=BUSY)\r\n", huart3.gState);
    printf("\r\n** If you can see this message, UART TX is working! **\r\n");
    printf("** Now test UART RX by selecting a menu option... **\r\n");

    // MPU + DCACHE 상태 확인
    printf("\r\n========================================\r\n");
    printf("  MPU + DCACHE Test (Phase 3)\r\n");
    printf("========================================\r\n");

    // MPU 상태
    if (MPU->CTRL & MPU_CTRL_ENABLE_Msk) {
        printf("[MPU] ✓ ENABLED\r\n");
        printf("  Region 0: 0x2003C000 ~ 0x20043FFF (32KB)\r\n");
        printf("  Attributes: Non-cacheable (for DMA buffers)\r\n");
    } else {
        printf("[MPU] ✗ DISABLED\r\n");
    }

    // DCACHE 상태
    if (DCACHE1->CR & DCACHE_CR_EN) {
        printf("[DCACHE] ✓ ENABLED\r\n");
    } else {
        printf("[DCACHE] ✗ DISABLED\r\n");
    }

    // DMA 버퍼 주소 출력
    printf("\r\n[DMA Buffer Addresses]\r\n");
    printf("  dac1_buffer_a:        0x%08lX\r\n", (uint32_t)dac1_buffer_a);
    printf("  dac1_buffer_b:        0x%08lX\r\n", (uint32_t)dac1_buffer_b);
    printf("  dac2_buffer_a:        0x%08lX\r\n", (uint32_t)dac2_buffer_a);
    printf("  dac2_buffer_b:        0x%08lX\r\n", (uint32_t)dac2_buffer_b);
    printf("  g_rx_cmd_packet:      0x%08lX\r\n", spi_handler_get_rx_buffer_addr());
    printf("  g_uart3_tx_dma_buf:   0x%08lX\r\n", UART3_Get_TX_Buffer_Addr());

    // MPU 영역 확인
    printf("\r\n[Verification]\r\n");
    if ((uint32_t)dac1_buffer_a >= 0x2003C000 &&
        (uint32_t)dac1_buffer_a < 0x20044000 &&
        spi_handler_get_rx_buffer_addr() >= 0x2003C000 &&
        spi_handler_get_rx_buffer_addr() < 0x20044000) {
        printf("  ✓ All DMA buffers in MPU non-cacheable region\r\n");
    } else {
        printf("  ✗ ERROR: Buffers NOT in MPU region!\r\n");
    }

    printf("========================================\r\n\r\n");
}

void run_proc(void)
{
    init_proc();

    // 기본 동작 모드 선택:
    // Option 1: 자동으로 slave 모드 실행 (배포용)
    // run_slave_mode();

    // Option 2: 테스트 메뉴 표시 (개발/디버그용)
    run_test_menu();

    // Option 3: 특정 테스트 직접 실행
    // test_led_blink();
    // test_dac_sine_wave();
    // test_rdy_pin();
}
