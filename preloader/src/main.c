#include <stdint.h>
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
	PL_CMD_FLASH_UNLOCK_BYPASS
};

/* active uart port used during this session */
static int active_uart;
/* temp buffer for flash block */
static uint16_t block_buf[FLASH_BLK_SIZE >> 1];
/* unlock bypass enabled */
static bool unlock_bypass = false;

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
				uint8_t checksum = 0;
				for(int off = 0; off < FLASH_BLK_SIZE; off += 2) {
					uint16_t value = flash_read(blk, off);
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

				uint8_t checksum = 0;
				for(int i = 0; i < blk_wrd_count; i++) {
					uint16_t value = block_buf[i] = uart_getc(active_uart) |
							(uart_getc(active_uart) << 8);
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

				/* flushing a temp buffer to flash chip */
				for(int off = 0; off < (blk_wrd_count << 1); off += 0x20 * sizeof(uint16_t)) {
					if(blk == 255 && off >= 0x8000) break;

					flash_buffer_write(blk, off, (void *)(&block_buf) + off, 0x20);
					flash_buffer_program(blk);
					while(true) {
						if((flash_read(blk, off) & 0x40) == (flash_read(blk, off) & 0x40))
							break;
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
				if(unlock_bypass)
					flash_unlock_bypass_blk_erase(blk);
				else
					flash_blk_erase(blk);
				while(true) {
					uint16_t check_val;
					if((flash_read(blk, 0) & 0x40) == ((check_val = flash_read(blk, 0)) & 0x40)) {
						if(check_val == 0xffff)
							break;
						else
							flash_blk_erase(blk);
					}
				}

				/* done */
				uart_putc(active_uart, 'd');
				break;
			}

			/* flash chip erase */
			case PL_CMD_FLASH_CHIP_ERASE: {
				uart_putc(active_uart, PL_VALID);

				if(unlock_bypass)
					flash_unlock_bypass_chip_erase();
				else
					flash_chip_erase();
				while(true) {
					if((flash_read(0, 0) & 0x40) == (flash_read(0, 0) & 0x40)) {
						break;
					}
				}

				/* done */
				uart_putc(active_uart, 'd');
				break;
			}

			case PL_CMD_FLASH_UNLOCK_BYPASS: {
				uart_putc(active_uart, PL_VALID);

				if(uart_getc(active_uart)) {
					flash_unlock_bypass();
					unlock_bypass = true;
				} else {
					flash_unlock_bypass_reset();
					unlock_bypass = false;
				}
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
