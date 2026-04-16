#ifndef SETTINGS_H
#define SETTINGS_H

#include "types.h"

void settings_create(void);
void settings_render(void);
bool settings_is_alive(void);
void settings_save(void);
void settings_load(void);

#endif
