#ifndef EVENT_H
#define EVENT_H

#include "types.h"

typedef enum {
    EVT_NONE = 0,
    EVT_MOUSE_MOVE,
    EVT_MOUSE_DOWN,
    EVT_MOUSE_UP,
    EVT_KEY_PRESS,
    EVT_MOUSE_SCROLL,        /* scroll_delta: wheel ticks (see mouse driver) */
} event_type_t;

struct gui_event {
    event_type_t type;
    int32_t  mouse_x, mouse_y;
    int8_t   scroll_delta;   /* wheel: positive = scroll down (content moves up) */
    uint8_t  mouse_button;   /* 0=left, 1=right, 2=middle */
    uint8_t  key;
};

void event_init(void);
void event_push(struct gui_event *evt);
bool event_pop(struct gui_event *evt);

#endif
