#include <stdint.h>

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

/* preloader loadable code */
extern const uint8_t preloader_data[];

/* preloader size */
extern const uint32_t preloader_data_size;
