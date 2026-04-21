#include <stdint.h>
#include <stdbool.h>
#include "uart.h"

void *uart_handler[2] = {
	(void *)0x70007000,
	(void *)0x70006000
};

void uart_enable(int uart) {
	if(uart == 0) {
		*(volatile uint32_t *)0x7000400c = 0x80;
	} else {
		*(volatile uint32_t *)0x7000400c = 0x40;
	}
}

void uart_disable(int uart) {
	if(uart == 0) {
		*(volatile uint32_t *)0x70004010 = 0x80;
	} else {
		*(volatile uint32_t *)0x70004010 = 0x40;
	}
}

uint32_t uart_get_status(int uart) {
	return *((volatile uint32_t *)(uart_handler[uart] + 0x0c));
}

uint32_t uart_get_baud(int uart) {
	return *((volatile uint32_t *)(uart_handler[uart] + 0x08));
}

void uart_set_baud(int uart, uint32_t divider) {
	*((volatile uint32_t *)(uart_handler[uart] + 0x08)) = divider;
}

bool uart_is_receiver_full(int uart) {
	return uart_get_status(uart) & 1;
}

bool uart_is_transmit_fifo_full(int uart) {
	return (uart_get_status(uart) << 14) >> 31;
}

int uart_getc(int uart) {
	while(!uart_is_receiver_full(uart)) {
	}

	return *((volatile uint8_t *)(uart_handler[uart] + 0x10));
}

void uart_putc(int uart, int data) {
	while(uart_is_transmit_fifo_full(uart)) {
	}
	
	*((volatile uint8_t *)(uart_handler[uart] + 0x10)) = data;
}
