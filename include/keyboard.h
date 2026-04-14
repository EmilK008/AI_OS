#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

/* Special key codes (above ASCII range) */
#define KEY_UP     0x80
#define KEY_DOWN   0x81
#define KEY_LEFT   0x82
#define KEY_RIGHT  0x83
#define KEY_HOME   0x84
#define KEY_END    0x85
#define KEY_DELETE 0x86
#define KEY_PGUP   0x87
#define KEY_PGDN   0x88

void keyboard_init(void);
void keyboard_handler(void);
bool keyboard_has_key(void);
char keyboard_getchar(void);
void keyboard_set_gui_mode(bool enabled);
void keyboard_inject_char(uint8_t c);

#endif
