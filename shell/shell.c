/* ===========================================================================
 * AI_OS Shell
 * Interactive command-line interface with filesystem, process, and editor support
 * =========================================================================== */
#include "shell.h"
#include "vga.h"
#include "keyboard.h"
#include "memory.h"
#include "string.h"
#include "timer.h"
#include "io.h"
#include "fs.h"
#include "process.h"
#include "editor.h"
#include "speaker.h"
#include "snake.h"

#define CMD_BUFFER_SIZE 256
#define MAX_ARGS 16

static char cmd_buffer[CMD_BUFFER_SIZE];
static int  cmd_pos = 0;

/* Command history */
#define HISTORY_SIZE 16
static char history[HISTORY_SIZE][CMD_BUFFER_SIZE];
static int  history_count = 0;
static int  history_pos = -1;

/* Prompt with CWD */
static void print_prompt(void) {
    char path[128];
    fs_get_path(fs_get_cwd(), path, sizeof(path));
    vga_print_colored("AI_OS", VGA_COLOR(VGA_LIGHT_CYAN, VGA_BLACK));
    vga_print_colored(":", VGA_COLOR(VGA_LIGHT_GREY, VGA_BLACK));
    vga_print_colored(path, VGA_COLOR(VGA_LIGHT_BLUE, VGA_BLACK));
    vga_print_colored("> ", VGA_COLOR(VGA_LIGHT_GREEN, VGA_BLACK));
}

static void clear_input_line(void) {
    /* Erase current input from screen */
    for (int i = 0; i < cmd_pos; i++) {
        vga_putchar('\b');
    }
}

/* ---- String utilities ---- */

static int parse_args(char *input, char *argv[]) {
    int argc = 0;
    while (*input && argc < MAX_ARGS) {
        while (*input == ' ') input++;
        if (!*input) break;
        argv[argc++] = input;
        while (*input && *input != ' ') input++;
        if (*input) *input++ = '\0';
    }
    return argc;
}

/* ---- Built-in commands ---- */

static void help_row(const char *left, const char *right) {
    vga_print(left);
    /* Pad to column 40 */
    int len = str_len(left);
    for (int i = len; i < 40; i++) vga_putchar(' ');
    if (right) vga_print(right);
    vga_putchar('\n');
}

static void cmd_help(void) {
    vga_print_colored(" === AI_OS Commands ===", VGA_COLOR(VGA_YELLOW, VGA_BLACK));
    vga_print_colored("                    ", VGA_COLOR(VGA_DARK_GREY, VGA_BLACK));
    vga_print_colored("Type 'help <cmd>' for details\n", VGA_COLOR(VGA_DARK_GREY, VGA_BLACK));
    vga_print_colored(" General:                               ", VGA_COLOR(VGA_LIGHT_CYAN, VGA_BLACK));
    vga_print_colored("Filesystem:\n", VGA_COLOR(VGA_LIGHT_CYAN, VGA_BLACK));
    help_row("  help        Show this help",            "  ls          List files");
    help_row("  clear       Clear screen",              "  cd <dir>    Change directory");
    help_row("  echo <msg>  Print message",             "  pwd         Working directory");
    help_row("  info        System information",        "  mkdir <n>   Create directory");
    help_row("  mem         Memory usage",              "  touch <f>   Create empty file");
    help_row("  uptime      Show uptime",               "  cat <f>     Print file contents");
    help_row("  color <0-F> Change text color",         "  write <f> t Write text to file");
    help_row("  history     Command history",           "  rm <name>   Delete file/dir");
    help_row("  calc <expr> Calculator (+ - * / %)",    "  edit <f>    Text editor");
    help_row("  matrix      Matrix rain effect",        "  beep <hz>   Play a tone");
    help_row("  sleep <sec> Sleep N seconds",           "  play <notes> Play notes");
    help_row("  logo        Show AI_OS logo",           "  song        Play a melody");
    vga_print_colored(" Processes:                             ", VGA_COLOR(VGA_LIGHT_CYAN, VGA_BLACK));
    vga_print_colored("System:\n", VGA_COLOR(VGA_LIGHT_CYAN, VGA_BLACK));
    help_row("  ps          List processes",            "  panic <t>   Test crash handler");
    help_row("  kill <pid>  Kill process",              "  reboot      Reboot system");
    help_row("  run <prog>  Background program",        "  halt        Shutdown");
    help_row("  snake       Play Snake!",               NULL);
    vga_print_colored("              ", VGA_COLOR(VGA_DARK_GREY, VGA_BLACK));
    vga_print_colored("(clock, counter, fib)\n", VGA_COLOR(VGA_DARK_GREY, VGA_BLACK));
}

static void cmd_info(void) {
    vga_print_colored("\n=== System Information ===\n", VGA_COLOR(VGA_YELLOW, VGA_BLACK));
    vga_print("  OS:         AI_OS v0.2\n");
    vga_print("  Builder:    Claude (Anthropic)\n");
    vga_print("  Arch:       x86 (i386) Protected Mode\n");
    vga_print("  Display:    VGA Text Mode 80x25\n");
    vga_print("  Input:      PS/2 Keyboard\n");
    vga_print("  Scheduler:  Preemptive Round-Robin\n");
    vga_print("  Filesystem: RAM Filesystem (ramfs)\n");
    vga_print("  Memory:     ");
    vga_print_dec(memory_get_free() / 1024);
    vga_print(" KB free / ");
    vga_print_dec(memory_get_total() / 1024);
    vga_print(" KB total\n");
    vga_print("  Processes:  ");
    vga_print_dec(process_count());
    vga_print(" active\n\n");
}

static void cmd_mem(void) {
    vga_print_colored("\n=== Memory Map ===\n", VGA_COLOR(VGA_YELLOW, VGA_BLACK));
    vga_print("  Heap start:  ");
    vga_print_hex(memory_get_heap_start());
    vga_print("\n  Heap end:    ");
    vga_print_hex(memory_get_heap_end());
    vga_print("\n  Used:        ");
    vga_print_dec((memory_get_total() - memory_get_free()) / 1024);
    vga_print(" KB\n  Free:        ");
    vga_print_dec(memory_get_free() / 1024);
    vga_print(" KB\n  Total:       ");
    vga_print_dec(memory_get_total() / 1024);
    vga_print(" KB\n\n");
}

static void cmd_echo(int argc, char *argv[]) {
    vga_putchar('\n');
    for (int i = 1; i < argc; i++) {
        if (i > 1) vga_putchar(' ');
        vga_print(argv[i]);
    }
    vga_putchar('\n');
}

static void cmd_uptime(void) {
    uint32_t ticks = timer_get_ticks();
    uint32_t seconds = ticks / 100;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;
    seconds %= 60;
    minutes %= 60;
    vga_print("\n  Uptime: ");
    if (hours > 0) { vga_print_dec(hours); vga_print("h "); }
    vga_print_dec(minutes);
    vga_print("m ");
    vga_print_dec(seconds);
    vga_print("s (");
    vga_print_dec(ticks);
    vga_print(" ticks)\n\n");
}

static void cmd_color(int argc, char *argv[]) {
    if (argc < 2) {
        vga_print("\n  Usage: color <0-F>\n\n");
        return;
    }
    char c = argv[1][0];
    uint8_t color;
    if (c >= '0' && c <= '9') color = c - '0';
    else if (c >= 'A' && c <= 'F') color = c - 'A' + 10;
    else if (c >= 'a' && c <= 'f') color = c - 'a' + 10;
    else { vga_print("\n  Invalid color.\n"); return; }
    vga_set_color(VGA_COLOR(color, VGA_BLACK));
    vga_print("\n  Color changed!\n\n");
}

static void cmd_reboot(void) {
    vga_print_colored("\n  Rebooting...\n", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
    uint8_t good = 0x02;
    while (good & 0x02)
        good = inb(0x64);
    outb(0x64, 0xFE);
    __asm__ __volatile__("cli; hlt");
}

static void cmd_halt(void) {
    vga_print_colored("\n  System halted. Goodbye!\n", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
    __asm__ __volatile__("cli; hlt");
}

static void cmd_logo(void) {
    uint8_t c1 = VGA_COLOR(VGA_LIGHT_CYAN, VGA_BLACK);
    uint8_t c2 = VGA_COLOR(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_putchar('\n');
    vga_print_colored("     ___    ____  ____  _____\n", c1);
    vga_print_colored("    /   |  /  _/ / __ \\/ ___/\n", c1);
    vga_print_colored("   / /| |  / /  / / / /\\__ \\ \n", c1);
    vga_print_colored("  / ___ |_/ /  / /_/ /___/ / \n", c1);
    vga_print_colored(" /_/  |_/___/  \\____//____/  \n", c1);
    vga_print_colored("                              \n", c2);
    vga_print_colored("  Built by Claude :: v0.2     \n\n", c2);
}

static void cmd_matrix(void) {
    vga_print_colored("\n  Matrix rain for 3 seconds...\n", VGA_COLOR(VGA_GREEN, VGA_BLACK));
    vga_clear();
    uint32_t seed = timer_get_ticks();
    uint32_t start = timer_get_ticks();
    while (timer_get_ticks() - start < 300) {
        seed = seed * 1103515245 + 12345;
        int x = (seed >> 16) % 78;
        int y = (seed >> 8) % 24;
        char c = '!' + (seed % 94);
        vga_putchar_at(x, y, c, VGA_COLOR(VGA_GREEN, VGA_BLACK));
        for (volatile int d = 0; d < 50000; d++);
    }
    vga_clear();
    vga_set_color(VGA_COLOR(VGA_LIGHT_GREY, VGA_BLACK));
    vga_print("  Matrix effect complete.\n\n");
}

static void cmd_calc(int argc, char *argv[]) {
    if (argc < 4) {
        vga_print("\n  Usage: calc <num> <op> <num>\n\n");
        return;
    }
    int a = str_to_int(argv[1]);
    int b = str_to_int(argv[3]);
    char op = argv[2][0];
    int result = 0;
    bool valid = true;
    switch (op) {
        case '+': result = a + b; break;
        case '-': result = a - b; break;
        case '*': result = a * b; break;
        case '/':
            if (b == 0) { vga_print_colored("\n  Error: Division by zero!\n\n", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK)); return; }
            result = a / b; break;
        case '%':
            if (b == 0) { vga_print_colored("\n  Error: Division by zero!\n\n", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK)); return; }
            result = a % b; break;
        default: valid = false;
    }
    if (!valid) { vga_print("\n  Unknown operator.\n\n"); return; }
    vga_print("\n  = ");
    if (result < 0) { vga_putchar('-'); result = -result; }
    vga_print_dec((uint32_t)result);
    vga_print("\n\n");
}

static void cmd_sleep_cmd(int argc, char *argv[]) {
    if (argc < 2) { vga_print("\n  Usage: sleep <seconds>\n\n"); return; }
    int secs = str_to_int(argv[1]);
    if (secs <= 0 || secs > 60) { vga_print("\n  1-60 seconds.\n\n"); return; }
    vga_print("\n  Sleeping for ");
    vga_print_dec(secs);
    vga_print(" seconds...\n");
    timer_wait(secs * 100);
    vga_print("  Done.\n\n");
}

static void cmd_history_show(void) {
    vga_print_colored("\n=== Command History ===\n", VGA_COLOR(VGA_YELLOW, VGA_BLACK));
    for (int i = 0; i < history_count; i++) {
        vga_print("  ");
        vga_print_dec(i + 1);
        vga_print("  ");
        vga_print(history[i]);
        vga_putchar('\n');
    }
    vga_putchar('\n');
}

/* ---- Filesystem commands ---- */

static void cmd_ls(void) {
    struct fs_node *dir = fs_get_node(fs_get_cwd());
    if (!dir) return;
    vga_putchar('\n');
    if (dir->child_count == 0) {
        vga_print("  (empty)\n");
    }
    for (int i = 0; i < dir->child_count; i++) {
        struct fs_node *child = fs_get_node(dir->children[i]);
        if (!child) continue;
        vga_print("  ");
        if (child->type == FS_DIR) {
            vga_print_colored(child->name, VGA_COLOR(VGA_LIGHT_BLUE, VGA_BLACK));
            vga_print_colored("/", VGA_COLOR(VGA_LIGHT_BLUE, VGA_BLACK));
        } else {
            vga_print(child->name);
            vga_print("  ");
            vga_print_dec(child->size);
            vga_print("B");
        }
        vga_putchar('\n');
    }
    vga_putchar('\n');
}

static void cmd_cd(int argc, char *argv[]) {
    if (argc < 2) { vga_print("\n  Usage: cd <dir>\n\n"); return; }
    int r = fs_change_dir(argv[1]);
    if (r == -1) vga_print_colored("\n  Directory not found.\n\n", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
    else if (r == -2) vga_print_colored("\n  Not a directory.\n\n", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
    else vga_putchar('\n');
}

static void cmd_pwd(void) {
    char path[128];
    fs_get_path(fs_get_cwd(), path, sizeof(path));
    vga_print("\n  ");
    vga_print(path);
    vga_print("\n\n");
}

static void cmd_mkdir(int argc, char *argv[]) {
    if (argc < 2) { vga_print("\n  Usage: mkdir <name>\n\n"); return; }
    if (fs_create(argv[1], FS_DIR) < 0) {
        vga_print_colored("\n  Failed to create directory.\n\n", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
    } else {
        vga_putchar('\n');
    }
}

static void cmd_touch(int argc, char *argv[]) {
    if (argc < 2) { vga_print("\n  Usage: touch <file>\n\n"); return; }
    if (fs_find(argv[1]) >= 0) { vga_putchar('\n'); return; } /* Already exists */
    if (fs_create(argv[1], FS_FILE) < 0) {
        vga_print_colored("\n  Failed to create file.\n\n", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
    } else {
        vga_putchar('\n');
    }
}

static void cmd_cat(int argc, char *argv[]) {
    if (argc < 2) { vga_print("\n  Usage: cat <file>\n\n"); return; }
    int idx = fs_find(argv[1]);
    if (idx < 0) {
        vga_print_colored("\n  File not found.\n\n", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
        return;
    }
    struct fs_node *node = fs_get_node(idx);
    if (!node || node->type != FS_FILE) {
        vga_print_colored("\n  Not a file.\n\n", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
        return;
    }
    vga_putchar('\n');
    char buf[MAX_FILE_DATA];
    int len = fs_read_file(idx, buf, sizeof(buf));
    if (len > 0) {
        vga_print(buf);
        if (buf[len - 1] != '\n') vga_putchar('\n');
    }
    vga_putchar('\n');
}

static void cmd_write(int argc, char *argv[]) {
    if (argc < 3) { vga_print("\n  Usage: write <file> <text...>\n\n"); return; }

    int idx = fs_find(argv[1]);
    if (idx < 0) {
        idx = fs_create(argv[1], FS_FILE);
        if (idx < 0) {
            vga_print_colored("\n  Failed to create file.\n\n", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
            return;
        }
    }

    /* Concatenate remaining args with spaces */
    char content[MAX_FILE_DATA];
    int pos = 0;
    for (int i = 2; i < argc && pos < MAX_FILE_DATA - 2; i++) {
        if (i > 2 && pos < MAX_FILE_DATA - 2) content[pos++] = ' ';
        for (int j = 0; argv[i][j] && pos < MAX_FILE_DATA - 2; j++) {
            content[pos++] = argv[i][j];
        }
    }
    content[pos++] = '\n';
    content[pos] = '\0';

    fs_write_file(idx, content, pos);
    vga_print("\n  Written ");
    vga_print_dec(pos);
    vga_print(" bytes.\n\n");
}

static void cmd_rm(int argc, char *argv[]) {
    if (argc < 2) { vga_print("\n  Usage: rm <name>\n\n"); return; }
    int r = fs_delete(argv[1]);
    if (r == -1) vga_print_colored("\n  Not found.\n\n", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
    else if (r == -2) vga_print_colored("\n  Directory not empty.\n\n", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
    else vga_putchar('\n');
}

static void cmd_edit(int argc, char *argv[]) {
    if (argc < 2) { vga_print("\n  Usage: edit <file>\n\n"); return; }
    editor_open(argv[1]);
    print_prompt();
}

/* ---- Process commands ---- */

static void cmd_ps(void) {
    struct process *procs = process_get_table();
    vga_print_colored("\n=== Processes ===\n", VGA_COLOR(VGA_YELLOW, VGA_BLACK));
    vga_print("  PID  STATE     NAME\n");
    const char *state_names[] = { "unused", "RUNNING", "READY", "SLEEP", "DEAD" };
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (procs[i].state == PROC_UNUSED) continue;
        vga_print("  ");
        if (i < 10) vga_putchar(' ');
        vga_print_dec(procs[i].pid);
        vga_print("   ");
        uint8_t sc = VGA_COLOR(VGA_LIGHT_GREEN, VGA_BLACK);
        if (procs[i].state == PROC_SLEEPING) sc = VGA_COLOR(VGA_YELLOW, VGA_BLACK);
        if (procs[i].state == PROC_DEAD) sc = VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK);
        vga_print_colored(state_names[procs[i].state], sc);
        /* Pad to align names */
        int slen = str_len(state_names[procs[i].state]);
        for (int j = slen; j < 10; j++) vga_putchar(' ');
        vga_print(procs[i].name);
        if (i == process_get_current()) vga_print(" *");
        vga_putchar('\n');
    }
    vga_putchar('\n');
}

static void cmd_kill(int argc, char *argv[]) {
    if (argc < 2) { vga_print("\n  Usage: kill <pid>\n\n"); return; }
    int pid = str_to_int(argv[1]);
    if (pid <= 0) {
        vga_print_colored("\n  Cannot kill PID 0 (shell).\n\n", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
        return;
    }
    process_kill(pid);
    vga_print("\n  Process ");
    vga_print_dec(pid);
    vga_print(" killed.\n\n");
}

/* ---- Background programs ---- */

static void prog_clock(void) {
    while (1) {
        uint32_t ticks = timer_get_ticks();
        uint32_t secs = ticks / 100;
        uint32_t mins = (secs / 60) % 60;
        uint32_t hrs = (secs / 3600) % 24;
        secs %= 60;

        /* Draw clock in top-right corner */
        uint8_t color = VGA_COLOR(VGA_BLACK, VGA_LIGHT_GREEN);
        char time_str[9];
        time_str[0] = '0' + (hrs / 10);
        time_str[1] = '0' + (hrs % 10);
        time_str[2] = ':';
        time_str[3] = '0' + (mins / 10);
        time_str[4] = '0' + (mins % 10);
        time_str[5] = ':';
        time_str[6] = '0' + (secs / 10);
        time_str[7] = '0' + (secs % 10);
        time_str[8] = '\0';

        for (int i = 0; i < 8; i++) {
            vga_putchar_at(72 + i, 0, time_str[i], color);
        }

        timer_wait(100); /* Update every second */
    }
}

static void prog_counter(void) {
    uint32_t count = 0;
    while (1) {
        count++;
        /* Draw counter in top-right area */
        uint8_t color = VGA_COLOR(VGA_BLACK, VGA_YELLOW);
        char buf[12];
        int val = count;
        int len = 0;
        char tmp[12];
        if (val == 0) { tmp[len++] = '0'; }
        while (val > 0) { tmp[len++] = '0' + (val % 10); val /= 10; }
        for (int i = 0; i < len; i++) buf[i] = tmp[len - 1 - i];
        buf[len] = '\0';

        int startx = 72;
        for (int i = 0; i < 8; i++) {
            char c = (i < len) ? buf[i] : ' ';
            vga_putchar_at(startx + i, 0, c, color);
        }

        timer_wait(50); /* Update every 500ms */
    }
}

static uint32_t fib(uint32_t n) {
    if (n <= 1) return n;
    uint32_t a = 0, b = 1;
    for (uint32_t i = 2; i <= n; i++) {
        uint32_t t = a + b;
        a = b;
        b = t;
    }
    return b;
}

static void prog_fib(void) {
    for (uint32_t n = 1; n <= 46; n++) {
        uint32_t f = fib(n);
        /* Display in top-right */
        uint8_t color = VGA_COLOR(VGA_BLACK, VGA_LIGHT_MAGENTA);
        /* Show "F(n)=result" */
        char buf[20];
        int pos = 0;
        buf[pos++] = 'F';
        buf[pos++] = '(';
        /* Write n */
        char ntmp[4]; int ni = 0;
        uint32_t nv = n;
        if (nv == 0) ntmp[ni++] = '0';
        while (nv > 0) { ntmp[ni++] = '0' + (nv % 10); nv /= 10; }
        for (int i = ni - 1; i >= 0; i--) buf[pos++] = ntmp[i];
        buf[pos++] = ')';
        buf[pos++] = '=';
        /* Write result */
        char ftmp[12]; int fi = 0;
        uint32_t fv = f;
        if (fv == 0) ftmp[fi++] = '0';
        while (fv > 0) { ftmp[fi++] = '0' + (fv % 10); fv /= 10; }
        for (int i = fi - 1; i >= 0; i--) buf[pos++] = ftmp[i];

        /* Pad and display */
        while (pos < 20) buf[pos++] = ' ';

        int startx = 60;
        for (int i = 0; i < 20 && startx + i < 80; i++) {
            vga_putchar_at(startx + i, 0, buf[i], color);
        }

        timer_wait(100);
    }
    /* Done - process exits */
    process_exit();
}

static void cmd_run(int argc, char *argv[]) {
    if (argc < 2) {
        vga_print("\n  Usage: run <program>\n");
        vga_print("  Programs: clock, counter, fib\n\n");
        return;
    }

    void (*entry)(void) = NULL;
    if (str_eq(argv[1], "clock")) entry = prog_clock;
    else if (str_eq(argv[1], "counter")) entry = prog_counter;
    else if (str_eq(argv[1], "fib")) entry = prog_fib;
    else {
        vga_print_colored("\n  Unknown program.\n\n", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
        return;
    }

    int pid = process_create(argv[1], entry);
    if (pid < 0) {
        vga_print_colored("\n  Failed to create process.\n\n", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
    } else {
        vga_print("\n  Started '");
        vga_print(argv[1]);
        vga_print("' (PID ");
        vga_print_dec(pid);
        vga_print(")\n\n");
    }
}

/* ---- Test panic ---- */

static void cmd_panic(int argc, char *argv[]) {
    if (argc < 2) {
        vga_print("\n  Usage: panic <type>\n");
        vga_print("  Types: div0, opcode, gpf\n\n");
        return;
    }
    if (str_eq(argv[1], "div0")) {
        vga_print("\n  Triggering divide-by-zero...\n");
        volatile int a = 1;
        volatile int b = 0;
        volatile int c = a / b;
        (void)c;
    } else if (str_eq(argv[1], "opcode")) {
        vga_print("\n  Triggering invalid opcode...\n");
        __asm__ __volatile__("ud2");
    } else if (str_eq(argv[1], "gpf")) {
        vga_print("\n  Triggering general protection fault...\n");
        __asm__ __volatile__("int $13");
    } else {
        vga_print_colored("\n  Unknown panic type.\n\n", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
    }
}

/* ---- Sound commands ---- */

static void cmd_beep(int argc, char *argv[]) {
    if (argc < 2) {
        vga_print("\n  Usage: beep <freq> [duration_ms]\n");
        vga_print("  Example: beep 440 500\n\n");
        return;
    }
    int freq = str_to_int(argv[1]);
    int dur = (argc >= 3) ? str_to_int(argv[2]) : 200;
    if (freq < 20 || freq > 20000) { vga_print("\n  Freq: 20-20000 Hz\n\n"); return; }
    if (dur < 10) dur = 10;
    if (dur > 5000) dur = 5000;
    vga_print("\n  ");
    vga_print_dec(freq);
    vga_print(" Hz for ");
    vga_print_dec(dur);
    vga_print(" ms\n\n");
    speaker_beep(freq, dur / 10);
}

static void cmd_play(int argc, char *argv[]) {
    if (argc < 2) {
        vga_print("\n  Usage: play <notes>\n");
        vga_print("  Notes: C D E F G A B (lowercase=oct3, UPPER=oct5)\n");
        vga_print("  Example: play C D E F G A B\n\n");
        return;
    }
    vga_putchar('\n');
    for (int i = 1; i < argc; i++) {
        char n = argv[i][0];
        if (n == '-' || n == '.') {
            timer_wait(10); /* rest */
        } else {
            int oct = (n >= 'a' && n <= 'g') ? 3 : 5;
            speaker_play_note(n, oct, 12);
            timer_wait(2);
        }
    }
    vga_putchar('\n');
}

static void cmd_song(void) {
    vga_print_colored("\n  Playing melody...\n\n", VGA_COLOR(VGA_LIGHT_MAGENTA, VGA_BLACK));
    /* Ode to Joy - Beethoven */
    static const uint16_t notes[] = {
        NOTE_E4, NOTE_E4, NOTE_F4, NOTE_G4,
        NOTE_G4, NOTE_F4, NOTE_E4, NOTE_D4,
        NOTE_C4, NOTE_C4, NOTE_D4, NOTE_E4,
        NOTE_E4, NOTE_D4, NOTE_D4, 0,
        NOTE_E4, NOTE_E4, NOTE_F4, NOTE_G4,
        NOTE_G4, NOTE_F4, NOTE_E4, NOTE_D4,
        NOTE_C4, NOTE_C4, NOTE_D4, NOTE_E4,
        NOTE_D4, NOTE_C4, NOTE_C4, 0
    };
    static const uint8_t durations[] = {
        12, 12, 12, 12,
        12, 12, 12, 12,
        12, 12, 12, 12,
        16, 6, 20, 8,
        12, 12, 12, 12,
        12, 12, 12, 12,
        12, 12, 12, 12,
        16, 6, 20, 8
    };
    for (int i = 0; i < 32; i++) {
        if (notes[i] == 0) {
            timer_wait(durations[i]);
        } else {
            speaker_beep(notes[i], durations[i]);
        }
        timer_wait(2);
    }
}

static void cmd_snake(void) {
    snake_run();
}

/* ---- Command dispatcher ---- */

void shell_execute(char *input) {
    char *argv[MAX_ARGS];
    int argc = parse_args(input, argv);

    if (argc == 0) return;

    if (str_eq(argv[0], "help"))         cmd_help();
    else if (str_eq(argv[0], "clear"))   vga_clear();
    else if (str_eq(argv[0], "echo"))    cmd_echo(argc, argv);
    else if (str_eq(argv[0], "info"))    cmd_info();
    else if (str_eq(argv[0], "mem"))     cmd_mem();
    else if (str_eq(argv[0], "uptime"))  cmd_uptime();
    else if (str_eq(argv[0], "color"))   cmd_color(argc, argv);
    else if (str_eq(argv[0], "reboot"))  cmd_reboot();
    else if (str_eq(argv[0], "halt"))    cmd_halt();
    else if (str_eq(argv[0], "logo"))    cmd_logo();
    else if (str_eq(argv[0], "matrix"))  cmd_matrix();
    else if (str_eq(argv[0], "calc"))    cmd_calc(argc, argv);
    else if (str_eq(argv[0], "sleep"))   cmd_sleep_cmd(argc, argv);
    else if (str_eq(argv[0], "history")) cmd_history_show();
    /* Filesystem */
    else if (str_eq(argv[0], "ls"))      cmd_ls();
    else if (str_eq(argv[0], "cd"))      cmd_cd(argc, argv);
    else if (str_eq(argv[0], "pwd"))     cmd_pwd();
    else if (str_eq(argv[0], "mkdir"))   cmd_mkdir(argc, argv);
    else if (str_eq(argv[0], "touch"))   cmd_touch(argc, argv);
    else if (str_eq(argv[0], "cat"))     cmd_cat(argc, argv);
    else if (str_eq(argv[0], "write"))   cmd_write(argc, argv);
    else if (str_eq(argv[0], "rm"))      cmd_rm(argc, argv);
    else if (str_eq(argv[0], "edit"))    cmd_edit(argc, argv);
    /* Processes */
    else if (str_eq(argv[0], "ps"))      cmd_ps();
    else if (str_eq(argv[0], "kill"))    cmd_kill(argc, argv);
    else if (str_eq(argv[0], "run"))     cmd_run(argc, argv);
    /* Debug */
    else if (str_eq(argv[0], "panic"))   cmd_panic(argc, argv);
    /* Sound & Games */
    else if (str_eq(argv[0], "beep"))    cmd_beep(argc, argv);
    else if (str_eq(argv[0], "play"))    cmd_play(argc, argv);
    else if (str_eq(argv[0], "song"))    cmd_song();
    else if (str_eq(argv[0], "snake"))   cmd_snake();
    else {
        vga_print_colored("\n  Unknown command: '", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
        vga_print_colored(argv[0], VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
        vga_print_colored("'. Type 'help' for commands.\n\n", VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
    }
}

void shell_init(void) {
    cmd_pos = 0;
    cmd_buffer[0] = '\0';
}

void shell_run(void) {
    print_prompt();

    while (1) {
        char c = keyboard_getchar();

        if (c == '\n') {
            /* Enter: execute command */
            vga_putchar('\n');
            cmd_buffer[cmd_pos] = '\0';

            if (cmd_pos > 0) {
                /* Add to history */
                if (history_count < HISTORY_SIZE) {
                    str_copy(history[history_count], cmd_buffer);
                    history_count++;
                } else {
                    /* Shift history up */
                    for (int i = 0; i < HISTORY_SIZE - 1; i++) {
                        str_copy(history[i], history[i + 1]);
                    }
                    str_copy(history[HISTORY_SIZE - 1], cmd_buffer);
                }
                shell_execute(cmd_buffer);
            }

            cmd_pos = 0;
            cmd_buffer[0] = '\0';
            history_pos = -1;
            print_prompt();

        } else if (c == '\b') {
            if (cmd_pos > 0) {
                cmd_pos--;
                cmd_buffer[cmd_pos] = '\0';
                vga_putchar('\b');
            }
        } else if (c == 3) {
            /* Ctrl+C */
            vga_print("^C\n");
            cmd_pos = 0;
            cmd_buffer[0] = '\0';
            history_pos = -1;
            print_prompt();
        } else if (c == 12) {
            /* Ctrl+L */
            vga_clear();
            cmd_pos = 0;
            cmd_buffer[0] = '\0';
            history_pos = -1;
            print_prompt();
        } else if ((uint8_t)c == KEY_UP) {
            /* History: previous command */
            if (history_count > 0) {
                if (history_pos < 0) history_pos = history_count;
                if (history_pos > 0) {
                    history_pos--;
                    clear_input_line();
                    str_copy(cmd_buffer, history[history_pos]);
                    cmd_pos = str_len(cmd_buffer);
                    vga_print(cmd_buffer);
                }
            }
        } else if ((uint8_t)c == KEY_DOWN) {
            /* History: next command */
            if (history_pos >= 0) {
                clear_input_line();
                history_pos++;
                if (history_pos >= history_count) {
                    history_pos = -1;
                    cmd_pos = 0;
                    cmd_buffer[0] = '\0';
                } else {
                    str_copy(cmd_buffer, history[history_pos]);
                    cmd_pos = str_len(cmd_buffer);
                    vga_print(cmd_buffer);
                }
            }
        } else if (c >= 32 && c < 127) {
            if (cmd_pos < CMD_BUFFER_SIZE - 1) {
                cmd_buffer[cmd_pos++] = c;
                vga_putchar(c);
            }
        }
    }
}

void shell_entry(void) {
    shell_init();
    shell_run();
}
