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
#include <readline/readline.h>
#include <readline/history.h>
#include "ctrlsym.h"
#include "menu_layout.h"
#include "uart.h"
#include "preloader.h"
#include "ihex.h"
#include "menu.h"
#include "tfs.h"

#define ITCM_PHYS_START_ADDRESS			0x1f400000
#define FLASH_PHYS_START_ADDRESS		0x90000000
#define FLASH_OTP_LENGTH				0x200

#define FLASH_BLK_SIZE(blk)				(blk < 255 ? 0x20000 : 0x8000)

#define TFS_SECTS	0x7800

/* baud rate and its divider */
struct baud_divider {
	uint32_t baud;
	uint32_t divider;
};

static char *program_name;

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
};


static int current_menu = MENU_MAIN;

/* long options */
static const struct option longopts[] = {
	{"help",      0, NULL, 'h'},
	{"device",    1, NULL, 'd'},
	{NULL, 0, NULL, 0},
};

/* default serial device */
static char serial_device[256] = "/dev/ttyUSB0";
/* serial UNIX file descriptor */
static int serial_fd;
/* reboot after flashing */
static bool reboot_after_flash = false;

/* flashing */
static bool bin_flashing = false;
static char bin_file[256];
static int bin_blk_first;
static bool tfs_flashing = false;
static char tfs_file[256];
static bool csc_flashing = false;
static char csc_file[256];

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

/* read one byte from a device (blocking operation) */
static int serial_read_byte() {
	uint8_t b;
	int status = read(serial_fd, &b, sizeof(uint8_t));
	if(status < 0) {
		return status;
	}

	return b;
}

/* read fixed bytes from a device (blocking operation) */
static int serial_read_fixed(void *buf, int count) {
	int n_read = 0;
	do {
		int n = read(serial_fd, buf + n_read, count - n_read);
		if(n > 0) {
			n_read += n;
		} else if(n <= 0) {
			if(n == 0) errno = EBADF;
			return -1;
		}

	} while(n_read < count);

	return count;
}

/* send one byte to a device (blocking operation) */
static int serial_send_byte(uint8_t b) {
	return write(serial_fd, &b, sizeof(uint8_t));
}

/* enable/disable canonical mode on this terminal */
static void canon_mode(bool enable) {
	struct termios2 main_tty;
	if(ioctl(0, TCGETS2, &main_tty) != 0) {
		perror("ioctl TCGETS2");
		exit(1);
	}

	if(enable)
		main_tty.c_lflag |= ICANON;
	else
		main_tty.c_lflag &= ~ICANON;

	if(ioctl(0, TCSETS2, &main_tty) < 0) {
		perror("ioctl TCSETS2");
		exit(1);
	}
}

/* press any key message */
static void press_any_key(void) {
	printf("Press any key...\n");
	uint8_t b;
	read(STDIN_FILENO, &b, sizeof(uint8_t));
	canon_mode(false);
}

/* quit with canonical mode returning */
static void quit(int exit_code) {
	canon_mode(true);
	close(serial_fd);
	exit(exit_code);
}

/* signal handler */
static void sig_handler(int sig) {
	switch(sig) {
		case SIGINT:
			quit(1);
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

/* yes/no choice */
static bool yes_no_choice(uint8_t *string) {
	printf("\r");
	fflush(stdout);

	char *input = readline(string);
	if(!input)
		quit(1);

	if(!strcasecmp(input, "y")) {
		free(input);
		return true;
	}

	free(input);
	return false;
}

/* update MENU_MAIN_FLASH_START button state */
static void update_flash_button_enabled() {
	if(bin_flashing || tfs_flashing || csc_flashing)
		get_button(menus[MENU_MAIN].entries, MENU_MAIN_FLASH_START)->button_enabled = true;
	else
		get_button(menus[MENU_MAIN].entries, MENU_MAIN_FLASH_START)->button_enabled = false;
}

/* flash (un)protect range */
static bool flash_protect_range(bool protect, int start, int end) {
	for(int blk = start; blk < end + 1; blk++) {
		serial_send_byte(protect ? PL_CMD_FLASH_BLK_LOCK : PL_CMD_FLASH_BLK_UNLOCK);
		if(serial_read_byte() != PL_VALID) {
			printf("Preloader: Invalid command\n");
			return false;
		}

		if(write(serial_fd, &blk, sizeof(uint16_t)) < 0) {
			perror("write");
			return false;
		}
	}

	return true;
}

/* flash TFS function */
static bool flash_tfs(uint8_t *ctrl, uint16_t ctrl_sect, uint8_t *sect_marks, bool csc) {

	uint8_t *tfs, *cfg;
	uint32_t tfs_size, cfg_size;

	FILE *fd;

	/* reading .tfs file */
	fd = fopen(csc ? csc_file : tfs_file, "rb");
	if(!fd) {
		perror(csc ? csc_file : tfs_file);
		return false;
	}
	fseek(fd, 0, SEEK_END);
	tfs_size = ftell(fd);
	fseek(fd, 0, SEEK_SET);

	/* allocating memory for tfs file */
	tfs = malloc(tfs_size);
	if(!tfs) {
		perror("malloc");
		return false;
	}
	fread(tfs, 1, tfs_size, fd);
	fclose(fd);

	/* reading .cfg file */
	char cfg_file[256];
	int cfg_file_len = snprintf(cfg_file, sizeof(cfg_file) - 4, csc ? csc_file : tfs_file);
	char *dot = strrchr(cfg_file, '.');
	if(dot)
		strcpy(dot, csc ? ".ccf" : ".cfg");
	else
		strcpy(&cfg_file[cfg_file_len], csc ? ".ccf" : ".cfg");

	fd = fopen(cfg_file, "rb");
	if(!fd) {
		perror(cfg_file);
		return false;
	}
	fseek(fd, 0, SEEK_END);
	cfg_size = ftell(fd);
	fseek(fd, 0, SEEK_SET);
	cfg = malloc(cfg_size);
	if(!cfg) {
		free(tfs);
		perror("malloc");
		return false;
	}
	fread(cfg, 1, cfg_size, fd);
	fclose(fd);
	
	/* sector actions */
	struct sect_action_list act_list = {
		.num_acts = 0,
		.acts = NULL,
	};

	/* let's go! */
	printf("Formatting TFS file system...\n\n");
	if(!tfs4_patch(tfs, cfg, tfs_size, cfg_size, TFS_SECTS, &act_list, ctrl, ctrl_sect, sect_marks, !csc)) {
		if(act_list.acts) free(act_list.acts);
		free(tfs);
		free(cfg);
		printf("\nError occurred during TFS formatting\n\n");
		return false;
	}
	printf("\nFormatting done!\nNumber of the sector actions need to be performed: %d\n\n", act_list.num_acts);

	free(tfs);
	free(cfg);

	/* unprotecting TFS */
	if(!flash_protect_range(false, 128, 247))
		return false;

	/* erasing TFS if we're flashing TFS */
	if(!csc) {
		for(int i = 128; i < 248; i++) {
			printf("Erasing block %d... ", i);
			fflush(stdout);

			serial_send_byte(PL_CMD_FLASH_BLK_ERASE);
			if(serial_read_byte() != PL_VALID) {
				printf("Preloader: Invalid command\n");
				return false;
			}

			if(write(serial_fd, &i, sizeof(uint16_t)) < 0) {
				perror("write");
				return false;
			}

			/* waiting when operation will be done */
			if(serial_read_byte() != CTRL_EOT) {
				printf("FAIL\n");
				return false;
			}
			printf("OK\n");
		}
	}

	/* sending new sectors into phone */
	printf("Flashing sectors...\n\n");
	for(int i = 0; i < act_list.num_acts; i++) {
		switch(act_list.acts[i].type) {
			case SECT_ACTION_WRITE:
			case SECT_ACTION_ERASE_AND_WRITE: {
				serial_send_byte(PL_CMD_TFS_SECT_WRITE);
				if(serial_read_byte() != PL_VALID) {
					printf("Preloader: Invalid command\n");
					return false;
				}
				if(write(serial_fd, &act_list.acts[i].sect_num, sizeof(uint16_t)) < 0) {
					perror("write");
					return false;
				}
				serial_send_byte(act_list.acts[i].type == SECT_ACTION_ERASE_AND_WRITE);
				if(write(serial_fd, act_list.acts[i].sect_data, TFS_PAGE_SIZE) < 0) {
					perror("write");
					return false;
				}
				serial_send_byte(calc_checksum(act_list.acts[i].sect_data, TFS_PAGE_SIZE));
				if(serial_read_byte() != CTRL_ACK) {
					printf("Checksum error at %d sector\n", act_list.acts[i].sect_num);
					return false;
				}
				break;
			}

			case SECT_ACTION_MARK: {
				serial_send_byte(PL_CMD_TFS_MARK_SECT);
				if(serial_read_byte() != PL_VALID) {
					printf("Preloader: Invalid command\n");
					press_any_key();
					break;
				}
				if(write(serial_fd, &act_list.acts[i].sect_num, sizeof(uint16_t)) < 0) {
					perror("write");
					press_any_key();
					break;
				}
				serial_send_byte(act_list.acts[i].sect_data[TFS_SECT_SIZE]);
				break;
			}
		}
	}
	
	/* protecting TFS again */
	if(!flash_protect_range(true, 128, 247))
		return false;

	printf("TFS has been flashed.\n");

	if(act_list.acts) free(act_list.acts);

	return true;
}

/* main function */
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

	uint8_t kb_input;

	while(true) {
back:
		ioctl(serial_fd, TCFLSH, TCIFLUSH);

		int selected;
		if((selected = draw_menu(menus[current_menu].entries, menus[current_menu].selected)) == -1) {

			if(current_menu != MENU_MAIN) {
				current_menu = MENU_MAIN;
				goto back;
			}

			if(yes_no_choice("Are you sure? [y/N] "))
				quit(0);
			else
				goto back;

		}

		menus[current_menu].selected = selected;
		fputc('\n', stdout);

		switch(current_menu) {
			struct termios2 tty;

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
							serial_send_byte(CTRL_SYN);
							usleep(10000);
							uint32_t av;
							ioctl(serial_fd, FIONREAD, &av);
							if(av > 0) {
								if(serial_read_byte() == CTRL_SYN)
									break;
							}
						}

						printf("Sending preloader (%d bytes)...\n", preloader_data_size);
						if(!create_ihex(serial_fd, ITCM_PHYS_START_ADDRESS, preloader_data, preloader_data_size)) {
							printf("Unable to create IHEX file!\n");
							press_any_key();
							break;
						}
						uint8_t err;
						if((err = serial_read_byte()) != '0') {
							printf("BootROM error: %02X.\n", err);
							press_any_key();
							break;
						}
						
						printf("Performing init handshake...\n");
						while(serial_read_byte() != CTRL_ENQ) {
						}
						serial_send_byte(CTRL_ACK);

						press_any_key();

						get_button(menus[MENU_MAIN].entries, MENU_MAIN_SERIAL_DEVICE)->button_enabled = false;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_DOWNLOAD_PRELOADER)->button_enabled = false;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_BAUD_RATE)->button_enabled = true;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_FLASH_ID)->button_enabled = true;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_FLASH_READ)->button_enabled = true;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_FLASH_READ_OTP)->button_enabled = true;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_FLASH_BIN)->button_enabled = true;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_FLASH_TFS)->button_enabled = true;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_FLASH_CSC)->button_enabled = true;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_REBOOT_AFTER_FLASH)->button_enabled = true;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_FLASH_START)->button_enabled = false;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_FLASH_ERASE)->button_enabled = true;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_FLASH_ERASE_CHIP)->button_enabled = true;
						get_button(menus[MENU_MAIN].entries, MENU_MAIN_REBOOT_AND_EXIT)->button_enabled = true;
						break;
					}

					case MENU_MAIN_SERIAL_DEVICE: {
						char *input = readline("Path to a character device: ");
						if(!input)
							quit(1);
						add_history(input);
						strncpy(serial_device, input, sizeof(serial_device) - 1);
						free(input);
						break;
					}

					case MENU_MAIN_BAUD_RATE: {
						current_menu = MENU_BAUD_RATE;
						break;
					}

					case MENU_MAIN_FLASH_ID: {
						serial_send_byte(PL_CMD_FLASH_GET_INFO);
						if(serial_read_byte() != PL_VALID) {
							printf("Preloader: Invalid command\n");
							press_any_key();
							break;
						}

						uint16_t manufacturer_id;
						uint16_t device_id;
						serial_read_fixed(&manufacturer_id, sizeof(uint16_t));
						serial_read_fixed(&device_id, sizeof(uint16_t));
						
						printf("Manufacturer ID: 0x%04X\n", manufacturer_id);
						printf("Device ID: 0x%04X\n", device_id);
						press_any_key();
						break;
					}

					case MENU_MAIN_FLASH_READ: {
						int blk_first;
						int blk_last;

						char *input;

						input = readline("Enter a block range (0-258): ");
						if(!input)
							quit(1);
						char *tok;
						if(!(tok = strtok(input, "-"))) {
							printf("Invalid range.\n");
							press_any_key();
							break;
						} else {
							blk_first = atoi(tok);
						}
						if(!(tok = strtok(NULL, "-"))) {
							blk_last = blk_first + 1;
						} else {
							blk_last = atoi(tok) + 1;
						}
						free(input);

						if(blk_first < 0 || blk_first > 258 || blk_last < 1 || blk_last > 259) {
							printf("Invalid range.\n");
							press_any_key();
							break;
						}

						input = readline("Output path: ");
						if(!input)
							quit(1);
						add_history(input);

						FILE *out_fd = fopen(input, "wb");
						free(input);
						if(!out_fd) {
							perror(input);
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
									free(blk_buf);
									fclose(out_fd);
									printf("Interrupted by user.\n");
									press_any_key();
									goto back;
								}
							}

							printf("Reading block %d... ", i);
							fflush(stdout);

							serial_send_byte(PL_CMD_FLASH_BLK_READ);
							if(serial_read_byte() != PL_VALID) {
								free(blk_buf);
								fclose(out_fd);
								printf("Preloader: Invalid command\n");
								press_any_key();
								goto back;
							}
							int blk = i;
							if(write(serial_fd, &blk, sizeof(uint16_t)) < 0) {
								free(blk_buf);
								fclose(out_fd);
								perror("write");
								press_any_key();
								goto back;
							}

							uint32_t length = FLASH_BLK_SIZE(blk);

							serial_read_fixed(blk_buf, length);
							uint8_t checksum = serial_read_byte();
							if(checksum == calc_checksum(blk_buf, length)) {
								printf("OK\n");
								fwrite(blk_buf, 1, length, out_fd);
							} else {
								free(blk_buf);
								fclose(out_fd);
								printf("CHECKSUM WRONG\n");
								press_any_key();
								goto back;
							}
						}

						free(blk_buf);
						fclose(out_fd);

						printf("%d block(s) have been successfully dumped.\n", blk_last - blk_first);
						press_any_key();

						break;
					}

					case MENU_MAIN_FLASH_READ_OTP: {
						char *out_path = readline("Output path: ");
						if(!out_path)
							quit(1);
						add_history(out_path);

						FILE *out_fd = fopen(out_path, "wb");
						if(!out_fd) {
							free(out_path);
							perror(out_path);
							press_any_key();
							break;
						}
						free(out_path);

						uint8_t otp_buf[FLASH_OTP_LENGTH];

						serial_send_byte(PL_CMD_FLASH_OTP_READ);
						if(serial_read_byte() != PL_VALID) {
							printf("Preloader: Invalid command\n");
							press_any_key();
							fclose(out_fd);
							goto back;
						}

						printf("Dumping OTP... ");
						fflush(stdout);

						serial_read_fixed(otp_buf, FLASH_OTP_LENGTH);

						if(serial_read_byte() != calc_checksum(otp_buf, FLASH_OTP_LENGTH))
							printf("CHECKSUM WRONG\n");
						else
							printf("OK\n");

						fwrite(otp_buf, 1, FLASH_OTP_LENGTH, out_fd);
						fclose(out_fd);

						press_any_key();

						break;
					}

					case MENU_MAIN_FLASH_BIN: {
						if(!bin_flashing) {
							char *input;

							input = readline("Path to a binary (.cla/.bin): ");
							if(!input)
								quit(1);
							add_history(input);
							strncpy(bin_file, input, sizeof(bin_file));
							free(input);

							input = readline("Begin block (0-258): ");
							if(!input)
								quit(1);
							bin_blk_first = atoi(input);
							free(input);

							get_button(menus[current_menu].entries, MENU_MAIN_FLASH_BIN)->ansi = "\033[96m";
							bin_flashing = true;
						} else {
							get_button(menus[current_menu].entries, MENU_MAIN_FLASH_BIN)->ansi = "\033[97m";
							bin_flashing = false;
						}

						update_flash_button_enabled();

						break;
					}

					case MENU_MAIN_FLASH_TFS: {
						if(!tfs_flashing) {
							char *input = readline("Path to a TFS (.tfs) file: ");
							if(!input)
								quit(1);
							add_history(input);
							strncpy(tfs_file, input, sizeof(bin_file));
							free(input);

							get_button(menus[current_menu].entries, MENU_MAIN_FLASH_TFS)->ansi = "\033[96m";
							tfs_flashing = true;
						} else {
							get_button(menus[current_menu].entries, MENU_MAIN_FLASH_TFS)->ansi = "\033[97m";
							tfs_flashing = false;
						}

						update_flash_button_enabled();

						break;
					}

					case MENU_MAIN_FLASH_CSC: {
						if(!csc_flashing) {
							char *input = readline("Path to a CSC (.csc) file: ");
							if(!input)
								quit(1);
							add_history(input);
							strncpy(csc_file, input, sizeof(bin_file));
							free(input);

							get_button(menus[current_menu].entries, MENU_MAIN_FLASH_CSC)->ansi = "\033[96m";
							csc_flashing = true;
						} else {
							get_button(menus[current_menu].entries, MENU_MAIN_FLASH_CSC)->ansi = "\033[97m";
							csc_flashing = false;
						}

						update_flash_button_enabled();

						break;
					}

					case MENU_MAIN_FLASH_START: {
						/* ---------- BIN flashing ---------- */
						if(bin_flashing) {
							printf("Starting binary flashing...\n\n");

							int blk_first = bin_blk_first;

							if(blk_first < 0 || blk_first > 258) {
								printf("Invalid block index.\n");
								press_any_key();
								goto back;
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
							for(int i = bin_size; i > 0; i -= FLASH_BLK_SIZE(blk_last++)) {
							}

							if(blk_last > 259) {
								printf("Binary doesn't fit into the flash memory address space.\n");
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
								break;
							}

							for(int i = blk_first; i < blk_last; i++) {
								int av;
								ioctl(STDIN_FILENO, FIONREAD, &av);
								if(av > 0) {
									uint8_t b;
									read(STDIN_FILENO, &b, sizeof(uint8_t));
									if(b == 'q') {
										free(blk_buf);
										fclose(bin_fd);
										printf("Interrupted by user.\n");
										press_any_key();
										goto back;
									}
								}

								/* unlocking a block */
								serial_send_byte(PL_CMD_FLASH_BLK_UNLOCK);
								if(serial_read_byte() != PL_VALID) {
									free(blk_buf);
									fclose(bin_fd);
									printf("Preloader: Invalid command\n");
									press_any_key();
									goto back;
								}
								if(write(serial_fd, &i, sizeof(uint16_t)) < 0) {
									free(blk_buf);
									fclose(bin_fd);
									perror("write");
									press_any_key();
									goto back;
								}

								uint32_t length = FLASH_BLK_SIZE(i);

								uint32_t n_read = fread(blk_buf, 1, length, bin_fd);
								memset(blk_buf + n_read, 0xff, length - n_read);

								n_read += (n_read & 1);

								printf("Writing block %d... ", i);
								fflush(stdout);

								/* programming */
								serial_send_byte(PL_CMD_FLASH_BLK_PROGRAM);
								if(serial_read_byte() != PL_VALID) {
									free(blk_buf);
									fclose(bin_fd);
									printf("Preloader: Invalid command\n");
									press_any_key();
									goto back;
								}
								if(write(serial_fd, &i, sizeof(uint16_t)) < 0) {
									free(blk_buf);
									fclose(bin_fd);
									perror("write");
									press_any_key();
									goto back;
								}

								uint32_t blk_wrd_count = n_read >> 1;
								if(write(serial_fd, &blk_wrd_count, sizeof(uint32_t)) < 0) {
									free(blk_buf);
									fclose(bin_fd);
									perror("write");
									press_any_key();
									goto back;
								}

								if(write(serial_fd, blk_buf, n_read) < 0) {
									free(blk_buf);
									fclose(bin_fd);
									perror("write");
									press_any_key();
									goto back;
								}
								serial_send_byte(calc_checksum(blk_buf, n_read));

								if(serial_read_byte() != CTRL_ACK) {
									free(blk_buf);
									fclose(bin_fd);
									printf("CHECKSUM WRONG\n");
									press_any_key();
									goto back;
								} else {
									/* waiting when operation will be done */
									if(serial_read_byte() != CTRL_EOT) {
										free(blk_buf);
										fclose(bin_fd);
										printf("FAIL\n");
										press_any_key();
										goto back;
									}
									printf("OK\n");
								}
							}

							free(blk_buf);
							fclose(bin_fd);

							printf("%d block(s) have been successfully reflashed.\n\n", blk_last - blk_first);
						}

						/* ---------- TFS flashing ---------- */
						if(tfs_flashing) {
							uint8_t *sect_marks = malloc(TFS_SECTS);
							if(!sect_marks) {
								perror("malloc");
								press_any_key();
								break;
							}

							flash_tfs(NULL, 0, sect_marks, false);

							free(sect_marks);
						}

						/* ---------- CSC flashing ---------- */
						if(csc_flashing) {
							serial_send_byte(PL_CMD_TFS_READ_SECT_MARKS);
							if(serial_read_byte() != PL_VALID) {
								printf("Preloader: Invalid command\n");
								press_any_key();
								break;
							}

							uint16_t start_sect = 0;
							uint16_t num_sects = TFS_SECTS;

							if(write(serial_fd, &start_sect, sizeof(uint16_t)) < 0) {
								perror("write");
								press_any_key();
								break;
							}
							if(write(serial_fd, &num_sects, sizeof(uint16_t)) < 0) {
								perror("write");
								press_any_key();
								break;
							}

							/* dumping compressed sect marks array from the phone */
							printf("Dumping compressed sector marks array...\n\n");
							uint8_t sect_marks_compressed[(num_sects >> 2) + (num_sects & 3 != 0)];
							for(int i = 0; i < sizeof(sect_marks_compressed); i++)
								sect_marks_compressed[i] = serial_read_byte();

							/* decompressing sector markers */
							uint8_t *sect_marks = malloc(num_sects);
							if(!sect_marks) {
								perror("malloc");
								press_any_key();
								break;
							}
							uint8_t tmp_mark;
							for(int i = 0; i < num_sects; i++) {
								if(!(i & 3))
									tmp_mark = sect_marks_compressed[i >> 2];

								sect_marks[i] =
									tmp_mark >= 0xc0 ? 0xff :
									tmp_mark >= 0x80 ? 0xf0 :
									0x00;

								tmp_mark <<= 2;
							}

							/* getting total control size */
							uint32_t ctrl_sect_num = 0;
							uint32_t ctrl_size = 0;
							for(int i = 0; i < num_sects; i++) {
								if(sect_marks[i] == 0xf0)
									ctrl_sect_num++;
							}
							ctrl_size = ctrl_sect_num * (TFS_SECT_SIZE);

							/* reading control */
							uint8_t *ctrl = malloc(ctrl_size);
							if(!ctrl) {
								perror("malloc");
								press_any_key();
								break;
							}
							uint8_t *p_ctrl = ctrl;
							uint16_t *sect_nums = malloc(ctrl_sect_num * sizeof(uint16_t));
							printf("Reading control sectors...\n\n");
							for(int s = 0, i = 0; i < num_sects; i++) {
								if(sect_marks[i] == 0xf0) {
									serial_send_byte(PL_CMD_TFS_SECT_READ);
									if(serial_read_byte() != PL_VALID) {
										printf("Preloader: Invalid command\n");
										press_any_key();
										goto back;
									}

									if(write(serial_fd, &i, sizeof(uint16_t)) < 0) {
										perror("write");
										press_any_key();
										quit(1);
									}
									serial_read_fixed(p_ctrl, TFS_SECT_SIZE);
									uint8_t checksum = serial_read_byte();
									if(checksum != calc_checksum(p_ctrl, TFS_SECT_SIZE)) {
										printf("Checksum error while reading a control at %d sector\n", i);
										press_any_key();
										goto back;
									}

									p_ctrl += TFS_SECT_SIZE;

									sect_nums[s++] = i;
								}
							}

							/* allocating new final control and freeing old */
							uint16_t ctrl_sect;
							uint8_t *new_ctrl = tfs4_ctrl_fix_sect_order(ctrl, ctrl_size, sect_nums, ctrl_sect_num, &ctrl_sect);
							free(ctrl);
							if(!new_ctrl) {
								free(sect_marks);
								press_any_key();
								break;
							}

							/* now like in TFS flashing, but CSC */
							flash_tfs(new_ctrl, ctrl_sect, sect_marks, true);

							free(sect_marks);
							free(new_ctrl);
						}

						if(reboot_after_flash) {
							serial_send_byte(PL_CMD_JUMP);
							if(serial_read_byte() != PL_VALID) {
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

						printf("Flashing complete!\n\n");

						press_any_key();
						break;
					}

					case MENU_MAIN_FLASH_ERASE: {
						int blk_first;
						int blk_last;

						while(true) {
							char *input = readline("Enter a block range (0-258): ");
							if(!input)
								quit(1);

							char *tok;
							if(!(tok = strtok(input, "-"))) {
								printf("Invalid range.\n");
								press_any_key();
								goto back;
							} else {
								blk_first = atoi(tok);
							}
							if(!(tok = strtok(NULL, "-"))) {
								blk_last = blk_first + 1;
							} else {
								blk_last = atoi(tok) + 1;
							}
							free(input);

							if(blk_first < 0 || blk_first > 258 || blk_last < 1 || blk_last > 259) {
								printf("Invalid range.\n");
								press_any_key();
								goto back;
							} else {
								break;
							}
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
									goto back;
								}
							}
	
							printf("Erasing block %d... ", i);
							fflush(stdout);
	
							/* unlocking a block */
							serial_send_byte(PL_CMD_FLASH_BLK_UNLOCK);
							if(serial_read_byte() != PL_VALID) {
								printf("Preloader: Invalid command\n");
								press_any_key();
								goto back;
							}
							if(write(serial_fd, &i, sizeof(uint16_t)) < 0) {
								perror("write");
								press_any_key();
								goto back;
							}
	
							serial_send_byte(PL_CMD_FLASH_BLK_ERASE);
							if(serial_read_byte() != PL_VALID) {
								printf("Preloader: Invalid command\n");
								press_any_key();
								goto back;
							}
							if(write(serial_fd, &i, sizeof(uint16_t)) < 0) {
								perror("write");
								press_any_key();
								goto back;
							}

							/* waiting when operation will be done */
							if(serial_read_byte() != CTRL_EOT) {
								printf("FAIL\n");
								press_any_key();
								goto back;
							}
							printf("OK\n");
						}

						printf("%d block(s) have been successfully erased.\n", blk_last - blk_first);
						press_any_key();
						break;
					}

					case MENU_MAIN_FLASH_ERASE_CHIP: {
						if(!yes_no_choice("Flash chip will be fully erased. Are you sure? [y/N] "))
							break;

						/* unprotecting chip */
						printf("Unprotecting the chip...\n");
						flash_protect_range(false, 0, 258);
	
						/* dangerous... */
						printf("Erasing the chip...\n");
						serial_send_byte(PL_CMD_FLASH_CHIP_ERASE);
						if(serial_read_byte() != PL_VALID) {
							printf("Preloader: Invalid command\n");
							press_any_key();
							break;
						}

						/* waiting when operation will be done */
						if(serial_read_byte() != CTRL_EOT) {
							printf("Chip erasing failed.\n");
							press_any_key();
							break;
						}
						printf("Chip was erased successfully.\n");

						/* protecting chip */
						printf("Protecting the chip...\n");
						flash_protect_range(true, 0, 258);

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
						serial_send_byte(PL_CMD_JUMP);
						if(serial_read_byte() != PL_VALID) {
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
				serial_send_byte(PL_CMD_BAUD);
				if(serial_read_byte() != PL_VALID) {
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
}
