#include "menu.h"

/* current menu state type */
struct menu_state {
	int selected;
	struct menu_entry *entries;
};

/* menu list */
enum {
	MENU_MAIN,
	MENU_BAUD_RATE,
};

/* menu - main menu */
enum {
	MENU_MAIN_DOWNLOAD_PRELOADER,
	MENU_MAIN_SERIAL_DEVICE,
	MENU_MAIN_BAUD_RATE,
	MENU_MAIN_FLASH_ID,
	MENU_MAIN_FLASH_READ,
	MENU_MAIN_FLASH_READ_OTP,
	MENU_MAIN_FLASH_BIN,
	MENU_MAIN_FLASH_TFS,
	MENU_MAIN_FLASH_CSC,
	MENU_MAIN_REBOOT_AFTER_FLASH,
	MENU_MAIN_FLASH_START,
	MENU_MAIN_FLASH_ERASE,
	MENU_MAIN_FLASH_ERASE_CHIP,
	MENU_MAIN_REBOOT_AND_EXIT,
};

/* menu - baud rate set up */
enum {
	MENU_BAUD_RATE_600,
	MENU_BAUD_RATE_1200,
	MENU_BAUD_RATE_1800,
	MENU_BAUD_RATE_2000,
	MENU_BAUD_RATE_2400,
	MENU_BAUD_RATE_3600,
	MENU_BAUD_RATE_4800,
	MENU_BAUD_RATE_7200,
	MENU_BAUD_RATE_9600,
	MENU_BAUD_RATE_14400,
	MENU_BAUD_RATE_19200,
	MENU_BAUD_RATE_28800,
	MENU_BAUD_RATE_38400,
	MENU_BAUD_RATE_57600,
	MENU_BAUD_RATE_76800,
	MENU_BAUD_RATE_115200,
	MENU_BAUD_RATE_230400,
	MENU_BAUD_RATE_460800,
	MENU_BAUD_RATE_921600,
	MENU_BAUD_RATE_1152000,
	MENU_BAUD_RATE_1498000,
};

/* menus and entries */
static struct menu_state menus[] = {
	{
		.selected = 0,
		.entries = (struct menu_entry[]) {
			{
				.type = MENU_TYPE_SPACER,
				.space_height = 1
			},
			{
				.type = MENU_TYPE_LABEL,
				.label = "========== Samsung Swift (PNX49xx) Flasher/Dumper =========="
			},
			{
				.type = MENU_TYPE_SPACER,
				.space_height = 1
			},
			{
				.type = MENU_TYPE_LABEL,
				.label = "GitHub: https://github.com/bhmsgame06/sflasher"
			},
			{
				.type = MENU_TYPE_SPACER,
				.space_height = 1
			},
			{
				.type = MENU_TYPE_LABEL,
				.label = "Press 'q' to quit."
			},
			{
				.type = MENU_TYPE_SPACER,
				.space_height = 1
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "Start",
				.ansi = "\033[97m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "Serial port",
				.ansi = "\033[97m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_SPACER,
				.space_height = 1
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "Set baud rate",
				.ansi = "\033[97m",
				.button_enabled = false
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "Read flash chip info",
				.ansi = "\033[97m",
				.button_enabled = false
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "Dump image",
				.ansi = "\033[97m",
				.button_enabled = false
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "Dump OTP flash region",
				.ansi = "\033[97m",
				.button_enabled = false
			},
			{
				.type = MENU_TYPE_SPACER,
				.space_height = 1
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "BIN flashing",
				.ansi = "\033[97m",
				.button_enabled = false
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "TFS flashing",
				.ansi = "\033[97m",
				.button_enabled = false
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "CSC flashing",
				.ansi = "\033[97m",
				.button_enabled = false
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "Reboot after flashing",
				.ansi = "\033[97m",
				.button_enabled = false
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "Flash!",
				.ansi = "\033[94m",
				.button_enabled = false
			},
			{
				.type = MENU_TYPE_SPACER,
				.space_height = 1
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "Erase blocks",
				.ansi = "\033[97m",
				.button_enabled = false
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "Erase chip",
				.ansi = "\033[97m",
				.button_enabled = false
			},
			{
				.type = MENU_TYPE_SPACER,
				.space_height = 1
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "Reboot and exit",
				.ansi = "\033[93m",
				.button_enabled = false
			},
			{
				.type = MENU_TYPE_END
			}
		}
	},

	{
		.selected = 15,
		.entries = (struct menu_entry[]) {
			{
				.type = MENU_TYPE_SPACER,
				.space_height = 1
			},
			{
				.type = MENU_TYPE_LABEL,
				.label = "========== Baud rate =========="
			},
			{
				.type = MENU_TYPE_SPACER,
				.space_height = 1
			},
			{
				.type = MENU_TYPE_LABEL,
				.label = "Press 'q' to go back."
			},
			{
				.type = MENU_TYPE_SPACER,
				.space_height = 1
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "600 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "1200 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "1800 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "2000 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "2400 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "3600 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "4800 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "7200 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "9600 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "14400 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "19200 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "28800 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "38400 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "57600 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "76800 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "115200 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "230400 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "460800 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "921600 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "1152000 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_BUTTON,
				.label = "1498000 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_END
			},
		},
	},
};
