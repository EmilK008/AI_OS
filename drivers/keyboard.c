/* ===========================================================================
 * Keyboard Driver (PS/2 - IRQ1)
 * Handles scan codes, arrow keys, builds a key buffer for the shell
 * =========================================================================== */
#include "keyboard.h"
#include "io.h"
#include "vga.h"
#include "shell.h"
#include "event.h"

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64
#define KEY_BUFFER_SIZE 256

/* US QWERTY scan code -> ASCII (set 1, lowercase) */
static const char scancode_to_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,   'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,   '\\','z','x','c','v','b','n','m',',','.','/',0,
    '*', 0,  ' ', 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,  0,  0
};

/* Shifted characters */
static const char scancode_to_ascii_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,   'A','S','D','F','G','H','J','K','L',':','"','~',
    0,   '|','Z','X','C','V','B','N','M','<','>','?',0,
    '*', 0,  ' ', 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,   0,  0,  0,  0,  0,  0,  0
};

static volatile uint8_t key_buffer[KEY_BUFFER_SIZE];
static volatile int buf_head = 0;
static volatile int buf_tail = 0;
static volatile bool shift_pressed = false;
static volatile bool caps_lock = false;
static volatile bool ctrl_pressed = false;
static volatile bool alt_pressed = false;
static volatile bool extended_key = false;
static volatile bool gui_mode_active = false;

static void buffer_put(uint8_t c) {
    int next = (buf_head + 1) % KEY_BUFFER_SIZE;
    if (next != buf_tail) {
        key_buffer[buf_head] = c;
        buf_head = next;
    }
}

/* Called from assembly ISR stub */
void keyboard_handler(void) {
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    /* Handle 0xE0 prefix for extended keys (arrows, etc.) */
    if (scancode == 0xE0) {
        extended_key = true;
        outb(0x20, 0x20);
        return;
    }

    if (extended_key) {
        extended_key = false;
        if (!(scancode & 0x80)) {
            uint8_t key = 0;
            /* Extended key press */
            switch (scancode) {
                case 0x48: key = KEY_UP;    break;
                case 0x50: key = KEY_DOWN;  break;
                case 0x4B: key = KEY_LEFT;  break;
                case 0x4D: key = KEY_RIGHT; break;
                case 0x47: key = KEY_HOME;  break;
                case 0x4F: key = KEY_END;   break;
                case 0x53: key = KEY_DELETE; break;
                case 0x49: key = KEY_PGUP;  break;
                case 0x51: key = KEY_PGDN;  break;
            }
            if (key) {
                if (gui_mode_active) {
                    struct gui_event evt;
                    evt.type = EVT_KEY_PRESS;
                    evt.key = key;
                    evt.mouse_x = 0;
                    evt.mouse_y = 0;
                    evt.mouse_button = 0;
                    event_push(&evt);
                } else {
                    buffer_put(key);
                }
            }
        }
        outb(0x20, 0x20);
        return;
    }

    /* Key release (high bit set) */
    if (scancode & 0x80) {
        uint8_t released = scancode & 0x7F;
        if (released == 0x2A || released == 0x36) shift_pressed = false;
        if (released == 0x1D) ctrl_pressed = false;
        if (released == 0x38) alt_pressed = false;
    } else {
        /* Key press */
        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = true;
        } else if (scancode == 0x1D) {
            ctrl_pressed = true;
        } else if (scancode == 0x38) {
            alt_pressed = true;
        } else if (scancode == 0x3A) {
            caps_lock = !caps_lock;
        } else if (alt_pressed && scancode == 0x0F) {
            /* Alt+Tab: send special key event */
            if (gui_mode_active) {
                struct gui_event evt;
                evt.type = EVT_KEY_PRESS;
                evt.key = KEY_ALTTAB;
                evt.mouse_x = 0;
                evt.mouse_y = 0;
                evt.mouse_button = 0;
                event_push(&evt);
            }
        } else {
            char c;
            if (shift_pressed) {
                c = scancode_to_ascii_shift[scancode];
            } else {
                c = scancode_to_ascii[scancode];
            }

            /* Apply caps lock to letters */
            if (caps_lock && c >= 'a' && c <= 'z') c -= 32;
            else if (caps_lock && c >= 'A' && c <= 'Z') c += 32;

            /* Ctrl combinations */
            if (ctrl_pressed) {
                if (c == 'c' || c == 'C') c = 3;       /* ETX - Ctrl+C */
                else if (c == 'f' || c == 'F') c = 6;  /* ACK - Ctrl+F */
                else if (c == 'h' || c == 'H') c = 8;  /* BS  - Ctrl+H */
                else if (c == 'l' || c == 'L') c = 12;  /* FF - Ctrl+L */
                else if (c == 'n' || c == 'N') c = 14;  /* SO - Ctrl+N */
                else if (c == 's' || c == 'S') c = 19;  /* Ctrl+S */
                else if (c == 'q' || c == 'Q') c = 17;  /* Ctrl+Q */
                else if (c == 't' || c == 'T') c = 20;  /* DC4 - Ctrl+T */
                else if (c == 'w' || c == 'W') c = 23;  /* ETB - Ctrl+W */
                else if (c == 'z' || c == 'Z') c = 26;  /* SUB - Ctrl+Z */
                else if (c == 'y' || c == 'Y') c = 25;  /* EM  - Ctrl+Y */
                else if (c == 'r' || c == 'R') c = 18;  /* DC2 - Ctrl+R */
                else if (c == 'b' || c == 'B') c = 2;   /* STX - Ctrl+B */
                else if (c == 'u' || c == 'U') c = 21;  /* NAK - Ctrl+U */
            }

            if (c != 0) {
                if (gui_mode_active) {
                    struct gui_event evt;
                    evt.type = EVT_KEY_PRESS;
                    evt.key = (uint8_t)c;
                    evt.mouse_x = 0;
                    evt.mouse_y = 0;
                    evt.mouse_button = 0;
                    event_push(&evt);
                } else {
                    buffer_put((uint8_t)c);
                }
            }
        }
    }

    /* Send EOI to master PIC */
    outb(0x20, 0x20);
}

bool keyboard_has_key(void) {
    return buf_head != buf_tail;
}

char keyboard_getchar(void) {
    while (buf_head == buf_tail) {
        __asm__ __volatile__("hlt");  /* Wait for interrupt */
    }
    char c = (char)key_buffer[buf_tail];
    buf_tail = (buf_tail + 1) % KEY_BUFFER_SIZE;
    return c;
}

void keyboard_init(void) {
    /* Keyboard is enabled via PIC in idt_init */
}

void keyboard_set_gui_mode(bool enabled) {
    gui_mode_active = enabled;
}

void keyboard_inject_char(uint8_t c) {
    buffer_put(c);
}

bool keyboard_alt_held(void) {
    return alt_pressed;
}
