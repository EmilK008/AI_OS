#ifndef APPMENU_H
#define APPMENU_H

#include "types.h"

void appmenu_toggle(void);
void appmenu_close(void);
bool appmenu_is_open(void);
int  appmenu_hit_test(int mx, int my);  /* returns item index or -1 */
void appmenu_on_click(int item);
void appmenu_render(void);

#endif
