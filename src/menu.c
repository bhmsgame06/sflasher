#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include "menu.h"

#define READ_KB_INPUT	(read(STDIN_FILENO, &kb_input, sizeof(uint8_t)), kb_input)

struct menu_entry *get_button(struct menu_entry entries[], int button) {
	int bi = 0;
	for(int i = 0; ; i++) {
		if(entries[i].type == MENU_TYPE_BUTTON) {
			if(bi++ == button) {
				return &entries[i];
			}
		}
	}
}

int draw_menu(struct menu_entry entries[], int selected) {
	int count;
	for(count = 0; entries[count].type != MENU_TYPE_END; count++) {}

	if(count > 0) {
		int buttons[count];
		int buttons_length = 0;
		for(int i = 0; i < count; i++) {
			if(entries[i].type == MENU_TYPE_BUTTON)
				buttons[buttons_length++] = i;
		}

		while(true) {
			printf("\033[H\033[J\033[3J");

			for(int i = 0; i < count; i++) {
				switch(entries[i].type) {

					case MENU_TYPE_SPACER:
						for(int sp = 0; sp < entries[i].space_height; sp++) {
							fputc('\n', stdout);
						}
						break;

					case MENU_TYPE_LABEL:
						printf("  ");
						printf(entries[i].label);
						fputc('\n', stdout);
						break;

					case MENU_TYPE_BUTTON:
						if(buttons[selected] == i) {
							printf("  \033[94m>\033[0m ");
						} else {
							printf("    ");
						}

						if(!entries[i].button_enabled)
							printf("\033[90m");
						else if(entries[i].ansi)
							printf(entries[i].ansi);

						printf(entries[i].label);

						if(entries[i].ansi || !entries[i].button_enabled) printf("\033[0m");

						fputc('\n', stdout);
						break;

					default:
						break;

				}
			}

			uint8_t kb_input;
			switch(READ_KB_INPUT) {
				case '\033':
					if(READ_KB_INPUT == '[') {
						/* arrows */
						switch(READ_KB_INPUT) {
							/* up */
							case 'A':
								selected--;
								break;

							/* down */
							case 'B':
								selected++;
								break;
						}
					}
					break;

				/* arrows again (vi) */

				/* up */
				case 'k':
					selected--;
					break;

				/* down */
				case 'j':
					selected++;
					break;

				/* enter/space */
				case '\n':
				case ' ':
					if(entries[buttons[selected]].button_enabled)
						return selected;
					break;

				/* quit */
				case 'q':
					return -1;
			}

			/* check if selected index out of bounds */
			if(selected >= buttons_length)
				selected = 0;
			else if(selected < 0)
				selected = buttons_length - 1;
		}
	}

	return -1;
}
