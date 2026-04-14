#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"

void mouse_init(void);
void mouse_handler(void);
int  mouse_get_x(void);
int  mouse_get_y(void);
bool mouse_left_held(void);
bool mouse_right_held(void);
bool mouse_left_click(void);
bool mouse_right_click(void);
void mouse_clear_clicks(void);
void mouse_set_bounds(int max_x, int max_y);

#endif
