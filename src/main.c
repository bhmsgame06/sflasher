#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <asm/termbits.h>
#include <sys/ioctl.h>
#include <getopt.h>
#include "uart.h"
#include "preloader.h"
#include "ihex.h"
#include "menu.h"

#define INTERNAL_RAM_PHYS_START_ADDRESS	0x1f400000
#define FLASH_PHYS_START_ADDRESS		0x90000000
#define FLASH_OTP_LENGTH				0x200

#define flash_blk_size(blk)				(blk < 255 ? 0x20000 : 0x8000)

#define SERIAL_READ_BYTE()				(read(serial_fd, &b, sizeof(uint8_t)), b)

#define SERIAL_WRITE_BYTE(value)		{ \
											b = value; \
											write(serial_fd, &b, sizeof(uint8_t)); \
										}

/* current menu state type */
struct menu_state {
	int selected;
	struct menu_entry *entries;
};

/* baud rate and its divider */
struct baud_divider {
	uint32_t baud;
	uint32_t divider;
};

static char *program_name;

/* default serial device */
static char serial_device[256] = "/dev/ttyUSB0";
/* serial UNIX file descriptor */
static int serial_fd;
/* reboot after flashing */
static bool reboot_after_flash = false;

/* baudrate table */
static const struct baud_divider baudrate_table[22] = {
	{600,        UART_600},
	{1200,       UART_1200},
	{1800,       UART_1800},
	{2000,       UART_2000},
	{2400,       UART_2400},
	{3600,       UART_3600},
	{4800,       UART_4800},
	{7200,       UART_7200},
	{9600,       UART_9600},
	{14400,      UART_14400},
	{19200,      UART_19200},
	{28800,      UART_28800},
	{38400,      UART_38400},
	{57600,      UART_57600},
	{76800,      UART_76800},
	{115200,     UART_115200},
	{230400,     UART_230400},
	{460800,     UART_460800},
	{921600,     UART_921600},
	{1152000,    UART_1152000},
	{1498000,    UART_1498000},
	{2000000,    UART_2000000}  /* be careful. */
};

/* menu stuff */

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
	MENU_MAIN_FLASH_WRITE,
	MENU_MAIN_REBOOT_AFTER_FLASH,
	MENU_MAIN_FLASH_ERASE,
	MENU_MAIN_FLASH_ERASE_CHIP,
	MENU_MAIN_REBOOT_AND_EXIT
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
	MENU_BAUD_RATE_2000000
};

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
				.type = MENU_TYPE_BUTTON,
				.label = "Flash image",
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
				.type = MENU_TYPE_BUTTON,
				.label = "2000000 bps",
				.ansi = "\033[96m",
				.button_enabled = true
			},
			{
				.type = MENU_TYPE_END
			}
		}
	}
};
static int current_menu = MENU_MAIN;

/* long options */
static const struct option longopts[] = {
	{"help",      0, NULL, 'h'},
	{"device",    1, NULL, 'd'},
	{NULL, 0, NULL, 0}
};

static void show_help(int err);
static int read_fixed(int fd, void *buf, int count);
static void press_any_key(void);
static void canon_mode(bool return_old);
static void quit(int exit_code);
static void sig_handler(int sig);
static uint8_t calc_checksum(uint8_t *buf, uint32_t length);

/* print help to the terminal */
static void show_help(int err) {
	fprintf(err == 1 ? stderr : stdout,
			"Usage: %s [options]\n" \
			"\n" \
			"Available options:\n" \
			"  -h, --help             - print help and exit.\n" \
			"  -d, --device=<file>    - serial device to operate on.\n",
			program_name);
}

/* read fixed bytes (blocking operation) */
static int read_fixed(int fd, void *buf, int count) {
	int n_read = 0;
	do {
		int n = read(fd, buf + n_read, count - n_read);
		if(n > 0) {
			n_read += n;
		} else if(n <= 0) {
			if(n == 0) errno = EBADF;
			return -1;
		}

	} while(n_read < count);

	return count;
}

/* press any key message */
static void press_any_key(void) {
	printf("Press any key...\n");
	uint8_t b;
	read(STDIN_FILENO, &b, sizeof(uint8_t));
	canon_mode(false);
}

/* enable/disable canonical mode on this terminal */
static void canon_mode(bool enable) {
	struct termios2 main_tty;
	if(ioctl(0, TCGETS2, &main_tty) != 0) {
		perror("ioctl TCGETS2");
		press_any_key();
	}

	if(enable)
		main_tty.c_lflag |= ICANON | ECHO;
	else
		main_tty.c_lflag &= ~(ICANON | ECHO);

	if(ioctl(0, TCSETS2, &main_tty) < 0) {
		perror("ioctl TCSETS2");
		press_any_key();
	}
}

/* quit with canonical mode returning */
static void quit(int exit_code) {
	canon_mode(true);
	exit(exit_code);
}

/* signal handler */
static void sig_handler(int sig) {
	switch(sig) {
		case SIGINT:
			quit(1);

		case SIGCONT:
			canon_mode(true);
			canon_mode(false);
			break;
	}
}

/* flash checksum */
static uint8_t calc_checksum(uint8_t *buf, uint32_t length) {
	uint8_t result = 0;
	for(int i = 0; i < length; i++) {
		result -= buf[i];
	}
	return result;
}

int main(int argc, char *argv[]) {
	if(!argv[0])
		program_name = "sflasher";
	else
		program_name = argv[0];

	int c;
	while((c = getopt_long(argc, argv, "hd:", longopts, NULL)) != -1) {
		
		switch(c) {
			/* --help */
			case 'h':
				show_help(0);
				return 0;

			/* --device */
			case 'd':
				strncpy(serial_device, optarg, sizeof(serial_device) - 1);
				break;

			default:
				show_help(1);
				return 1;
		}

	}

	argv += optind;
	argc -= optind;

	/* configuring tty */
	canon_mode(false);

	/* setting signal handler */
	signal(SIGINT, sig_handler);
	signal(SIGCONT, sig_handler);

	while(true) {
		ioctl(serial_fd, TCFLSH, TCIFLUSH);

		int selected;

do_not_process:
		if((selected = draw_menu(menus[current_menu].entries,
						menus[current_menu].selected)) == -1) {

			if(current_menu != MENU_MAIN) {
				current_menu = MENU_MAIN;
				goto do_not_process;
			} else {
				printf("\nAre you sure? [Y/n]\n");
				fflush(stdout);
				uint8_t kb_input;
				while(true) {
					read(0, &kb_input, sizeof(uint8_t));
					if(kb_input == 'Y' || kb_input == 'y' || kb_input == '\n' || kb_input == ' ') {
						goto end;
					} else if(kb_input == 'N' || kb_input == 'n') {
						goto do_not_process;
					}
				}
			}

		}

		menus[current_menu].selected = selected;
		fputc('\n', stdout);

		switch(current_menu) {
			struct termios2 tty;
			uint8_t b;

			case MENU_MAIN:
				switch(selected) {
					case MENU_MAIN_DOWNLOAD_PRELOADER: {
						printf("Opening UART port...\n");
						serial_fd = open(serial_device, O_RDWR | O_NOCTTY);
						if(serial_fd < 0) {
							perror(serial_device);
							press_any_key();
							break;
						}
						if(ioctl(serial_fd, TCGETS2, &tty) < 0) {
							perror("ioctl TCGETS2");
							press_any_key();
							break;
						}
						tty.c_cflag &= ~CBAUD;
						tty.c_cflag |= BOTHER;
						tty.c_ispeed = 115200;
						tty.c_ospeed = 115200;
						tty.c_lflag &= ~(ISIG | ICANON | XCASE | IEXTEN | ECHO | ECHOK | ECHOKE | ECHOCTL);
						tty.c_iflag &= ~(IGNBRK | BRKINT | IGNPAR | PARMRK | INPCK | ISTRIP | INLCR | IGNCR | ICRNL | IUCLC | IXON | IXANY | IXOFF | IMAXBEL);
						tty.c_oflag &= ~(OPOST);
						tty.c_cc[VMIN] = 1;
						tty.c_cc[VTIME] = 0;
						if(ioctl(serial_fd, TCSETS2, &tty) < 0) {
							perror("ioctl TCSETS2");
							press_any_key();
							break;
						}
						ioctl(serial_fd, TCFLSH, TCIFLUSH);

						printf("Waiting for a device response...\nPlease press and hold end key (hang up) for 1 sec.\n");
						while(true) {
							SERIAL_WRITE_BYTE(0x16); /* SYN byte */
							usleep(10000);
							uint32_t av;
							ioctl(serial_fd, FIONREAD, &av);
							if(av > 0) {
								if(SERIAL_READ_BYTE() == 0x16)
									break;
							}
						}

						printf("Sending preloader (%d bytes)...\n", preloader_data_size);
						if(!create_ihex(serial_fd, INTERNAL_RAM_PHYS_START_ADDRESS, preloader_data, preloader_data_size)) {
							printf("Unable to create IHEX file!\n");
							press_any_key();
							break;
						}
						if(SERIAL_READ_BYTE() != '0') {
							printf("BootROM error: %02X.\n", b);
							press_any_key();
							break;
						}
						
						printf("Performing init handshake...\n");
						while(SERIAL_READ_BYTE() != 'I') {
						}
						SERIAL_WRITE_BYTE('a');

						press_any_key();

						get_button(menus[MENU_MAIN].entries, MENU_MAIN_SERIAL_DEVICE)->button_enabled = false;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_DOWNLOAD_PRELOADER)->button_enabled = false;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_BAUD_RATE)->button_enabled = true;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_FLASH_ID)->button_enabled = true;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_FLASH_READ)->button_enabled = true;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_FLASH_READ_OTP)->button_enabled = true;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_FLASH_WRITE)->button_enabled = true;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_REBOOT_AFTER_FLASH)->button_enabled = true;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_FLASH_ERASE)->button_enabled = true;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_FLASH_ERASE_CHIP)->button_enabled = true;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_REBOOT_AND_EXIT)->button_enabled = true;
						break;
					}

					case MENU_MAIN_SERIAL_DEVICE: {
						printf("Path to a character device (%s): ", serial_device);
						fflush(stdout);
						canon_mode(true);
						scanf("%s", serial_device);
						canon_mode(false);
						break;
					}

					case MENU_MAIN_BAUD_RATE: {
						current_menu = MENU_BAUD_RATE;
						break;
					}

					case MENU_MAIN_FLASH_ID: {
						SERIAL_WRITE_BYTE(PL_CMD_FLASH_GET_INFO);
						if(SERIAL_READ_BYTE() != PL_VALID) {
							printf("Preloader: Invalid command\n");
							press_any_key();
							break;
						}

						uint16_t manufacturer_id;
						uint16_t device_id;
						read_fixed(serial_fd, &manufacturer_id, sizeof(uint16_t));
						read_fixed(serial_fd, &device_id, sizeof(uint16_t));
						
						printf("Manufacturer ID: 0x%04X\n", manufacturer_id);
						printf("Device ID: 0x%04X\n", device_id);
						press_any_key();
						break;
					}

					case MENU_MAIN_FLASH_READ: {
						int blk_first;
						int blk_last;

						canon_mode(true);

						printf("Enter a block range (0-258): ");
						fflush(stdout);
						char sbuf[16];
						scanf("%s", sbuf);

						char *tok;
						if(!(tok = strtok(sbuf, "-"))) {
							printf("Invalid range.\n");
							goto exit_from_switch;
						} else {
							blk_first = atoi(tok);
						}
						if(!(tok = strtok(NULL, "-"))) {
							blk_last = blk_first + 1;
						} else {
							blk_last = atoi(tok) + 1;
						}

						if(blk_first < 0 || blk_first > 258 || blk_last < 1 || blk_last > 259) {
							printf("Invalid range.\n");
							break;
						}

						printf("Output path: ");
						fflush(stdout);
						char out_path[256];
						scanf("%s", out_path);

						canon_mode(false);

						FILE *out_fd = fopen(out_path, "wb");
						if(!out_fd) {
							perror(out_path);
							press_any_key();
							break;
						}

						uint8_t *blk_buf = malloc(0x20000);
						if(!blk_buf) {
							perror("malloc");
							press_any_key();
							fclose(out_fd);
							break;
						}
						
						for(int i = blk_first; i < blk_last; i++) {
							int av;
							ioctl(STDIN_FILENO, FIONREAD, &av);
							if(av > 0) {
								uint8_t b;
								read(STDIN_FILENO, &b, sizeof(uint8_t));
								if(b == 'q') {
									printf("Interrupted by user.\n");
									press_any_key();
									free(blk_buf);
									fclose(out_fd);
									goto exit_from_switch;
								}
							}

							printf("Reading block %d... ", i);
							fflush(stdout);

							SERIAL_WRITE_BYTE(PL_CMD_FLASH_BLK_READ);
							if(SERIAL_READ_BYTE() != PL_VALID) {
								printf("Preloader: Invalid command\n");
								press_any_key();
								free(blk_buf);
								fclose(out_fd);
								goto exit_from_switch;
							}
							int blk = i;
							if(write(serial_fd, &blk, 2) < 0) {
								perror("write");
								press_any_key();
								free(blk_buf);
								fclose(out_fd);
								goto exit_from_switch;
							}

							uint32_t length = flash_blk_size(blk);

							read_fixed(serial_fd, blk_buf, length);
							uint8_t checksum = SERIAL_READ_BYTE();
							if(checksum == calc_checksum(blk_buf, length)) {
								printf("OK\n");
								fwrite(blk_buf, 1, length, out_fd);
							} else {
								printf("CHECKSUM WRONG\n");
								i--; /* retry */
							}
						}

						free(blk_buf);
						fclose(out_fd);

						printf("%d block(s) have been successfully dumped.\n", blk_last - blk_first);
						press_any_key();

						break;
					}

					case MENU_MAIN_FLASH_READ_OTP: {
						canon_mode(true);

						char out_path[256];

						printf("Output file: ");
						fflush(stdout);
						scanf("%s", out_path);

						canon_mode(false);

						FILE *out_fd = fopen(out_path, "wb");
						if(!out_fd) {
							perror(out_path);
							press_any_key();
							break;
						}

						uint8_t otp_buf[FLASH_OTP_LENGTH];

						while(true) {
							SERIAL_WRITE_BYTE(PL_CMD_FLASH_OTP_READ);
							if(SERIAL_READ_BYTE() != PL_VALID) {
								printf("Preloader: Invalid command\n");
								press_any_key();
								fclose(out_fd);
								goto exit_from_switch;
							}

							read_fixed(serial_fd, otp_buf, FLASH_OTP_LENGTH);

							if(SERIAL_READ_BYTE() == calc_checksum(otp_buf, FLASH_OTP_LENGTH))
								break;
						}

						fwrite(otp_buf, 1, FLASH_OTP_LENGTH, out_fd);
						fclose(out_fd);

						break;
					}

					case MENU_MAIN_FLASH_WRITE: {
						canon_mode(true);

						char bin_file[256];
						uint32_t blk_first;

						printf("Path to a binary (.cla/.bin): ");
						fflush(stdout);
						scanf("%s", bin_file);

						printf("Begin flash block (0-258): ");
						fflush(stdout);
						scanf("%d", &blk_first);

						canon_mode(false);

						if(blk_first < 0 || blk_first > 258) {
							printf("Invalid block index.\n");
							goto exit_from_switch;
						}

						FILE *bin_fd = fopen(bin_file, "rb");
						if(!bin_fd) {
							perror(bin_file);
							press_any_key();
							break;
						}
						fseek(bin_fd, 0, SEEK_END);
						uint32_t bin_size = ftell(bin_fd);
						fseek(bin_fd, 0, SEEK_SET);

						uint32_t blk_last = blk_first;
						for(int i = bin_size; i > 0; i -= flash_blk_size(blk_last++)) {
						}

						if(blk_last > 259) {
							printf("Binary doesn't fitting to the flash memory address space.\n");
							press_any_key();
							fclose(bin_fd);
							break;
						}

						printf("Binary size: %d (0x%08X) bytes\n", bin_size, bin_size);
						printf("Number of blocks: %d\n", blk_last - blk_first);
						printf("Block start: %d\n", blk_first);
						printf("Block end: %d\n", blk_last);

						uint8_t *blk_buf = malloc(0x20000);
						if(!blk_buf) {
							perror("malloc");
							press_any_key();
							fclose(bin_fd);
							goto exit_from_switch;
						}

						for(int i = blk_first; i < blk_last; i++) {
							int av;
							ioctl(STDIN_FILENO, FIONREAD, &av);
							if(av > 0) {
								uint8_t b;
								read(STDIN_FILENO, &b, sizeof(uint8_t));
								if(b == 'q') {
									printf("Interrupted by user.\n");
									press_any_key();
									free(blk_buf);
									fclose(bin_fd);
									goto exit_from_switch;
								}
							}

							/* unlocking a block */
							SERIAL_WRITE_BYTE(PL_CMD_FLASH_BLK_UNLOCK);
							if(SERIAL_READ_BYTE() != PL_VALID) {
								printf("Preloader: Invalid command\n");
								press_any_key();
								free(blk_buf);
								fclose(bin_fd);
								goto exit_from_switch;
							}
							if(write(serial_fd, &i, 2) < 0) {
								perror("write");
								press_any_key();
								free(blk_buf);
								fclose(bin_fd);
								goto exit_from_switch;
							}

							uint32_t length = flash_blk_size(i);

							uint32_t n_read = fread(blk_buf, 1, length, bin_fd);
							memset(blk_buf + n_read, 0xff, length - n_read);

							n_read += (n_read & 1);
program_try_again:
							printf("Writing block %d... ", i);
							fflush(stdout);

							/* programming */
							SERIAL_WRITE_BYTE(PL_CMD_FLASH_BLK_PROGRAM);
							if(SERIAL_READ_BYTE() != PL_VALID) {
								printf("Preloader: Invalid command\n");
								press_any_key();
								free(blk_buf);
								fclose(bin_fd);
								goto exit_from_switch;
							}
							if(write(serial_fd, &i, 2) < 0) {
								perror("write");
								press_any_key();
								free(blk_buf);
								fclose(bin_fd);
								goto exit_from_switch;
							}

							uint32_t blk_wrd_count = n_read >> 1;
							if(write(serial_fd, &blk_wrd_count, 4) < 0) {
								perror("write");
								press_any_key();
								free(blk_buf);
								fclose(bin_fd);
								goto exit_from_switch;
							}

							if(write(serial_fd, blk_buf, n_read) < 0) {
								perror("write");
								press_any_key();
								free(blk_buf);
								fclose(bin_fd);
								goto exit_from_switch;
							}
							SERIAL_WRITE_BYTE(calc_checksum(blk_buf, n_read));

							if(SERIAL_READ_BYTE() != 'c') {
								printf("CHECKSUM WRONG\n");
								goto program_try_again;
							} else {
								if(SERIAL_READ_BYTE() != 'd') {
									printf("FAIL\n");
									press_any_key();
									free(blk_buf);
									fclose(bin_fd);
									goto exit_from_switch;
								}
								printf("OK\n");
							}

							/* locking again */
							SERIAL_WRITE_BYTE(PL_CMD_FLASH_BLK_LOCK);
							if(SERIAL_READ_BYTE() != PL_VALID) {
								printf("Preloader: Invalid command\n");
								press_any_key();
								free(blk_buf);
								fclose(bin_fd);
								goto exit_from_switch;
							}
							if(write(serial_fd, &i, 2) < 0) {
								perror("write");
								press_any_key();
								free(blk_buf);
								fclose(bin_fd);
								goto exit_from_switch;
							}
						}

						free(blk_buf);
						fclose(bin_fd);

						printf("%d block(s) have been successfully reflashed.\n", blk_last - blk_first);

						if(reboot_after_flash) {
							SERIAL_WRITE_BYTE(PL_CMD_JUMP);
							if(SERIAL_READ_BYTE() != PL_VALID) {
								printf("Preloader: Invalid command\n");
								press_any_key();
								quit(1);
							}

							uint32_t flash_start = FLASH_PHYS_START_ADDRESS;
							if(write(serial_fd, &flash_start, sizeof(uint32_t)) < 0) {
								perror("write");
								press_any_key();
								quit(1);
							}

							press_any_key();
							quit(0);
						}

						press_any_key();
						break;
					}

					case MENU_MAIN_FLASH_ERASE: {
						int blk_first;
						int blk_last;

						canon_mode(true);

						while(true) {
							printf("Enter a block range (0-258): ");
							fflush(stdout);
							char sbuf[16];
							scanf("%s", sbuf);

							char *tok;
							if(!(tok = strtok(sbuf, "-"))) {
								printf("Invalid range.\n");
								goto exit_from_switch;
							} else {
								blk_first = atoi(tok);
							}
							if(!(tok = strtok(NULL, "-"))) {
								blk_last = blk_first + 1;
							} else {
								blk_last = atoi(tok) + 1;
							}

							if(blk_first < 0 || blk_first > 258 || blk_last < 1 || blk_last > 259) {
								printf("Invalid range.\n");
								goto exit_from_switch;
							} else {
								break;
							}
						}

						canon_mode(false);

						for(int i = blk_first; i < blk_last; i++) {
							int av;
							ioctl(STDIN_FILENO, FIONREAD, &av);
							if(av > 0) {
								uint8_t b;
								read(STDIN_FILENO, &b, sizeof(uint8_t));
								if(b == 'q') {
									printf("Interrupted by user.\n");
									goto exit_from_switch;
								}
							}
	
							printf("Erasing block %d... ", i);
							fflush(stdout);
	
							/* unlocking a block */
							SERIAL_WRITE_BYTE(PL_CMD_FLASH_BLK_UNLOCK);
							if(SERIAL_READ_BYTE() != PL_VALID) {
								printf("Preloader: Invalid command\n");
								press_any_key();
								goto exit_from_switch;
							}
							if(write(serial_fd, &i, 2) < 0) {
								perror("write");
								press_any_key();
								goto exit_from_switch;
							}
	
							SERIAL_WRITE_BYTE(PL_CMD_FLASH_BLK_ERASE);
							if(SERIAL_READ_BYTE() != PL_VALID) {
								printf("Preloader: Invalid command\n");
								press_any_key();
								goto exit_from_switch;
							}
							if(write(serial_fd, &i, 2) < 0) {
								perror("write");
								press_any_key();
								goto exit_from_switch;
							}
							if(SERIAL_READ_BYTE() != 'd') {
								printf("FAIL\n");
								press_any_key();
								goto exit_from_switch;
							}
							printf("OK\n");
						}

						printf("%d block(s) have been successfully erased.\n", blk_last - blk_first);
						press_any_key();
						break;
					}

					case MENU_MAIN_FLASH_ERASE_CHIP: {
						printf("\nChip will be fully erased. Are you sure? [Y/n]\n");
						fflush(stdout);

						uint8_t kb_input;
						while(true) {
							read(0, &kb_input, sizeof(uint8_t));
							if(kb_input == 'Y' || kb_input == 'y' || kb_input == '\n' || kb_input == ' ') {
								break;
							} else if(kb_input == 'N' || kb_input == 'n') {
								goto exit_from_switch;
							}
						}

						/* unlocking chip */
						printf("Unprotecting the chip...\n");
						for(int i = 0; i < 259; i++) {
							SERIAL_WRITE_BYTE(PL_CMD_FLASH_BLK_UNLOCK);
							if(SERIAL_READ_BYTE() != PL_VALID) {
								printf("Preloader: Invalid command\n");
								press_any_key();
								break;
							}
							if(write(serial_fd, &i, 2) < 0) {
								perror("write");
								press_any_key();
								break;
							}
						}
	
						/* dangerous... */
						printf("Erasing the chip...\n");
						SERIAL_WRITE_BYTE(PL_CMD_FLASH_CHIP_ERASE);
						if(SERIAL_READ_BYTE() != PL_VALID) {
							printf("Preloader: Invalid command\n");
							press_any_key();
							break;
						}
						if(SERIAL_READ_BYTE() != 'd') {
							printf("Chip erasing failed.\n");
							press_any_key();
							break;
						}
						printf("Chip was erased successfully.\n");

						/* locking chip */
						printf("Protecting the chip...\n");
						for(int i = 0; i < 259; i++) {
							SERIAL_WRITE_BYTE(PL_CMD_FLASH_BLK_LOCK);
							if(SERIAL_READ_BYTE() != PL_VALID) {
								printf("Preloader: Invalid command\n");
								press_any_key();
								break;
							}
							if(write(serial_fd, &i, 2) < 0) {
								perror("write");
								press_any_key();
								break;
							}
						}

						press_any_key();
						break;
					}

					case MENU_MAIN_REBOOT_AFTER_FLASH: {
						if(!reboot_after_flash) {
							reboot_after_flash = true;
							get_button(menus[current_menu].entries, MENU_MAIN_REBOOT_AFTER_FLASH)->ansi = "\033[96m";
						} else {
							reboot_after_flash = false;
							get_button(menus[current_menu].entries, MENU_MAIN_REBOOT_AFTER_FLASH)->ansi = "\033[97m";
						}
						break;
					}

					case MENU_MAIN_REBOOT_AND_EXIT: {
						SERIAL_WRITE_BYTE(PL_CMD_JUMP);
						if(SERIAL_READ_BYTE() != PL_VALID) {
							printf("Preloader: Invalid command\n");
							press_any_key();
							quit(1);
						}

						uint32_t flash_start = FLASH_PHYS_START_ADDRESS;
						if(write(serial_fd, &flash_start, sizeof(uint32_t)) < 0) {
							perror("write");
							press_any_key();
							quit(1);
						}

						quit(0);
					}
				}
				break;

			case MENU_BAUD_RATE: {
				SERIAL_WRITE_BYTE(PL_CMD_BAUD);
				if(SERIAL_READ_BYTE() != PL_VALID) {
					printf("Preloader: Invalid command\n");
					press_any_key();
					break;
				}

				struct baud_divider bauddiv = baudrate_table[selected];
				
				/* set up baud on the device */
				if(write(serial_fd, &(bauddiv.divider), sizeof(uint32_t)) < 0) {
					perror("write");
					press_any_key();
					break;
				}

				usleep(100000);

				/* set up on the host machine itself */
				if(ioctl(serial_fd, TCGETS2, &tty) < 0) {
					perror("ioctl TCGETS2");
					press_any_key();
					break;
				}
				tty.c_cflag &= ~CBAUD;
				tty.c_cflag |= BOTHER;
				tty.c_ispeed = bauddiv.baud;
				tty.c_ospeed = bauddiv.baud;
				if(ioctl(serial_fd, TCSETS2, &tty) < 0) {
					perror("ioctl TCSETS2");
					press_any_key();
					break;
				}

				current_menu = MENU_MAIN;
				break;
			}
		}
exit_from_switch:
	}

end:
	return 0;
}
