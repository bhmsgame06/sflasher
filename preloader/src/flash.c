#include <stdint.h>
#include "flash.h"

static void flash_unlock_seq(void) {
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0xaa;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0x554) = 0x55;
}

/* all blocks have size - 131072 except 255, 256, 257, 258 that - 32768 */
volatile uint16_t *flash_blk_addr(FLASH_BLK blk) {
	if(blk < 255)
		return (volatile uint16_t *)FLASH_BASE_ADDR + (blk * 0x10000);
	else
		return (volatile uint16_t *)FLASH_BASE_ADDR + 0xff0000 + ((blk - 255) * 0x4000);
}

/* Autoselect */

uint16_t flash_manufacturer_id(void) {
	uint16_t value;

	flash_unlock_seq();
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x90;
	value = *(volatile uint16_t *)FLASH_BASE_ADDR;
	*(volatile uint16_t *)FLASH_BASE_ADDR = 0xf0;

	return value;
}

uint16_t flash_device_id(void) {
	uint16_t value;

	flash_unlock_seq();
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x90;
	value = *(volatile uint16_t *)(FLASH_BASE_ADDR + 2);
	*(volatile uint16_t *)FLASH_BASE_ADDR = 0xf0;

	return value;
}

/* Flash operations */

void flash_program(FLASH_BLK blk, FLASH_BLK_WRD_OFF off, uint16_t value) {
	flash_unlock_seq();
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0xa0;
	flash_blk_addr(blk)[off] = value;
}

void flash_blk_erase(FLASH_BLK blk) {
	flash_unlock_seq();
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x80;
	flash_unlock_seq();
	*flash_blk_addr(blk) = 0x30;
}

void flash_chip_erase(void) {
	flash_unlock_seq();
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x80;
	flash_unlock_seq();
	*(volatile uint16_t *)FLASH_BASE_ADDR = 0x10;
}

void flash_blk_protect(FLASH_BLK blk) {
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x60;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x60;
	flash_blk_addr(blk)[0x02] = 0x60;
}

void flash_blk_unprotect(FLASH_BLK blk) {
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x60;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x60;
	flash_blk_addr(blk)[0x42] = 0x60;
}

void flash_buffer_write(FLASH_BLK blk, FLASH_BLK_WRD_OFF off, void *buf, uint32_t wc) {
	flash_unlock_seq();
	volatile uint16_t *blk_addr = flash_blk_addr(blk) + off;
	*blk_addr = 0x25;
	*blk_addr = wc - 1;

	uint16_t *wrd_buf = (uint16_t *)buf;
	for(int wrd = 0; wrd < wc; wrd++) {
		blk_addr[wrd] = wrd_buf[wrd];
	}
}

void flash_buffer_program(FLASH_BLK blk) {
	*flash_blk_addr(blk) = 0x29;
}

void flash_buffer_abort(void) {
	flash_unlock_seq();
	*(volatile uint16_t *)FLASH_BASE_ADDR = 0xf0;
}

/* OTP */

void flash_otp_entry(void) {
	flash_unlock_seq();
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x70;
}

void flash_otp_exit(void) {
	flash_unlock_seq();
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x75;
	*(volatile uint16_t *)FLASH_BASE_ADDR = 0x00;
}
