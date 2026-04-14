/* ===========================================================================
 * Snake Game
 * Classic snake game with VGA text-mode graphics and PC speaker sound
 * =========================================================================== */
#include "snake.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"
#include "speaker.h"

/* Game area (inside border) */
#define BOARD_X      1
#define BOARD_Y      2
#define BOARD_W      78
#define BOARD_H      21
#define MAX_SNAKE    400

/* Characters */
#define CH_SNAKE_HEAD 0x02  /* smiley face */
#define CH_SNAKE_BODY 0xFE  /* filled square */
#define CH_FOOD       0x04  /* diamond */
#define CH_BORDER_H   0xCD  /* double horizontal */
#define CH_BORDER_V   0xBA  /* double vertical */
#define CH_CORNER_TL  0xC9
#define CH_CORNER_TR  0xBB
#define CH_CORNER_BL  0xC8
#define CH_CORNER_BR  0xBC

/* Direction */
#define DIR_UP    0
#define DIR_DOWN  1
#define DIR_LEFT  2
#define DIR_RIGHT 3

static int snake_x[MAX_SNAKE];
static int snake_y[MAX_SNAKE];
static int snake_len;
static int direction;
static int food_x, food_y;
static int score;
static int high_score;
static bool game_over;
static bool game_quit;
static uint32_t rng_state;

static void put(int x, int y, char c, uint8_t color) {
    vga_putchar_at(x, y, c, color);
}

static uint32_t rng(void) {
    rng_state = rng_state * 1103515245 + 12345;
    return (rng_state >> 16) & 0x7FFF;
}

static void draw_border(void) {
    uint8_t bc = VGA_COLOR(VGA_LIGHT_CYAN, VGA_BLACK);

    /* Top and bottom walls */
    put(0, 1, CH_CORNER_TL, bc);
    put(79, 1, CH_CORNER_TR, bc);
    put(0, 23, CH_CORNER_BL, bc);
    put(79, 23, CH_CORNER_BR, bc);

    for (int x = 1; x < 79; x++) {
        put(x, 1, CH_BORDER_H, bc);
        put(x, 23, CH_BORDER_H, bc);
    }
    for (int y = 2; y < 23; y++) {
        put(0, y, CH_BORDER_V, bc);
        put(79, y, CH_BORDER_V, bc);
    }
}

static void draw_header(void) {
    uint8_t title_c = VGA_COLOR(VGA_LIGHT_GREEN, VGA_BLACK);
    uint8_t score_c = VGA_COLOR(VGA_YELLOW, VGA_BLACK);
    uint8_t hi_c = VGA_COLOR(VGA_LIGHT_CYAN, VGA_BLACK);

    /* Clear top row */
    for (int x = 0; x < 80; x++) put(x, 0, ' ', title_c);

    /* Title */
    const char *title = " SNAKE";
    for (int i = 0; title[i]; i++) put(i, 0, title[i], title_c);

    /* Score */
    const char *sc = "Score: ";
    int sx = 20;
    for (int i = 0; sc[i]; i++) put(sx + i, 0, sc[i], score_c);
    sx += 7;
    /* Print score number */
    char nbuf[8]; int ni = 0;
    int sv = score;
    if (sv == 0) nbuf[ni++] = '0';
    else { while (sv > 0) { nbuf[ni++] = '0' + (sv % 10); sv /= 10; } }
    for (int i = ni - 1; i >= 0; i--) put(sx++, 0, nbuf[i], score_c);

    /* High score */
    const char *hs = "Best: ";
    int hx = 40;
    for (int i = 0; hs[i]; i++) put(hx + i, 0, hs[i], hi_c);
    hx += 6;
    ni = 0; sv = high_score;
    if (sv == 0) nbuf[ni++] = '0';
    else { while (sv > 0) { nbuf[ni++] = '0' + (sv % 10); sv /= 10; } }
    for (int i = ni - 1; i >= 0; i--) put(hx++, 0, nbuf[i], hi_c);

    /* Controls hint */
    const char *hint = "Q=Quit";
    int cx = 73;
    for (int i = 0; hint[i]; i++) put(cx + i, 0, hint[i], VGA_COLOR(VGA_DARK_GREY, VGA_BLACK));
}

static void draw_bottom(void) {
    uint8_t c = VGA_COLOR(VGA_DARK_GREY, VGA_BLACK);
    for (int x = 0; x < 80; x++) put(x, 24, ' ', c);
    const char *msg = " Arrow keys to move | Eat food to grow | Don't hit walls or yourself!";
    for (int i = 0; msg[i] && i < 80; i++) put(i, 24, msg[i], c);
}

static void spawn_food(void) {
    bool valid;
    do {
        valid = true;
        food_x = BOARD_X + (rng() % BOARD_W);
        food_y = BOARD_Y + (rng() % BOARD_H);
        /* Don't spawn on snake */
        for (int i = 0; i < snake_len; i++) {
            if (snake_x[i] == food_x && snake_y[i] == food_y) {
                valid = false;
                break;
            }
        }
    } while (!valid);

    put(food_x, food_y, CH_FOOD, VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
}

static void sound_eat(void) {
    speaker_beep(880, 3);
    speaker_beep(1100, 3);
}

static void sound_die(void) {
    speaker_beep(400, 8);
    speaker_beep(300, 8);
    speaker_beep(200, 12);
}

static void sound_start(void) {
    speaker_beep(523, 5);
    speaker_beep(659, 5);
    speaker_beep(784, 8);
}

static void game_init(void) {
    /* Clear screen via VGA abstraction */
    vga_set_color(VGA_COLOR(VGA_LIGHT_GREY, VGA_BLACK));
    vga_clear();

    /* Hide cursor off-screen */
    vga_set_cursor_pos(80, 25);

    score = 0;
    game_over = false;
    game_quit = false;
    rng_state = timer_get_ticks();

    /* Initialize snake in the middle */
    snake_len = 4;
    direction = DIR_RIGHT;
    int start_x = BOARD_X + BOARD_W / 2;
    int start_y = BOARD_Y + BOARD_H / 2;
    for (int i = 0; i < snake_len; i++) {
        snake_x[i] = start_x - i;
        snake_y[i] = start_y;
    }

    draw_border();
    draw_header();
    draw_bottom();

    /* Draw initial snake */
    put(snake_x[0], snake_y[0], CH_SNAKE_HEAD, VGA_COLOR(VGA_LIGHT_GREEN, VGA_BLACK));
    for (int i = 1; i < snake_len; i++) {
        put(snake_x[i], snake_y[i], CH_SNAKE_BODY, VGA_COLOR(VGA_GREEN, VGA_BLACK));
    }

    spawn_food();
    sound_start();
}

static void game_update(void) {
    /* Calculate new head position */
    int new_x = snake_x[0];
    int new_y = snake_y[0];

    switch (direction) {
        case DIR_UP:    new_y--; break;
        case DIR_DOWN:  new_y++; break;
        case DIR_LEFT:  new_x--; break;
        case DIR_RIGHT: new_x++; break;
    }

    /* Check wall collision */
    if (new_x < BOARD_X || new_x >= BOARD_X + BOARD_W ||
        new_y < BOARD_Y || new_y >= BOARD_Y + BOARD_H) {
        game_over = true;
        return;
    }

    /* Check self collision */
    for (int i = 0; i < snake_len; i++) {
        if (snake_x[i] == new_x && snake_y[i] == new_y) {
            game_over = true;
            return;
        }
    }

    /* Check food */
    bool ate = (new_x == food_x && new_y == food_y);

    if (!ate) {
        /* Erase tail */
        put(snake_x[snake_len - 1], snake_y[snake_len - 1], ' ',
            VGA_COLOR(VGA_LIGHT_GREY, VGA_BLACK));
    }

    /* Move body: shift all segments back */
    if (ate && snake_len < MAX_SNAKE) {
        snake_len++;
    }
    for (int i = snake_len - 1; i > 0; i--) {
        snake_x[i] = snake_x[i - 1];
        snake_y[i] = snake_y[i - 1];
    }

    /* Place new head */
    snake_x[0] = new_x;
    snake_y[0] = new_y;

    /* Draw old head position as body */
    put(snake_x[1], snake_y[1], CH_SNAKE_BODY, VGA_COLOR(VGA_GREEN, VGA_BLACK));
    /* Draw new head */
    put(snake_x[0], snake_y[0], CH_SNAKE_HEAD, VGA_COLOR(VGA_LIGHT_GREEN, VGA_BLACK));

    if (ate) {
        score += 10;
        if (score > high_score) high_score = score;
        draw_header();
        sound_eat();
        spawn_food();
    }
}

static void show_game_over(void) {
    sound_die();

    uint8_t box_c = VGA_COLOR(VGA_WHITE, VGA_RED);
    uint8_t text_c = VGA_COLOR(VGA_YELLOW, VGA_RED);

    /* Draw game over box in center */
    int bx = 25, by = 9, bw = 30, bh = 7;
    for (int y = by; y < by + bh; y++) {
        for (int x = bx; x < bx + bw; x++) {
            put(x, y, ' ', box_c);
        }
    }

    const char *go = "G A M E   O V E R";
    int gx = bx + (bw - 18) / 2;
    for (int i = 0; go[i]; i++) put(gx + i, by + 1, go[i], box_c);

    /* Score line */
    const char *sc = "Score: ";
    int sx = bx + 4;
    for (int i = 0; sc[i]; i++) put(sx + i, by + 3, sc[i], text_c);
    sx += 7;
    char nbuf[8]; int ni = 0;
    int sv = score;
    if (sv == 0) nbuf[ni++] = '0';
    else { while (sv > 0) { nbuf[ni++] = '0' + (sv % 10); sv /= 10; } }
    for (int i = ni - 1; i >= 0; i--) put(sx++, by + 3, nbuf[i], text_c);

    const char *restart = "R=Restart  Q=Quit";
    int rx = bx + (bw - 17) / 2;
    for (int i = 0; restart[i]; i++) put(rx + i, by + 5, restart[i], box_c);
}

void snake_run(void) {
    high_score = 0;

restart:
    game_init();

    uint32_t last_tick = timer_get_ticks();

    while (!game_quit) {
        /* Process input (non-blocking) */
        while (keyboard_has_key()) {
            char c = keyboard_getchar();
            if (c == 'q' || c == 'Q') {
                game_quit = true;
                break;
            }
            if (game_over) {
                if (c == 'r' || c == 'R') goto restart;
                continue;
            }
            /* Arrow keys */
            uint8_t k = (uint8_t)c;
            if (k == KEY_UP    && direction != DIR_DOWN)  direction = DIR_UP;
            if (k == KEY_DOWN  && direction != DIR_UP)    direction = DIR_DOWN;
            if (k == KEY_LEFT  && direction != DIR_RIGHT) direction = DIR_LEFT;
            if (k == KEY_RIGHT && direction != DIR_LEFT)  direction = DIR_RIGHT;
        }

        if (game_quit) break;

        if (!game_over) {
            /* Speed: faster as score increases */
            uint32_t speed = 15;  /* base: 150ms */
            if (score >= 50)  speed = 12;
            if (score >= 100) speed = 10;
            if (score >= 200) speed = 8;
            if (score >= 300) speed = 6;
            if (score >= 500) speed = 4;

            uint32_t now = timer_get_ticks();
            if (now - last_tick >= speed) {
                last_tick = now;
                game_update();
                if (game_over) {
                    show_game_over();
                }
            }
        }

        __asm__ __volatile__("hlt");
    }

    speaker_off();
    vga_clear();
}
