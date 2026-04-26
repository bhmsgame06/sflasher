#include <stdint.h>
#include <string.h>
#include "uart.h"
#include "flash.h"

/* preloader command status */
enum {
	PL_VALID,
	PL_INVALID
};

/* preloader command list */
enum {
	PL_CMD_NOOP,
	PL_CMD_JUMP,
	PL_CMD_BAUD,
	PL_CMD_FLASH_GET_INFO,
	PL_CMD_FLASH_BLK_LOCK,
	PL_CMD_FLASH_BLK_UNLOCK,
	PL_CMD_FLASH_BLK_READ,
	PL_CMD_FLASH_BLK_PROGRAM,
	PL_CMD_FLASH_BLK_ERASE,
	PL_CMD_FLASH_CHIP_ERASE,
	PL_CMD_FLASH_OTP_READ
};

/* active uart port used during this session */
static int active_uart;
/* temp buffer for flash block */
static uint16_t block_buf[0x20000];

/* check if a block is erased */
bool blank_check(FLASH_BLK blk) {
	volatile uint16_t *addr = flash_blk_addr(blk);
	uint32_t length = flash_blk_wrd_count(blk);

	for(int i = 0; i < length; i++) {
		if(addr[i] != 0xffff)
			return false;
	}

	return true;
}

/* preloader command handler code */
void preloader_start(void) {
	while(true) {
		switch(uart_getc(active_uart)) {
			
			/* no operation */
			case PL_CMD_NOOP: {
				uart_putc(active_uart, PL_VALID);

				break;
			}

			/* jump to an address */
			case PL_CMD_JUMP: {
				uart_putc(active_uart, PL_VALID);

				void (*func)() = (void (*))(uart_getc(active_uart) |
					(uart_getc(active_uart) << 8) |
					(uart_getc(active_uart) << 16) |
					(uart_getc(active_uart) << 24));
				func();
				/* no return from there... */
			}

			/* set baud rate */
			case PL_CMD_BAUD: {
				uart_putc(active_uart, PL_VALID);

				uart_set_baud(active_uart, uart_getc(active_uart) |
						(uart_getc(active_uart) << 8) |
						(uart_getc(active_uart) << 16) |
						(uart_getc(active_uart) << 24));

				break;
			}
			
			/* get manufacturer ID and device ID */
			case PL_CMD_FLASH_GET_INFO: {
				uart_putc(active_uart, PL_VALID);

				uint16_t manu_id = flash_manufacturer_id();
				uint16_t dev_id = flash_device_id();
				uart_putc(active_uart, manu_id);
				uart_putc(active_uart, manu_id >> 8);
				uart_putc(active_uart, dev_id);
				uart_putc(active_uart, dev_id >> 8);

				break;
			}

			/* lock a flash block */
			case PL_CMD_FLASH_BLK_LOCK: {
				uart_putc(active_uart, PL_VALID);

				FLASH_BLK blk = uart_getc(active_uart) |
					(uart_getc(active_uart) << 8);
				flash_blk_protect(blk);

				break;
			}

			/* unlock a flash block */
			case PL_CMD_FLASH_BLK_UNLOCK: {
				uart_putc(active_uart, PL_VALID);

				FLASH_BLK blk = uart_getc(active_uart) |
					(uart_getc(active_uart) << 8);
				flash_blk_unprotect(blk);

				break;
			}

			/* read flash block */
			case PL_CMD_FLASH_BLK_READ: {
				uart_putc(active_uart, PL_VALID);
				
				FLASH_BLK blk = uart_getc(active_uart) |
					(uart_getc(active_uart) << 8);

				volatile uint16_t *addr = flash_blk_addr(blk);
				uint32_t length = flash_blk_wrd_count(blk);

				uint8_t checksum = 0;
				for(int i = 0; i < length; i++) {
					uint16_t value = addr[i];
					uart_putc(active_uart, value);
					uart_putc(active_uart, value >> 8);
					checksum -= value & 0xff;
					checksum -= value >> 8;
				}
				uart_putc(active_uart, checksum);

				break;
			}

			/* program flash block */
			case PL_CMD_FLASH_BLK_PROGRAM: {
				uart_putc(active_uart, PL_VALID);
				
				/* reading from uart to a temp buffer */
				FLASH_BLK blk = uart_getc(active_uart) |
					(uart_getc(active_uart) << 8);
				uint32_t blk_wrd_count = uart_getc(active_uart) |
					(uart_getc(active_uart) << 8) |
					(uart_getc(active_uart) << 16) |
					(uart_getc(active_uart) << 24);

				volatile uint16_t *addr = flash_blk_addr(blk);
				uint32_t length = flash_blk_wrd_count(blk);

				uint8_t checksum = 0;
				for(int i = 0; i < blk_wrd_count; i++) {
					uint16_t value = block_buf[i] = (uart_getc(active_uart) |
							(uart_getc(active_uart) << 8));
					checksum -= value & 0xff;
					checksum -= value >> 8;
				}

				/* checksum check */
				if(uart_getc(active_uart) == checksum) {
					/* correct */
					uart_putc(active_uart, 'c');
				} else {
					/* incorrect */
					uart_putc(active_uart, 'i');
					break;
				}

				if(memcmp((void *)addr, block_buf, blk_wrd_count << 1)) {
					if(!blank_check(blk)) {
						/* erasing */
						flash_blk_erase(blk);
						while(true) {
							uint16_t check_val;
							if((*addr & 0x40) == ((check_val = *addr) & 0x40)) {
								if(check_val == 0xffff)
									break;
								else
									flash_blk_erase(blk);
							}
						}
					}

					/* flushing a temp buffer to flash chip */
					for(int i = 0; i < blk_wrd_count; i += 0x20) {
						flash_buffer_write(blk, i, &block_buf[i], 0x20);
						flash_buffer_program(blk);
						while(true) {
							if((addr[i] & 0x40) == (addr[i] & 0x40))
								break;
						}
					}
				}

				/* done */
				uart_putc(active_uart, 'd');
				break;
			}

			/* flash block erase */
			case PL_CMD_FLASH_BLK_ERASE: {
				uart_putc(active_uart, PL_VALID);

				FLASH_BLK blk = uart_getc(active_uart) |
					(uart_getc(active_uart) << 8);

				volatile uint16_t *addr = flash_blk_addr(blk);

				if(!blank_check(blk)) {
					flash_blk_erase(blk);
					while(true) {
						uint16_t check_val;
						if((*addr & 0x40) == ((check_val = *addr) & 0x40)) {
							if(check_val == 0xffff)
								break;
							else
								flash_blk_erase(blk);
						}
					}
				}

				/* done */
				uart_putc(active_uart, 'd');
				break;
			}

			/* flash chip erase */
			case PL_CMD_FLASH_CHIP_ERASE: {
				uart_putc(active_uart, PL_VALID);

				flash_chip_erase();
				while(true) {
					if((*(volatile uint16_t *)FLASH_BASE_ADDR & 0x40) == (*(volatile uint16_t *)FLASH_BASE_ADDR & 0x40)) {
						break;
					}
				}

				/* done */
				uart_putc(active_uart, 'd');
				break;
			}

			case PL_CMD_FLASH_OTP_READ: {
				uart_putc(active_uart, PL_VALID);

				volatile uint16_t *addr = (volatile uint16_t *)(FLASH_BASE_ADDR + 0x1ffff00);
				uint8_t checksum = 0;

				flash_otp_entry();
				for(int i = 0; i < 0x80; i++) {
					uint16_t value = addr[i];
					uart_putc(active_uart, value);
					uart_putc(active_uart, value >> 8);
					checksum -= value;
					checksum -= value >> 8;
				}
				flash_otp_exit();

				uart_putc(active_uart, checksum);

				break;
			}

			/* unknown command */
			default: {
				uart_putc(active_uart, PL_INVALID);

				break;
			}

		}
	}
}

/* main function */
void start_kernel(void) {

	/* lock power */
	*(volatile uint32_t *)0x7001b030 = 0x01000000;

	/* trying to find one active uart */
	uart_set_baud(0, UART_115200);
	uart_set_baud(1, UART_115200);
	uart_enable(0);
	uart_enable(1);
	uart_putc(0, 'I');
	uart_putc(1, 'I');
	while(true) {
		if(uart_is_receiver_full(0)) {
			if(uart_getc(0) == 'a') {
				active_uart = 0;
				break;
			}
		} else if(uart_is_receiver_full(1)) {
			if(uart_getc(1) == 'a') {
				active_uart = 1;
				break;
			}
		}
	}
	if(active_uart == 0)
		uart_disable(1);
	else
		uart_disable(0);

	/* jump to preloader */
	preloader_start();

}
