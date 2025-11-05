/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32h5xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32h5xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "spi_handler.h"
#include "audio_channel.h"
#include "user_com.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
extern Queue rx_UART1_queue;
extern struct uart_Stat_ST uart1_stat_ST;

extern Queue rx_UART3_queue;
extern struct uart_Stat_ST uart3_stat_ST;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

// Forward declaration of HAL callback (defined in USER CODE section below)
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern DAC_HandleTypeDef hdac1;
extern DMA_HandleTypeDef handle_GPDMA1_Channel5;
extern DMA_HandleTypeDef handle_GPDMA1_Channel4;
extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim7;
extern DMA_HandleTypeDef handle_GPDMA1_Channel3;
extern DMA_NodeTypeDef Node_GPDMA1_Channel2;
extern DMA_QListTypeDef List_GPDMA1_Channel2;
extern DMA_HandleTypeDef handle_GPDMA1_Channel2;
extern DMA_HandleTypeDef handle_GPDMA1_Channel1;
extern DMA_NodeTypeDef Node_GPDMA1_Channel0;
extern DMA_QListTypeDef List_GPDMA1_Channel0;
extern DMA_HandleTypeDef handle_GPDMA1_Channel0;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart3;
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVCall_IRQn 0 */

  /* USER CODE END SVCall_IRQn 0 */
  /* USER CODE BEGIN SVCall_IRQn 1 */

  /* USER CODE END SVCall_IRQn 1 */
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_IRQn 0 */

  /* USER CODE END PendSV_IRQn 0 */
  /* USER CODE BEGIN PendSV_IRQn 1 */

  /* USER CODE END PendSV_IRQn 1 */
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */

  /* USER CODE END SysTick_IRQn 0 */
  HAL_IncTick();
  /* USER CODE BEGIN SysTick_IRQn 1 */

  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32H5xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32h5xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles GPDMA1 Channel 0 global interrupt.
  */
void GPDMA1_Channel0_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel0_IRQn 0 */

  /* USER CODE END GPDMA1_Channel0_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel0);
  /* USER CODE BEGIN GPDMA1_Channel0_IRQn 1 */

  /* USER CODE END GPDMA1_Channel0_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 1 global interrupt.
  */
void GPDMA1_Channel1_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel1_IRQn 0 */

  /* USER CODE END GPDMA1_Channel1_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel1);
  /* USER CODE BEGIN GPDMA1_Channel1_IRQn 1 */

  /* USER CODE END GPDMA1_Channel1_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 2 global interrupt.
  */
void GPDMA1_Channel2_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel2_IRQn 0 */

  /* USER CODE END GPDMA1_Channel2_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel2);
  /* USER CODE BEGIN GPDMA1_Channel2_IRQn 1 */

  /* USER CODE END GPDMA1_Channel2_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 3 global interrupt.
  */
void GPDMA1_Channel3_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel3_IRQn 0 */

  /* USER CODE END GPDMA1_Channel3_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel3);
  /* USER CODE BEGIN GPDMA1_Channel3_IRQn 1 */

  /* USER CODE END GPDMA1_Channel3_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 4 global interrupt.
  */
void GPDMA1_Channel4_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel4_IRQn 0 */

  /* USER CODE END GPDMA1_Channel4_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel4);
  /* USER CODE BEGIN GPDMA1_Channel4_IRQn 1 */

  /* USER CODE END GPDMA1_Channel4_IRQn 1 */
}

/**
  * @brief This function handles GPDMA1 Channel 5 global interrupt.
  */
void GPDMA1_Channel5_IRQHandler(void)
{
  /* USER CODE BEGIN GPDMA1_Channel5_IRQn 0 */

  /* USER CODE END GPDMA1_Channel5_IRQn 0 */
  HAL_DMA_IRQHandler(&handle_GPDMA1_Channel5);
  /* USER CODE BEGIN GPDMA1_Channel5_IRQn 1 */

  /* USER CODE END GPDMA1_Channel5_IRQn 1 */
}

/**
  * @brief This function handles DAC1 interrupt.
  */
void DAC1_IRQHandler(void)
{
  /* USER CODE BEGIN DAC1_IRQn 0 */

  /* USER CODE END DAC1_IRQn 0 */
  HAL_DAC_IRQHandler(&hdac1);
  /* USER CODE BEGIN DAC1_IRQn 1 */

  /* USER CODE END DAC1_IRQn 1 */
}

/**
  * @brief This function handles TIM7 global interrupt.
  */
void TIM7_IRQHandler(void)
{
  /* USER CODE BEGIN TIM7_IRQn 0 */

  /* USER CODE END TIM7_IRQn 0 */
  HAL_TIM_IRQHandler(&htim7);
  /* USER CODE BEGIN TIM7_IRQn 1 */

  /* USER CODE END TIM7_IRQn 1 */
}

/**
  * @brief This function handles SPI1 global interrupt.
  */
void SPI1_IRQHandler(void)
{
  /* USER CODE BEGIN SPI1_IRQn 0 */

  /* USER CODE END SPI1_IRQn 0 */
  HAL_SPI_IRQHandler(&hspi1);
  /* USER CODE BEGIN SPI1_IRQn 1 */

  /* USER CODE END SPI1_IRQn 1 */
}

/**
  * @brief This function handles USART1 global interrupt.
  */
void USART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART1_IRQn 0 */

  /* USER CODE END USART1_IRQn 0 */
  HAL_UART_IRQHandler(&huart1);
  /* USER CODE BEGIN USART1_IRQn 1 */

  /* USER CODE END USART1_IRQn 1 */
}

/**
  * @brief This function handles USART3 global interrupt.
  */
void USART3_IRQHandler(void)
{
  /* USER CODE BEGIN USART3_IRQn 0 */
	if (huart3.Instance->ISR & UART_FLAG_IDLE)
  {
		__HAL_UART_CLEAR_IDLEFLAG(&huart3);
    uart3_stat_ST.dma_rx_len = DMA_RX_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(&handle_GPDMA1_Channel0);
		HAL_UART_AbortReceive(&huart3);
    Enqueue_bytes(&rx_UART3_queue,uart3_stat_ST.uart_rx_DMA_buf,uart3_stat_ST.dma_rx_len);
    uart3_stat_ST.f_uart_rcvd = 1;
    memset(uart3_stat_ST.uart_rx_DMA_buf,0,DMA_RX_BUFFER_SIZE);
	  HAL_UART_Receive_DMA(&huart3,uart3_stat_ST.uart_rx_DMA_buf,DMA_RX_BUFFER_SIZE);
	}

  /* USER CODE END USART3_IRQn 0 */
  HAL_UART_IRQHandler(&huart3);
  /* USER CODE BEGIN USART3_IRQn 1 */

  /* USER CODE END USART3_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/* ============================================================================ */
/* HAL Callback Functions */
/* ============================================================================ */

/**
  * @brief UART TX Complete Callback
  * @note Called when UART DMA TX completes
  */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3)
    {
        // Call DMA TX complete handler
        UART3_TX_Complete_Callback();
    }
}

// External audio channels (defined in user_def.c or slave_main.c)
extern AudioChannel_t g_dac1_channel;
extern AudioChannel_t g_dac2_channel;

/**
  * @brief SPI RX Complete Callback
  * @note Called when SPI DMA reception completes
  */
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1)
    {
        spi_handler_rx_callback(hspi);
    }
}

/**
  * @brief SPI Error Callback
  * @note Called when SPI error occurs
  */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1)
    {
        spi_handler_error_callback(hspi);
    }
}

/**
  * @brief DAC CH1 DMA Half Transfer Complete Callback
  * @note Called when first half of buffer (0~1023) has been output
  */
void HAL_DAC_ConvHalfCpltCallbackCh1(DAC_HandleTypeDef *hdac)
{
    // First half (0~1023) of active buffer has been output
    // Could update first half here if needed (not used in circular buffer mode)
}

/**
  * @brief DAC CH1 DMA Transfer Complete Callback
  * @note Called when second half of buffer (1024~2047) has been output
  *       This is the time to swap buffers
  */
void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef *hdac)
{
    // Second half (1024~2047) of active buffer has been output
    // Now we need to swap buffers if fill buffer is ready

    if (g_dac1_channel.fill_index >= AUDIO_BUFFER_SIZE)
    {
        // Fill buffer is ready - swap buffers
        if (audio_channel_swap_buffers(&g_dac1_channel))
        {
            // Swap successful - clear underrun flag
            audio_channel_clear_underrun(&g_dac1_channel);

            // Update DMA to use new active buffer
            // Note: In circular mode, we need to restart DAC DMA with new buffer
            HAL_DAC_Stop_DMA(hdac, DAC_CHANNEL_1);
            HAL_DAC_Start_DMA(hdac, DAC_CHANNEL_1,
                            (uint32_t*)g_dac1_channel.active_buffer,
                            AUDIO_BUFFER_SIZE,
                            DAC_ALIGN_12B_R);
        }
    }
    else
    {
        // Buffer underrun - fill buffer not ready
        g_dac1_channel.underrun = 1;
        g_dac1_channel.underrun_count++;
    }
}

/**
  * @brief DAC CH2 DMA Half Transfer Complete Callback
  */
void HAL_DAC_ConvHalfCpltCallbackCh2(DAC_HandleTypeDef *hdac)
{
    // First half of CH2 buffer output
}

/**
  * @brief DAC CH2 DMA Transfer Complete Callback
  */
void HAL_DAC_ConvCpltCallbackCh2(DAC_HandleTypeDef *hdac)
{
    // Second half of CH2 buffer output - swap buffers

    if (g_dac2_channel.fill_index >= AUDIO_BUFFER_SIZE)
    {
        // Fill buffer is ready - swap buffers
        if (audio_channel_swap_buffers(&g_dac2_channel))
        {
            // Swap successful
            audio_channel_clear_underrun(&g_dac2_channel);

            // Restart DAC DMA with new buffer
            HAL_DAC_Stop_DMA(hdac, DAC_CHANNEL_2);
            HAL_DAC_Start_DMA(hdac, DAC_CHANNEL_2,
                            (uint32_t*)g_dac2_channel.active_buffer,
                            AUDIO_BUFFER_SIZE,
                            DAC_ALIGN_12B_R);
        }
    }
    else
    {
        // Buffer underrun
        g_dac2_channel.underrun = 1;
        g_dac2_channel.underrun_count++;
    }
}

/**
  * @brief DAC Error Callback
  */
void HAL_DAC_ErrorCallbackCh1(DAC_HandleTypeDef *hdac)
{
    // DAC CH1 error occurred
    g_dac1_channel.underrun = 1;

    printf("\r\n[DAC ERROR CH1] ==================\r\n");
    printf("ErrorCode: 0x%08lX\r\n", hdac->ErrorCode);
    printf("State: 0x%08lX\r\n", (uint32_t)hdac->State);
    printf("DMA_Handle1: 0x%08lX\r\n", (uint32_t)hdac->DMA_Handle1);
    if (hdac->DMA_Handle1) {
        printf("DMA ErrorCode: 0x%08lX\r\n", hdac->DMA_Handle1->ErrorCode);
        printf("DMA State: 0x%08lX\r\n", (uint32_t)hdac->DMA_Handle1->State);
    }
    printf("=====================================\r\n");
}

void HAL_DAC_ErrorCallbackCh2(DAC_HandleTypeDef *hdac)
{
    // DAC CH2 error occurred
    g_dac2_channel.underrun = 1;
}

/**
  * @brief GPIO EXTI Callback
  * @note Hardware NSS mode is used for SPI (no EXTI needed for CS detection)
  *       PA15 is configured as SPI1_NSS (hardware controlled)
  *       SPI DMA runs in continuous mode, NSS LOW enables data transfer
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    // Hardware NSS mode: EXTI not used for SPI CS detection
    // Other GPIO EXTI callbacks can be handled here if needed

    (void)GPIO_Pin;  // Suppress unused warning
}

/* USER CODE END 1 */
