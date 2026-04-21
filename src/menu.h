#ifndef _MENU_H
#define _MENU_H 1

#include <stdbool.h>

typedef enum {
	MENU_TYPE_END,
	MENU_TYPE_SPACER,
	MENU_TYPE_LABEL,
	MENU_TYPE_BUTTON
} MENU_TYPE;

struct menu_entry {
	MENU_TYPE type;
	union {
		int space_height;
		char *label;
	};
	char *ansi;
	bool button_enabled;
};

extern struct menu_entry *get_button(struct menu_entry entries[], int button);

extern int draw_menu(struct menu_entry entries[], int selected);

#endif /* _MENU_H */
