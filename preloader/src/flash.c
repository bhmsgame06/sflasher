#include <stdint.h>
#include "flash.h"

/* Autoselect */

uint16_t flash_manufacturer_id(void) {
	uint16_t value;

	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0xaa;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0x554) = 0x55;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x90;
	value = *(volatile uint16_t *)(FLASH_BASE_ADDR + 0);
	*(volatile uint16_t *)FLASH_BASE_ADDR = 0xf0;

	return value;
}

uint16_t flash_device_id(void) {
	uint16_t value;

	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0xaa;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0x554) = 0x55;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x90;
	value = *(volatile uint16_t *)(FLASH_BASE_ADDR + 2);
	*(volatile uint16_t *)FLASH_BASE_ADDR = 0xf0;

	return value;
}

uint16_t flash_blk_prot_verify(FLASH_BLK blk) {
	uint16_t value;

	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0xaa;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0x554) = 0x55;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + (blk * FLASH_BLK_SIZE) + 0xaaa) = 0x90;
	value = *(volatile uint16_t *)(FLASH_BASE_ADDR + 4);
	*(volatile uint16_t *)FLASH_BASE_ADDR = 0xf0;

	return value;
}

uint16_t flash_handshaking(void) {
	uint16_t value;

	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0xaa;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0x554) = 0x55;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x90;
	value = *(volatile uint16_t *)(FLASH_BASE_ADDR + 6);
	*(volatile uint16_t *)FLASH_BASE_ADDR = 0xf0;

	return value;
}

/* Flash operations */

void flash_program(FLASH_BLK blk, FLASH_BLK_OFFSET off, uint16_t value) {
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0xaa;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0x554) = 0x55;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0xa0;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + (blk * FLASH_BLK_SIZE) + off) = value;
}

void flash_blk_erase(FLASH_BLK blk) {
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0xaa;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0x554) = 0x55;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x80;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0xaa;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0x554) = 0x55;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + (blk * FLASH_BLK_SIZE)) = 0x30;
}

void flash_chip_erase(void) {
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0xaa;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0x554) = 0x55;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x80;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0xaa;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0x554) = 0x55;
	*(volatile uint16_t *)FLASH_BASE_ADDR = 0x10;
}

void flash_blk_protect(FLASH_BLK blk) {
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x60;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x60;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + (blk * FLASH_BLK_SIZE) + 0x04) = 0x60;
}

void flash_blk_unprotect(FLASH_BLK blk) {
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x60;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x60;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + (blk * FLASH_BLK_SIZE) + 0x84) = 0x60;
}

void flash_buffer_write(FLASH_BLK blk, FLASH_BLK_OFFSET off, void *buf, uint32_t wc) {
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0xaa;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0x554) = 0x55;
	volatile uint16_t *blk_addr = (volatile uint16_t *)(FLASH_BASE_ADDR + (blk * FLASH_BLK_SIZE) + off);
	*blk_addr = 0x25;
	*blk_addr = wc - 1;

	uint16_t *wrd_buf = (uint16_t *)buf;
	for(int wrd = 0; wrd < wc; wrd++) {
		blk_addr[wrd] = wrd_buf[wrd];
	}
}

void flash_buffer_program(FLASH_BLK blk) {
	*(volatile uint16_t *)(FLASH_BASE_ADDR + (blk * FLASH_BLK_SIZE)) = 0x29;
}

void flash_buffer_abort(void) {
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0xaa;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0x554) = 0x55;
	*(volatile uint16_t *)FLASH_BASE_ADDR = 0xf0;
}
/* Unlock Bypass operations */

void flash_unlock_bypass(void) {
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0xaa;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0x554) = 0x55;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + 0xaaa) = 0x20;
}

void flash_unlock_bypass_reset(void) {
	*(volatile uint16_t *)FLASH_BASE_ADDR = 0x90;
	*(volatile uint16_t *)FLASH_BASE_ADDR = 0x00;
}

void flash_unlock_bypass_program(FLASH_BLK blk, FLASH_BLK_OFFSET off, uint16_t value) {
	*(volatile uint16_t *)FLASH_BASE_ADDR = 0xa0;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + (blk * FLASH_BLK_SIZE) + off) = value;
}

void flash_unlock_bypass_blk_erase(FLASH_BLK blk) {
	*(volatile uint16_t *)FLASH_BASE_ADDR = 0x80;
	*(volatile uint16_t *)(FLASH_BASE_ADDR + (blk * FLASH_BLK_SIZE)) = 0x30;
}

void flash_unlock_bypass_chip_erase(void) {
	*(volatile uint16_t *)FLASH_BASE_ADDR = 0x80;
	*(volatile uint16_t *)FLASH_BASE_ADDR = 0x10;
}

