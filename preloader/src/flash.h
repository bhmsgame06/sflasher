#ifndef _FLASH_H
#define _FLASH_H 1

#include <stdint.h>

typedef uint32_t FLASH_BLK;
typedef uint32_t FLASH_BLK_OFFSET;

#define FLASH_BASE_ADDR	0x90000000
#define FLASH_BLK_SIZE	0x20000

#define flash_read(blk, off)	(*(volatile uint16_t *)(FLASH_BASE_ADDR + (blk * FLASH_BLK_SIZE) + off))

/* Autoselect */

uint16_t flash_manufacturer_id(void);

uint16_t flash_device_id(void);

uint16_t flash_blk_prot_verify(FLASH_BLK blk);

uint16_t flash_handshaking(void);

/* Flash operations */

void flash_program(FLASH_BLK blk, FLASH_BLK_OFFSET off, uint16_t value);

void flash_blk_erase(FLASH_BLK blk);

void flash_chip_erase(void);

void flash_blk_protect(FLASH_BLK blk);

void flash_blk_unprotect(FLASH_BLK blk);

/* Unlock Bypass operations */

void flash_unlock_bypass(void);

void flash_unlock_bypass_reset(void);

void flash_unlock_bypass_program(FLASH_BLK blk, FLASH_BLK_OFFSET off, uint16_t value);

void flash_unlock_bypass_blk_erase(FLASH_BLK blk);

void flash_unlock_bypass_chip_erase(void);

#endif /* _FLASH_H */

