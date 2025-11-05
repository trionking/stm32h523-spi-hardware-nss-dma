/*
 * user_com.c
 *
 *  Created on: Nov 2, 2021
 *      Author: trion
 */

#include "stdio.h"
#include "stdarg.h"
#include "string.h"

#include "main.h"
#include "ring_buffer.h"
#include "user_com.h"
#include "user_def.h"

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart3;

// UART1 queue
Queue rx_UART1_queue;
Queue rx_UART1_line_queue;
Queue tx_UART1_queue;

// UART3 queue
Queue rx_UART3_queue;
Queue rx_UART3_line_queue;
Queue tx_UART3_queue;

struct uart_Stat_ST uart1_stat_ST;
struct uart_Stat_ST uart3_stat_ST;

uint8_t atoh(char in_ascii)
{
    uint8_t rtn_val;

    if ( (in_ascii >= '0') && (in_ascii <= '9') )
    {
        rtn_val = in_ascii - '0';
    }
    else if ( (in_ascii >= 'a') && (in_ascii <= 'f') )
    {
        rtn_val = in_ascii - 'a' + 0x0A;
    }
    else if ( (in_ascii >= 'A') && (in_ascii <= 'F') )
    {
        rtn_val = in_ascii - 'A' + 0x0A;
    }
    else
    {
        rtn_val = 0xFF;
    }

    return rtn_val;
}

uint8_t atod(char in_ascii)
{
    uint8_t rtn_val;

    if ( (in_ascii >= '0') || (in_ascii <= '9') )
    {
        rtn_val = in_ascii - '0';
    }
    else
    {
        rtn_val = 0xFF;
    }

    return rtn_val;
}

uint32_t atoh_str(char *in_asc_str,uint8_t len)
{
	uint32_t rtn_val,i,j;

	rtn_val = 0;

	for(i=0,j=1;i<len;i++,j*=0x10)
	{
		rtn_val += (uint32_t)atoh(in_asc_str[len-1-i])*j;
	}

	return rtn_val;
}

uint32_t atod_str(char *in_asc_str,uint8_t len)
{
	uint32_t rtn_val,i,j;

	rtn_val = 0;

	for(i=0,j=1;i<len;i++,j*=10)
	{
		rtn_val += (uint32_t)atod(in_asc_str[len-1-i])*j;
	}

	return rtn_val;
}

void UART_baudrate_set(UART_HandleTypeDef *tm_hart,uint32_t baud_rate)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  //huart1.Instance = USART1;
  //huart1.Init.BaudRate = 9600;
	tm_hart->Init.BaudRate = baud_rate;
  if (HAL_UART_Init(tm_hart) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}


void init_UART_COM(void)
{

	UART_baudrate_set(&huart1,115200);
	UART_baudrate_set(&huart3,115200);

	// uart1
  InitQueue(&rx_UART1_line_queue,256);
  InitQueue(&rx_UART1_queue,512);
  InitQueue(&tx_UART1_queue,512);

	// uart3
  InitQueue(&rx_UART3_line_queue,256);
  InitQueue(&rx_UART3_queue,512);
  InitQueue(&tx_UART3_queue,2048);  // Increased for long initialization messages

//	__HAL_UART_ENABLE_IT(&huart1,UART_IT_IDLE);
//	HAL_UART_Receive_DMA(&huart1,uart1_stat_ST.uart_rx_DMA_buf,DMA_RX_BUFFER_SIZE);
	// UART3: IDLE 인터럽트 활성화 + 일반 DMA 수신 (작동하는 코드와 동일)
	__HAL_UART_ENABLE_IT(&huart3,UART_IT_IDLE);
	HAL_UART_Receive_DMA(&huart3,uart3_stat_ST.uart_rx_DMA_buf,DMA_RX_BUFFER_SIZE);
}


void printf_UARTC(UART_HandleTypeDef *h_tmUART,uint8_t color,const char *str_buf,...)
{
    char tx_bb[3200];
    uint32_t len=0;

    if (color != PR_NOP)
    {
			// color set
			sprintf(tx_bb,"\033[%dm",color);
			len = strlen(tx_bb);
    }


    va_list ap;
    va_start(ap, str_buf);
    vsprintf(&tx_bb[len], str_buf, ap);
    va_end(ap);

    len = strlen(tx_bb);
    tx_bb[len] = 0;
    if (h_tmUART->Instance == USART1)
    {
      Enqueue_bytes(&tx_UART1_queue,(uint8_t *)tx_bb,len);
    }
    else if (h_tmUART->Instance == USART3)
    {
      Enqueue_bytes(&tx_UART3_queue,(uint8_t *)tx_bb,len);
    }
}



COM_Idy_Typ UART3_GetLine(uint8_t *line_buf)
{
	uint16_t q_len;
	uint8_t rx_dat;
	COM_Idy_Typ rtn_val = NOT_LINE;

	q_len = Len_queue(&rx_UART3_queue);
	if (q_len)
	{
		for(uint16_t i=0;i<q_len;i++)
		{
			rx_dat = Dequeue(&rx_UART3_queue);
			Enqueue(&rx_UART3_line_queue,rx_dat);
			#ifdef UART3_ECHO
			printf_UARTC(&huart3,PR_NOP,"%c",rx_dat);
			#endif
			if (rx_dat == UART3_ETX)
			{
				#ifdef UART3_ECHO
				printf_UARTC(&huart3,PR_NOP,"\n");
				#endif
				flush_queue(&rx_UART3_queue);
				q_len = Len_queue(&rx_UART3_line_queue);
				Dequeue_bytes(&rx_UART3_line_queue,line_buf,q_len);
				line_buf[q_len-1] = 0;

		  	printf_UARTC(&huart3,PR_YEL,"%s\033[%dm\r\n",line_buf,PR_INI);

		  	// 유효한 명령어 체크 (help, stvc, stst, rdat, 0~5)
		  	if
				(
						(strncmp((char *)line_buf,"help",4) == 0) ||
						(strncmp((char *)line_buf,"stvc",4) == 0) ||
						(strncmp((char *)line_buf,"stst",4) == 0) ||
						(strncmp((char *)line_buf,"rdat",4) == 0) ||
						// 0~5 숫자 명령어 (한 글자만)
						(strlen((char *)line_buf) == 1 && line_buf[0] >= '0' && line_buf[0] <= '5')
				)
				{
					rtn_val = RCV_LINE;
				}
				else
				{
					rtn_val = WNG_LINE;
				}
				break;	// escape from for()
			}
		}
	}

	return rtn_val;
}

// ============================================================================
// DMA-based TX implementation
// ============================================================================

// DMA TX state and buffer
volatile uint8_t g_uart3_tx_busy = 0;

// DMA TX buffer - placed in non-cacheable RAM for DCACHE compatibility
__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
static uint8_t g_uart3_tx_dma_buffer[DMA_TX_BUFFER_SIZE];

/**
 * @brief Process UART3 TX queue and start DMA transmission if not busy
 * @note Call this function periodically from main loop
 */
void UART3_Process_TX_Queue(void)
{
	// If DMA is busy, return immediately
	if (g_uart3_tx_busy)
	{
		return;
	}

	// Check if there's data in TX queue
	uint16_t q_len = Len_queue(&tx_UART3_queue);
	if (q_len == 0)
	{
		return;  // Nothing to send
	}

	// Limit chunk size to DMA buffer size
	if (q_len > DMA_TX_BUFFER_SIZE)
	{
		q_len = DMA_TX_BUFFER_SIZE;
	}

	// Copy data from queue to DMA buffer
	Dequeue_bytes(&tx_UART3_queue, g_uart3_tx_dma_buffer, q_len);

	// Start DMA transmission
	g_uart3_tx_busy = 1;
	HAL_StatusTypeDef status = HAL_UART_Transmit_DMA(&huart3, g_uart3_tx_dma_buffer, q_len);

	if (status != HAL_OK)
	{
		// DMA start failed - mark as not busy so we can retry
		g_uart3_tx_busy = 0;
	}
}

/**
 * @brief UART3 TX DMA complete callback
 * @note Called from HAL_UART_TxCpltCallback in stm32h5xx_it.c
 */
void UART3_TX_Complete_Callback(void)
{
	// Mark as not busy - allows next transmission
	g_uart3_tx_busy = 0;

	// Immediately process next chunk if available (non-blocking)
	UART3_Process_TX_Queue();
}

/**
 * @brief Get address of UART3 TX DMA buffer (for debugging)
 */
uint32_t UART3_Get_TX_Buffer_Addr(void)
{
	return (uint32_t)g_uart3_tx_dma_buffer;
}

