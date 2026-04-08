/* ===========================================================================
 * Keyboard Driver (PS/2 - IRQ1)
 * Handles scan codes, arrow keys, builds a key buffer for the shell
 * =========================================================================== */
#include "keyboard.h"
#include "io.h"
#include "vga.h"
#include "shell.h"

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
static volatile bool extended_key = false;

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
            /* Extended key press */
            switch (scancode) {
                case 0x48: buffer_put(KEY_UP);    break;
                case 0x50: buffer_put(KEY_DOWN);  break;
                case 0x4B: buffer_put(KEY_LEFT);  break;
                case 0x4D: buffer_put(KEY_RIGHT); break;
                case 0x47: buffer_put(KEY_HOME);  break;
                case 0x4F: buffer_put(KEY_END);   break;
                case 0x53: buffer_put(KEY_DELETE); break;
                case 0x49: buffer_put(KEY_PGUP);  break;
                case 0x51: buffer_put(KEY_PGDN);  break;
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
    } else {
        /* Key press */
        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = true;
        } else if (scancode == 0x1D) {
            ctrl_pressed = true;
        } else if (scancode == 0x3A) {
            caps_lock = !caps_lock;
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
                else if (c == 'l' || c == 'L') c = 12;  /* FF - Ctrl+L */
                else if (c == 's' || c == 'S') c = 19;  /* Ctrl+S */
                else if (c == 'q' || c == 'Q') c = 17;  /* Ctrl+Q */
            }

            if (c != 0) {
                buffer_put((uint8_t)c);
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
