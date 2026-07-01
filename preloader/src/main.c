#include <stdint.h>
#include <string.h>
#include "uart.h"
#include "flash.h"

#define TFS_SECT_SIZE	512

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
	PL_CMD_FLASH_OTP_READ,
	PL_CMD_TFS_READ_SECT_MARKS,
	PL_CMD_TFS_MARK_SECT,
	PL_CMD_TFS_SECT_READ,
	PL_CMD_TFS_SECT_WRITE
};

/* active uart port used during this session */
static int active_uart;
/* temp buffer for flash block */
static uint16_t block_buf[0x10000];

/* check if a block is erased */

static bool blank_check(FLASH_BLK blk) {
	volatile uint16_t *addr = flash_blk_addr(blk);
	uint32_t length = flash_blk_wrd_count(blk);

	for(int i = 0; i < length; i++) {
		if(addr[i] != 0xffff)
			return false;
	}

	return true;
}

/* TFS4 stuff */

static uint8_t tfs_read_mark(uint16_t sect) {
	return ((uint8_t *)(FLASH_BASE_ADDR + (sect + 0x8000) * TFS_SECT_SIZE))[TFS_SECT_SIZE - 1];
}

static void tfs_write_mark(uint8_t mark, uint16_t sect) {
	sect += 0x8000;
	FLASH_BLK blk = sect >> 8;
	volatile uint16_t *addr = flash_blk_addr(blk);
	flash_blk_unprotect(blk);
	flash_program(sect >> 8, ((sect & 0xff) << 8) + 255, (mark << 8) | 0xff);
	flash_blk_protect(blk);
}

static void tfs_read_sect(uint8_t *dest, uint16_t sect) {
	memcpy(dest, (void *)(FLASH_BASE_ADDR + (sect + 0x8000) * TFS_SECT_SIZE), TFS_SECT_SIZE);
}

static uint8_t tfs_write_sect(uint8_t *src, uint16_t sect) {
	sect += 0x8000;
	FLASH_BLK blk = sect >> 8;
	volatile uint16_t *addr = flash_blk_addr(blk);
	flash_blk_unprotect(blk);
	for(int i = 0; i < 8; i++) {
		flash_buffer_write(sect >> 8, ((sect & 0xff) << 8) + (i << 5), src, 32);
		flash_buffer_program(sect >> 8);
		src += 64;
	}
	flash_blk_protect(blk);
}

/* UART wrappers */

static uint32_t uart_read32() {
	return uart_getc(active_uart) |
		(uart_getc(active_uart) << 8) |
		(uart_getc(active_uart) << 16) |
		(uart_getc(active_uart) << 24);
}

static uint16_t uart_read16() {
	return uart_getc(active_uart) |
		(uart_getc(active_uart) << 8);
}

static uint8_t uart_read8() {
	return uart_getc(active_uart);
}

static void uart_write32(uint32_t val) {
	uart_putc(active_uart, val);
	uart_putc(active_uart, val >> 8);
	uart_putc(active_uart, val >> 16);
	uart_putc(active_uart, val >> 24);
}

static void uart_write16(uint16_t val) {
	uart_putc(active_uart, val);
	uart_putc(active_uart, val >> 8);
}

static void uart_write8(uint8_t val) {
	uart_putc(active_uart, val);
}

/* preloader command handler code */

static void preloader_start(void) {
	while(true) {
		switch(uart_read8()) {
			
			/* no operation */
			case PL_CMD_NOOP: {
				uart_write8(PL_VALID);

				break;
			}

			/* jump to an address */
			case PL_CMD_JUMP: {
				uart_write8(PL_VALID);

				void (*func)() = (void (*))uart_read32();
				func();
				/* no return from there... */
			}

			/* set baud rate */
			case PL_CMD_BAUD: {
				uart_write8(PL_VALID);

				uart_set_baud(active_uart, uart_read32());

				break;
			}
			
			/* get manufacturer ID and device ID */
			case PL_CMD_FLASH_GET_INFO: {
				uart_write8(PL_VALID);

				uart_write32(flash_manufacturer_id() | (flash_device_id() << 16));

				break;
			}

			/* lock a flash block */
			case PL_CMD_FLASH_BLK_LOCK: {
				uart_write8(PL_VALID);

				FLASH_BLK blk = uart_read16();
				flash_blk_protect(blk);

				break;
			}

			/* unlock a flash block */
			case PL_CMD_FLASH_BLK_UNLOCK: {
				uart_write8(PL_VALID);

				FLASH_BLK blk = uart_read16();
				flash_blk_unprotect(blk);

				break;
			}

			/* read flash block */
			case PL_CMD_FLASH_BLK_READ: {
				uart_write8(PL_VALID);
				
				FLASH_BLK blk = uart_read16();

				volatile uint16_t *addr = flash_blk_addr(blk);
				uint32_t length = flash_blk_wrd_count(blk);

				uint8_t checksum = 0;
				for(int i = 0; i < length; i++) {
					uint16_t value = addr[i];
					uart_write16(value);
					checksum -= value & 0xff;
					checksum -= value >> 8;
				}
				uart_write8(checksum);

				break;
			}

			/* program flash block */
			case PL_CMD_FLASH_BLK_PROGRAM: {
				uart_write8(PL_VALID);
				
				/* reading from uart to a temp buffer */
				FLASH_BLK blk = uart_read16();
				uint32_t blk_wrd_count = uart_read32();

				volatile uint16_t *addr = flash_blk_addr(blk);
				uint32_t length = flash_blk_wrd_count(blk);

				uint8_t checksum = 0;
				for(int i = 0; i < blk_wrd_count; i++) {
					uint16_t value = block_buf[i] = uart_read16();
					checksum -= value & 0xff;
					checksum -= value >> 8;
				}

				/* checksum check */
				if(uart_read8() == checksum) {
					/* correct */
					uart_write8('c');
				} else {
					/* incorrect */
					uart_write8('i');
					break;
				}

				if(memcmp((void *)addr, block_buf, blk_wrd_count << 1)) {
					if(!blank_check(blk)) {
						/* erasing */
						flash_blk_erase(blk);
					}

					/* flushing a temp buffer to flash chip */
					for(int i = 0; i < blk_wrd_count; i += 32) {
						flash_buffer_write(blk, i, &block_buf[i], 32);
						flash_buffer_program(blk);
					}
				}

				/* done */
				uart_write8('d');
				break;
			}

			/* flash block erase */
			case PL_CMD_FLASH_BLK_ERASE: {
				uart_write8(PL_VALID);

				FLASH_BLK blk = uart_read16();

				volatile uint16_t *addr = flash_blk_addr(blk);

				if(!blank_check(blk))
					flash_blk_erase(blk);

				/* done */
				uart_write8('d');
				break;
			}

			/* flash chip erase */
			case PL_CMD_FLASH_CHIP_ERASE: {
				uart_write8(PL_VALID);

				flash_chip_erase();

				/* done */
				uart_write8('d');
				break;
			}

			/* read otp region */
			case PL_CMD_FLASH_OTP_READ: {
				uart_write8(PL_VALID);

				volatile uint16_t *addr = (volatile uint16_t *)(FLASH_BASE_ADDR + 0x1fffe00);
				uint8_t checksum = 0;

				flash_otp_entry();
				for(int i = 0; i < 0x100; i++) {
					uint16_t value = addr[i];
					uart_write16(value);
					checksum -= value;
					checksum -= value >> 8;
				}
				flash_otp_exit();

				uart_write8(checksum);

				break;
			}

			/* read sector marks */
			case PL_CMD_TFS_READ_SECT_MARKS: {
				uart_write8(PL_VALID);

				// start sect
				uint16_t sect = uart_read16();
				// num sects
				uint16_t end_sect = sect + uart_read16();

				uint8_t packed = 0;
				uint32_t packed_ind = 8;

				for(; sect < end_sect; sect++) {
					packed_ind -= 2;
					switch(tfs_read_mark(sect)) {
						case 0xff:
							packed |= 0b11 << packed_ind;
							break;

						case 0xf0:
							packed |= 0b10 << packed_ind;
							break;

						/* case 0x00: break; */
					}

					if(packed_ind == 0) {
						uart_write8(packed);
						packed_ind = 8;
						packed = 0;
					}
				}
				if(packed_ind < 8) {
					uart_write8(packed);
				}

				break;
			}

			/* mark sector */
			case PL_CMD_TFS_MARK_SECT: {
				uart_write8(PL_VALID);

				uint16_t sect = uart_read16();
				tfs_write_mark(uart_read8(), sect);

				break;
			}

			/* read a sector */
			case PL_CMD_TFS_SECT_READ: {
				uart_write8(PL_VALID);

				uint8_t sect_data[TFS_SECT_SIZE];
				tfs_read_sect(sect_data, uart_read16());

				uint8_t checksum = 0;
				for(int i = 0; i < TFS_SECT_SIZE - 1; i++) {
					uart_write8(sect_data[i]);
					checksum -= sect_data[i];
				}

				uart_write8(checksum);

				break;
			}

			/* write to a sector */
			case PL_CMD_TFS_SECT_WRITE: {
				uart_write8(PL_VALID);

				uint16_t sect = uart_read16();
				FLASH_BLK blk = (sect >> 8) + 128;

				bool erase = uart_read8();

				uint8_t checksum = 0;
				uint8_t sect_data[TFS_SECT_SIZE];
				for(int i = 0; i < TFS_SECT_SIZE; i++) {
					checksum -= sect_data[i] = uart_read8();
				}

				/* checksum check */
				if(uart_read8() == checksum) {
					/* correct */
					uart_write8('c');
				} else {
					/* incorrect */
					uart_write8('i');
					break;
				}

				/* erase block */
				if(erase)
					flash_blk_erase(blk);

				tfs_write_sect(sect_data, sect);

				break;
			}

			/* unknown command */
			default: {
				uart_write8(PL_INVALID);

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
