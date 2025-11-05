/*
 * user_com.h
 *
 *  Created on: Nov 2, 2021
 *      Author: trion
 */

#ifndef INC_USER_COM_H_
#define INC_USER_COM_H_

#include "main.h"
#include "ring_buffer.h"
#include "user_def.h"

#define PR_NOP    99        // No style
#define PR_INI    0         // initial value
#define PR_BLK    30        // Black
#define PR_RED    31        // Red
#define PR_GRN    32        // Green
#define PR_YEL    33        // Yellow
#define PR_BLE    34        // Blue
#define PR_MAG    35        // Magenta
#define PR_CYN    36        // Cyan
#define PR_WHT    37        // White

#define DMA_RX_BUFFER_SIZE	256
#define DMA_TX_BUFFER_SIZE	512

// DMA TX state
extern volatile uint8_t g_uart3_tx_busy;

struct uart_Stat_ST {
	uint8_t f_uart_rcvd;	// uart dma rx recieved complete flag
	uint8_t cnt_rx;

	uint8_t uart_rx_DMA_buf[DMA_RX_BUFFER_SIZE+2];
	uint8_t uart_tx_usr_buf[DMA_TX_BUFFER_SIZE+2];
	uint8_t rcv_line_buf[DMA_RX_BUFFER_SIZE+2];
	uint32_t dma_rx_len;
	uint16_t rx_CRC16;
	uint16_t tx_CRC16;
	uint16_t cal_CRC16;
};

typedef enum
{
  NOT_LINE = 0,
  RCV_LINE = 1,
  WNG_LINE = 2,
  PAS_LINE = 3,
} COM_Idy_Typ;

#define UART3_ECHO
#define UART3_ETX		'\r'

#define UART1_ECHO
#define UART1_ETX		'\r'

uint8_t atoh(char in_ascii);
uint8_t atod(char in_ascii);
uint32_t atoh_str(char *in_asc_str,uint8_t len);
uint32_t atod_str(char *in_asc_str,uint8_t len);

void UART_baudrate_set(UART_HandleTypeDef *tm_hart,uint32_t baud_rate);
void init_UART_COM(void);
void printf_UARTC(UART_HandleTypeDef *h_tmUART,uint8_t color,const char *str_buf,...);

COM_Idy_Typ UART3_GetLine(uint8_t *line_buf);

// DMA-based TX functions
void UART3_Process_TX_Queue(void);
void UART3_TX_Complete_Callback(void);

#endif /* INC_USER_COM_H_ */
