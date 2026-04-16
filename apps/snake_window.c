/* ===========================================================================
 * Snake Window - Pixel-rendered snake game in its own GUI window
 * =========================================================================== */
#include "snake_window.h"
#include "window.h"
#include "framebuffer.h"
#include "font8x16.h"
#include "keyboard.h"
#include "timer.h"
#include "speaker.h"
#include "string.h"

#define GRID_W     20
#define GRID_H     18
#define CELL_SIZE  16
#define HEADER_H   20
#define CONTENT_W  (GRID_W * CELL_SIZE)   /* 320 */
#define CONTENT_H  (GRID_H * CELL_SIZE + HEADER_H) /* 308 */

#define MAX_SNAKE  (GRID_W * GRID_H)

#define DIR_UP    0
#define DIR_DOWN  1
#define DIR_LEFT  2
#define DIR_RIGHT 3

/* Colors */
#define C_BG       COLOR_RGB(20, 20, 30)
#define C_GRID     COLOR_RGB(30, 30, 45)
#define C_SNAKE    COLOR_RGB(0, 200, 0)
#define C_HEAD     COLOR_RGB(0, 255, 50)
#define C_FOOD     COLOR_RGB(220, 40, 40)
#define C_FOOD2    COLOR_RGB(255, 80, 80)
#define C_TEXT     COLOR_WHITE
#define C_SCORE    COLOR_YELLOW
#define C_GAMEOVER COLOR_RGB(200, 30, 30)

static int win_id = -1;

static int snake_x[MAX_SNAKE], snake_y[MAX_SNAKE];
static int snake_len;
static int direction, next_dir;
static int food_x, food_y;
static int score, high_score;
static bool game_over;
static uint32_t rng_state;
static uint32_t last_tick;

static uint32_t rng(void) {
    rng_state = rng_state * 1103515245 + 12345;
    return (rng_state >> 16) & 0x7FFF;
}

/* Draw a cell (inset by 1px) for prettier look */
static void fill_cell_inset(color_t *buf, int cw, int gx, int gy, color_t color) {
    int px = gx * CELL_SIZE + 1;
    int py = gy * CELL_SIZE + HEADER_H + 1;
    int sz = CELL_SIZE - 2;
    for (int y = 0; y < sz; y++)
        for (int x = 0; x < sz; x++)
            buf[(py + y) * cw + px + x] = color;
}

static void draw_text(color_t *buf, int cw, int ch, int px, int py,
                      const char *s, color_t fg, color_t bg) {
    while (*s) {
        const uint8_t *glyph = font8x16[(uint8_t)*s];
        for (int gy = 0; gy < 16 && py + gy < ch; gy++) {
            uint8_t bits = glyph[gy];
            for (int gx = 0; gx < 8 && px + gx < cw; gx++) {
                color_t c = (bits & (0x80 >> gx)) ? fg : bg;
                if (py + gy >= 0 && px + gx >= 0)
                    buf[(py + gy) * cw + px + gx] = c;
            }
        }
        px += 8;
        s++;
    }
}

static void num_to_str(int n, char *buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[12]; int i = 0;
    while (n > 0) { tmp[i++] = '0' + (n % 10); n /= 10; }
    int j = 0;
    while (--i >= 0) buf[j++] = tmp[i];
    buf[j] = '\0';
}

static void spawn_food(void) {
    bool ok;
    do {
        ok = true;
        food_x = rng() % GRID_W;
        food_y = rng() % GRID_H;
        for (int i = 0; i < snake_len; i++) {
            if (snake_x[i] == food_x && snake_y[i] == food_y) { ok = false; break; }
        }
    } while (!ok);
}

static void game_init(void) {
    score = 0;
    game_over = false;
    rng_state = timer_get_ticks();
    snake_len = 4;
    direction = DIR_RIGHT;
    next_dir = DIR_RIGHT;
    int sx = GRID_W / 2, sy = GRID_H / 2;
    for (int i = 0; i < snake_len; i++) {
        snake_x[i] = sx - i;
        snake_y[i] = sy;
    }
    spawn_food();
    last_tick = timer_get_ticks();
    speaker_beep(523, 4);
    speaker_beep(659, 4);
    speaker_beep(784, 6);
}

static void game_update(void) {
    direction = next_dir;
    int nx = snake_x[0], ny = snake_y[0];
    switch (direction) {
        case DIR_UP:    ny--; break;
        case DIR_DOWN:  ny++; break;
        case DIR_LEFT:  nx--; break;
        case DIR_RIGHT: nx++; break;
    }
    /* Wall collision */
    if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) {
        game_over = true;
        speaker_beep(300, 8); speaker_beep(200, 10);
        return;
    }
    /* Self collision */
    for (int i = 0; i < snake_len; i++) {
        if (snake_x[i] == nx && snake_y[i] == ny) {
            game_over = true;
            speaker_beep(300, 8); speaker_beep(200, 10);
            return;
        }
    }
    bool ate = (nx == food_x && ny == food_y);
    if (ate && snake_len < MAX_SNAKE) snake_len++;
    for (int i = snake_len - 1; i > 0; i--) {
        snake_x[i] = snake_x[i - 1];
        snake_y[i] = snake_y[i - 1];
    }
    snake_x[0] = nx;
    snake_y[0] = ny;
    if (ate) {
        score += 10;
        if (score > high_score) high_score = score;
        speaker_beep(880, 2); speaker_beep(1100, 2);
        spawn_food();
    }
}

static void snake_on_event(struct window *win, struct gui_event *evt) {
    (void)win;
    if (evt->type != EVT_KEY_PRESS) return;
    uint8_t k = evt->key;
    if (k == 'q' || k == 'Q') {
        wm_destroy_window(win_id);
        win_id = -1;
        speaker_off();
        return;
    }
    if (game_over) {
        if (k == 'r' || k == 'R') game_init();
        return;
    }
    if (k == KEY_UP    && direction != DIR_DOWN)  next_dir = DIR_UP;
    if (k == KEY_DOWN  && direction != DIR_UP)    next_dir = DIR_DOWN;
    if (k == KEY_LEFT  && direction != DIR_RIGHT) next_dir = DIR_LEFT;
    if (k == KEY_RIGHT && direction != DIR_LEFT)  next_dir = DIR_RIGHT;
}

void snake_window_create(void) {
    if (win_id >= 0) {
        struct window *w = wm_get_window(win_id);
        if (w && w->alive && w->on_event == snake_on_event) { wm_focus_window(win_id); return; }
    }
    win_id = wm_create_window("Snake", 150, 50, CONTENT_W, CONTENT_H,
                               snake_on_event, NULL);
    game_init();
}

bool snake_window_is_alive(void) {
    if (win_id < 0) return false;
    struct window *w = wm_get_window(win_id);
    return (w && w->alive);
}

void snake_window_render(void) {
    if (win_id < 0) return;
    struct window *win = wm_get_window(win_id);
    if (!win || !win->alive || !win->content || win->on_event != snake_on_event) { win_id = -1; return; }

    int cw = win->content_w;
    int ch = win->content_h;
    color_t *buf = win->content;

    /* Game tick */
    if (!game_over) {
        uint32_t speed = 12;
        if (score >= 50)  speed = 10;
        if (score >= 100) speed = 8;
        if (score >= 200) speed = 6;
        if (score >= 400) speed = 4;
        uint32_t now = timer_get_ticks();
        if (now - last_tick >= speed) {
            last_tick = now;
            game_update();
        }
    }

    /* Clear background */
    for (int i = 0; i < cw * ch; i++) buf[i] = C_BG;

    /* Header */
    char nbuf[12];
    draw_text(buf, cw, ch, 4, 2, "SNAKE", C_HEAD, C_BG);
    draw_text(buf, cw, ch, 80, 2, "Score:", C_TEXT, C_BG);
    num_to_str(score, nbuf);
    draw_text(buf, cw, ch, 132, 2, nbuf, C_SCORE, C_BG);
    draw_text(buf, cw, ch, 200, 2, "Best:", C_TEXT, C_BG);
    num_to_str(high_score, nbuf);
    draw_text(buf, cw, ch, 244, 2, nbuf, C_SCORE, C_BG);

    /* Grid lines */
    for (int gx = 0; gx <= GRID_W; gx++) {
        int px = gx * CELL_SIZE;
        for (int y = HEADER_H; y < ch; y++)
            if (px < cw) buf[y * cw + px] = C_GRID;
    }
    for (int gy = 0; gy <= GRID_H; gy++) {
        int py = gy * CELL_SIZE + HEADER_H;
        if (py < ch)
            for (int x = 0; x < cw; x++) buf[py * cw + x] = C_GRID;
    }

    /* Food (pulsating) */
    color_t fc = ((timer_get_ticks() / 20) & 1) ? C_FOOD : C_FOOD2;
    fill_cell_inset(buf, cw, food_x, food_y, fc);

    /* Snake body */
    for (int i = 1; i < snake_len; i++)
        fill_cell_inset(buf, cw, snake_x[i], snake_y[i], C_SNAKE);
    /* Snake head */
    fill_cell_inset(buf, cw, snake_x[0], snake_y[0], C_HEAD);

    /* Game over overlay */
    if (game_over) {
        /* Darken */
        for (int i = 0; i < cw * ch; i++) {
            uint32_t c = buf[i];
            buf[i] = ((c >> 1) & 0x7F7F7F);
        }
        draw_text(buf, cw, ch, 88, 120, "GAME OVER", C_GAMEOVER, COLOR_RGB(10, 10, 15));
        draw_text(buf, cw, ch, 72, 150, "R=Restart Q=Quit", C_TEXT, COLOR_RGB(10, 10, 15));
        num_to_str(score, nbuf);
        draw_text(buf, cw, ch, 112, 180, "Score:", C_SCORE, COLOR_RGB(10, 10, 15));
        draw_text(buf, cw, ch, 164, 180, nbuf, C_SCORE, COLOR_RGB(10, 10, 15));
    }
}
