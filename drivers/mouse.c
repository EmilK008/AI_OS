/* ===========================================================================
 * PS/2 Mouse Driver (IRQ12)
 * 3-byte packet protocol, absolute position tracking
 * =========================================================================== */
#include "mouse.h"
#include "io.h"
#include "event.h"

static volatile int32_t mouse_x = 320;
static volatile int32_t mouse_y = 240;
static volatile bool    button_left = false;
static volatile bool    button_right = false;
static volatile bool    button_middle = false;
static volatile bool    click_left = false;
static volatile bool    click_right = false;
static volatile int     packet_byte = 0;
static volatile uint8_t packet[4];
static bool             scroll_wheel_enabled = false;
static int max_x = 640;
static int max_y = 480;
static int mouse_speed = 2;  /* 1=slow, 2=normal, 3=fast */

static void mouse_wait_input(void) {
    int timeout = 100000;
    while (timeout--) {
        if (!(inb(0x64) & 0x02)) return;
    }
}

static void mouse_wait_output(void) {
    int timeout = 100000;
    while (timeout--) {
        if (inb(0x64) & 0x01) return;
    }
}

static void mouse_write(uint8_t data) {
    mouse_wait_input();
    outb(0x64, 0xD4);    /* tell controller: next byte goes to mouse */
    mouse_wait_input();
    outb(0x60, data);
}

static uint8_t mouse_read(void) {
    mouse_wait_output();
    return inb(0x60);
}

/* Send byte to mouse; consume 0xFA ACK. Returns false if not ACK. */
static bool mouse_send_byte(uint8_t b) {
    mouse_write(b);
    return mouse_read() == 0xFA;
}

void mouse_init(void) {
    /* Enable auxiliary device */
    mouse_wait_input();
    outb(0x64, 0xA8);

    /* Enable IRQ12 in controller config */
    mouse_wait_input();
    outb(0x64, 0x20);    /* read compaq status */
    mouse_wait_output();
    uint8_t status = inb(0x60);
    status |= 0x02;      /* enable IRQ12 */
    status &= ~0x20;     /* enable mouse clock */
    mouse_wait_input();
    outb(0x64, 0x60);    /* write compaq status */
    mouse_wait_input();
    outb(0x60, status);

    /* Set defaults */
    mouse_write(0xF6);
    mouse_read(); /* ACK */

    /* Enable data reporting */
    mouse_write(0xF4);
    mouse_read(); /* ACK */

    /* Intellimouse: try to enable scroll wheel (4-byte packets, id 3 or 4). */
    scroll_wheel_enabled = false;
    if (mouse_send_byte(0xF3) && mouse_send_byte(200) &&
        mouse_send_byte(0xF3) && mouse_send_byte(100) &&
        mouse_send_byte(0xF3) && mouse_send_byte(80) &&
        mouse_send_byte(0xF2)) {
        uint8_t id = mouse_read();
        if (id == 3 || id == 4)
            scroll_wheel_enabled = true;
    }

    packet_byte = 0;
}

void mouse_handler(void) {
    uint8_t data = inb(0x60);
    int need = scroll_wheel_enabled ? 4 : 3;

    if (packet_byte == 0) {
        if (!(data & 0x08))
            goto eoi;
        packet[0] = data;
        packet_byte = 1;
        goto eoi;
    }
    if (packet_byte == 1) {
        packet[1] = data;
        packet_byte = 2;
        goto eoi;
    }
    if (packet_byte == 2) {
        packet[2] = data;
        if (need == 4) {
            packet_byte = 3;
            goto eoi;
        }
        packet_byte = 0;
    } else if (packet_byte == 3) {
        packet[3] = data;
        packet_byte = 0;
    } else {
        packet_byte = 0;
        goto eoi;
    }

    /* Full packet (3 bytes without wheel, 4 bytes with wheel) */
    {
        int32_t dx = (int32_t)packet[1];
        int32_t dy = (int32_t)packet[2];

        if (packet[0] & 0x10) dx |= 0xFFFFFF00;
        if (packet[0] & 0x20) dy |= 0xFFFFFF00;

        if (packet[0] & 0xC0)
            goto eoi;

        dy = -dy;

        dx = dx * mouse_speed / 2;
        dy = dy * mouse_speed / 2;

        mouse_x += dx;
        mouse_y += dy;

        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= max_x) mouse_x = max_x - 1;
        if (mouse_y >= max_y) mouse_y = max_y - 1;

        bool new_left  = (packet[0] & 0x01) != 0;
        bool new_right = (packet[0] & 0x02) != 0;
        bool new_mid   = (packet[0] & 0x04) != 0;

        struct gui_event evt;
        evt.mouse_x = mouse_x;
        evt.mouse_y = mouse_y;
        evt.key = 0;
        evt.scroll_delta = 0;

        if (new_left && !button_left) {
            evt.type = EVT_MOUSE_DOWN;
            evt.mouse_button = 0;
            event_push(&evt);
            click_left = true;
        }
        if (!new_left && button_left) {
            evt.type = EVT_MOUSE_UP;
            evt.mouse_button = 0;
            event_push(&evt);
        }
        if (new_right && !button_right) {
            evt.type = EVT_MOUSE_DOWN;
            evt.mouse_button = 1;
            event_push(&evt);
            click_right = true;
        }
        if (!new_right && button_right) {
            evt.type = EVT_MOUSE_UP;
            evt.mouse_button = 1;
            event_push(&evt);
        }

        button_left   = new_left;
        button_right  = new_right;
        button_middle = new_mid;

        if (scroll_wheel_enabled) {
            int z = (int8_t)packet[3];
            if (z > 7 || z < -7)
                z = (z > 0) ? 1 : -1;
            if (z > 3) z = 3;
            if (z < -3) z = -3;
            if (z != 0) {
                evt.type = EVT_MOUSE_SCROLL;
                evt.scroll_delta = (int8_t)z;
                evt.mouse_button = 0;
                event_push(&evt);
            }
        }

        if (dx != 0 || dy != 0) {
            evt.type = EVT_MOUSE_MOVE;
            evt.scroll_delta = 0;
            evt.mouse_button = (new_left ? 1 : 0) | (new_right ? 2 : 0);
            event_push(&evt);
        }
    }

eoi:
    /* EOI to slave and master PIC */
    outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

int  mouse_get_x(void)      { return (int)mouse_x; }
int  mouse_get_y(void)      { return (int)mouse_y; }
bool mouse_left_held(void)  { return button_left; }
bool mouse_right_held(void) { return button_right; }

bool mouse_left_click(void) {
    bool c = click_left;
    click_left = false;
    return c;
}

bool mouse_right_click(void) {
    bool c = click_right;
    click_right = false;
    return c;
}

void mouse_clear_clicks(void) {
    click_left = false;
    click_right = false;
}

void mouse_set_bounds(int mx, int my) {
    max_x = mx;
    max_y = my;
}

void mouse_set_speed(int speed) {
    if (speed < 1) speed = 1;
    if (speed > 3) speed = 3;
    mouse_speed = speed;
}

int mouse_get_speed(void) {
    return mouse_speed;
}

void mouse_clamp(void) {
    if (mouse_x >= max_x) mouse_x = max_x - 1;
    if (mouse_y >= max_y) mouse_y = max_y - 1;
}
