#ifndef UART_INTERFACE_H
#define UART_INTERFACE_H

#include <stdint.h>
#include "base_types.h"

void UARTIF_uartPrintf(uint8_t uartNumber, const char *format, ...);
void UARTIF_uartPrintfFloat(uint8_t uartNumber, const char *head, const float data);
void UARTIF_uartInit(void);
void UARTIF_lpuartInit(void);
void UARTIF_passThrough(void);
uint8_t UARTIF_passThroughCmd(void);
uint16_t UARTIF_fetchDataFromUart(uint8_t *buf, uint16_t *idx);
void UARTIF_getUartStats(uint32_t *rxCount, uint32_t *overflowCount);
void UARTIF_resetUartStats(void);

// ===== LPUART 接收队列访问接口（用于 E104 AT 指令接收）=====
// 检查 LPUART 接收队列是否为空
boolean_t UARTIF_isLpUartQueueEmpty(void);
// 从 LPUART 接收队列中取一个字节
boolean_t UARTIF_dequeueFromLpUart(uint8_t *data);

#endif // UART_INTERFACE_H
