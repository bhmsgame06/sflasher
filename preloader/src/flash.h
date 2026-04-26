#ifndef _FLASH_H
#define _FLASH_H 1

#include <stdint.h>

typedef uint32_t FLASH_BLK;
typedef uint32_t FLASH_BLK_WRD_OFF;

#define FLASH_BASE_ADDR				0x90000000

#define flash_blk_wrd_count(blk)	(blk < 255 ? 0x10000 : 0x4000)

extern volatile uint16_t *flash_blk_addr(FLASH_BLK blk);

/* Autoselect */

extern uint16_t flash_manufacturer_id(void);

extern uint16_t flash_device_id(void);

/* Flash operations */

extern void flash_program(FLASH_BLK blk, FLASH_BLK_WRD_OFF off, uint16_t value);

extern void flash_blk_erase(FLASH_BLK blk);

extern void flash_chip_erase(void);

extern void flash_blk_protect(FLASH_BLK blk);

extern void flash_blk_unprotect(FLASH_BLK blk);

extern void flash_buffer_write(FLASH_BLK blk, FLASH_BLK_WRD_OFF off, void *buf, uint32_t wc);

extern void flash_buffer_program(FLASH_BLK blk);

extern void flash_buffer_abort(void);

/* OTP */

extern void flash_otp_entry(void);

extern void flash_otp_exit(void);

#endif /* _FLASH_H */
