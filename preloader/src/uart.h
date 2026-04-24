#ifndef _UART_H
#define _UART_H 1

#include <stdint.h>
#include <stdbool.h>

enum {
	UART_AUTOBAUD = 0,
	UART_2000000 = 12,
	UART_1498000 = 16,
	UART_1152000 = 21,
	UART_921600 = 27,
	UART_460800 = 55,
	UART_230400 = 111,
	UART_115200 = 224,
	UART_76800 = 337,
	UART_57600 = 450,
	UART_38400 = 676,
	UART_28800 = 901,
	UART_19200 = 1353,
	UART_14400 = 1804,
	UART_9600 = 2707,
	UART_7200 = 3610,
	UART_4800 = 5415,
	UART_3600 = 7221,
	UART_2400 = 10832,
	UART_2000 = 12999,
	UART_1800 = 14443,
	UART_1200 = 21665,
	UART_600 = 43332
};

extern void uart_enable(int uart);

extern void uart_disable(int uart);

extern uint32_t uart_get_status(int uart);

extern uint32_t uart_get_baud(int uart);

extern void uart_set_baud(int uart, uint32_t divider);

extern bool uart_is_receiver_full(int uart);

extern bool uart_is_transmit_fifo_full(int uart);

extern int uart_getc(int uart);

extern void uart_putc(int uart, int data);

#endif /* _UART_H */
