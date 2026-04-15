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
static volatile uint8_t packet[3];
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

    packet_byte = 0;
}

void mouse_handler(void) {
    uint8_t data = inb(0x60);

    switch (packet_byte) {
        case 0:
            /* Byte 0: flags. Bit 3 must be set (sync) */
            if (!(data & 0x08)) {
                /* Out of sync, discard */
                break;
            }
            packet[0] = data;
            packet_byte = 1;
            break;
        case 1:
            packet[1] = data;
            packet_byte = 2;
            break;
        case 2:
            packet[2] = data;
            packet_byte = 0;

            /* Decode packet */
            int32_t dx = (int32_t)packet[1];
            int32_t dy = (int32_t)packet[2];

            /* Sign extend */
            if (packet[0] & 0x10) dx |= 0xFFFFFF00;
            if (packet[0] & 0x20) dy |= 0xFFFFFF00;

            /* Discard if overflow */
            if (packet[0] & 0xC0) break;

            /* PS/2 Y-axis is inverted */
            dy = -dy;

            /* Apply mouse speed scaling (1=slow, 2=normal, 3=fast) */
            dx = dx * mouse_speed / 2;
            dy = dy * mouse_speed / 2;

            /* Update absolute position */
            mouse_x += dx;
            mouse_y += dy;

            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= max_x) mouse_x = max_x - 1;
            if (mouse_y >= max_y) mouse_y = max_y - 1;

            /* Track button state edges */
            bool new_left  = (packet[0] & 0x01) != 0;
            bool new_right = (packet[0] & 0x02) != 0;
            bool new_mid   = (packet[0] & 0x04) != 0;

            /* Push events */
            struct gui_event evt;
            evt.mouse_x = mouse_x;
            evt.mouse_y = mouse_y;
            evt.key = 0;

            /* Button press events */
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

            button_left  = new_left;
            button_right = new_right;
            button_middle = new_mid;

            /* Move event */
            evt.type = EVT_MOUSE_MOVE;
            evt.mouse_button = (new_left ? 1 : 0) | (new_right ? 2 : 0);
            event_push(&evt);
            break;
    }

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
