#ifndef _FLASH_H
#define _FLASH_H 1

#include <stdint.h>

typedef uint32_t FLASH_BLK;
typedef uint32_t FLASH_BLK_OFFSET;

#define FLASH_BASE_ADDR	0x90000000
#define FLASH_BLK_SIZE	0x20000

#define flash_addr(blk, off)	((volatile uint16_t *)(FLASH_BASE_ADDR + (blk * FLASH_BLK_SIZE) + off))
#define flash_read(blk, off)	(*flash_addr(blk, off))

/* Autoselect */

extern uint16_t flash_manufacturer_id(void);

extern uint16_t flash_device_id(void);

extern uint16_t flash_blk_prot_verify(FLASH_BLK blk);

extern uint16_t flash_handshaking(void);

/* Flash operations */

extern void flash_program(FLASH_BLK blk, FLASH_BLK_OFFSET off, uint16_t value);

extern void flash_blk_erase(FLASH_BLK blk);

extern void flash_chip_erase(void);

extern void flash_blk_protect(FLASH_BLK blk);

extern void flash_blk_unprotect(FLASH_BLK blk);

extern void flash_buffer_write(FLASH_BLK blk, FLASH_BLK_OFFSET off, void *buf, uint32_t wc);

extern void flash_buffer_program(FLASH_BLK blk);

extern void flash_buffer_abort(void);

/* Unlock Bypass operations */

extern void flash_unlock_bypass(void);

extern void flash_unlock_bypass_reset(void);

extern void flash_unlock_bypass_program(FLASH_BLK blk, FLASH_BLK_OFFSET off, uint16_t value);

extern void flash_unlock_bypass_blk_erase(FLASH_BLK blk);

extern void flash_unlock_bypass_chip_erase(void);

#endif /* _FLASH_H */

