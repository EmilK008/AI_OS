/* ===========================================================================
 * RAM Filesystem with ATA Disk Persistence
 * In-memory filesystem with directories and files, serialized to disk
 * =========================================================================== */
#include "fs.h"
#include "string.h"
#include "memory.h"
#include "ata.h"

static struct fs_node nodes[MAX_FILES];
static int cwd_idx = 0;  /* current working directory index */
static bool disk_autosave = false;

/* Debug output via QEMU port 0xE9 */
static void fs_dbg(const char *s) {
    while (*s)
        __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)*s++), "Nd"((uint16_t)0xE9));
}

static void fs_dbg_hex(uint32_t v) {
    static const char hx[] = "0123456789ABCDEF";
    fs_dbg("0x");
    for (int i = 28; i >= 0; i -= 4)
        __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)hx[(v >> i) & 0xF]), "Nd"((uint16_t)0xE9));
}

static void auto_save(void) {
    if (disk_autosave && ata_available())
        fs_save_to_disk();
}

void fs_init(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        nodes[i].used = false;
        nodes[i].data = (char *)0;
    }

    /* Create root directory */
    nodes[0].used = true;
    str_copy(nodes[0].name, "/");
    nodes[0].type = FS_DIR;
    nodes[0].size = 0;
    nodes[0].parent_idx = 0;
    nodes[0].child_count = 0;
    cwd_idx = 0;
}

void fs_init_defaults(void) {
    /* Create default directories */
    fs_create("home", FS_DIR);
    fs_create("etc", FS_DIR);
    fs_create("tmp", FS_DIR);

    /* Create a welcome file */
    int idx = fs_create("readme.txt", FS_FILE);
    if (idx >= 0) {
        const char *msg = "Welcome to AI_OS!\n\nThis operating system was built entirely by Claude.\nType 'help' for available commands.\n\nFeatures:\n- VGA text mode display\n- PS/2 keyboard input\n- Preemptive multitasking\n- In-memory filesystem\n- Built-in text editor\n";
        fs_write_file(idx, msg, str_len(msg));
    }

    /* Create /etc/motd */
    int old_cwd = cwd_idx;
    int etc = fs_find("etc");
    if (etc >= 0) {
        cwd_idx = etc;
        int motd = fs_create("motd", FS_FILE);
        if (motd >= 0) {
            const char *m = "AI_OS v0.3 - Built by Claude\n";
            fs_write_file(motd, m, str_len(m));
        }
        cwd_idx = old_cwd;
    }

    /* Create default browser home page */
    int home = fs_create("home.htm", FS_FILE);
    if (home >= 0) {
        const char *htm =
            "<!-- CSS styles: tag selectors and .class selectors -->"
            "<style>"
            "h1 { color: red; text-align: center; }"
            "h2 { color: blue; }"
            ".big { color: white; background-color: red; }"
            ".green { color: lime; background-color: black; }"
            ".fancy { color: orange; text-decoration: underline; }"
            ".right { text-align: right; color: teal; }"
            ".center { text-align: center; color: purple; }"
            "</style>"
            "<!-- Heading: centered + red from h1 rule -->"
            "<h1>Welcome to AI_OS</h1>"
            "<p>This heading is <b>centered</b> and <b>red</b> via "
            "h1 { color: red; text-align: center; }</p>"
            "<!-- Subheading: blue from h2 rule -->"
            "<h2>CSS and Layout Demo</h2>"
            "<!-- Word wrapping: long text breaks at spaces -->"
            "<p>Long text wraps at word boundaries instead of breaking "
            "words apart. This paragraph demonstrates that long sentences "
            "will neatly wrap to the next line at spaces.</p>"
            "<hr>"
            "<!-- Text alignment via class selectors -->"
            "<p class=\"center\">Centered text via .center class</p>"
            "<p class=\"right\">Right-aligned text via .right class</p>"
            "<hr>"
            "<!-- Inline color classes on spans -->"
            "<p><span class=\"big\"> White on Red </span> "
            "<span class=\"green\"> Lime on Black </span> "
            "<span class=\"fancy\">Orange Underlined</span></p>"
            "<!-- Inline styles: color by name, hex, rgb -->"
            "<p>Inline: <span style=\"color: red\">RED</span> "
            "<span style=\"color: blue\">BLUE</span> "
            "<span style=\"color: #ff6600\">HEX</span> "
            "<span style=\"color: rgb(255,0,255)\">RGB</span></p>"
            "<hr>"
            "<!-- Links to other local files -->"
            "<p>Links: <a href=\"readme.txt\">readme.txt</a> | "
            "<a href=\"demo.htm\">demo page</a> | "
            "<a href=\"features.htm\">features demo</a> | "
            "<a href=\"jsdemo.htm\">JS demo</a> | "
            "<a href=\"tabledemo.htm\">tables demo</a></p>"
            "<!-- Navigation hints -->"
            "<p>Scroll: PgUp/PgDn | Back: Backspace</p>"
            "<p>Tabs: Ctrl+T new | Ctrl+W close</p>";
        fs_write_file(home, htm, str_len(htm));
    }

    /* Create CSS demo page */
    int demo = fs_create("demo.htm", FS_FILE);
    if (demo >= 0) {
        const char *htm =
            "<!-- Style block: tag and class selectors -->"
            "<style>"
            "h1 { color: crimson; text-align: center; }"
            "p { color: navy; }"
            ".box { background-color: gold; color: black; padding-left: 16; }"
            ".right { text-align: right; color: gray; }"
            "</style>"
            "<!-- Centered crimson heading -->"
            "<h1>CSS Demo Page</h1>"
            "<!-- All p tags are navy from: p { color: navy; } -->"
            "<p>All paragraphs are navy blue from: p { color: navy; }</p>"
            "<!-- Div with .box class: gold bg + padding -->"
            "<div class=\"box\">Gold background box with padding</div>"
            "<!-- Right-aligned paragraph -->"
            "<p class=\"right\">This text is right-aligned</p>"
            "<!-- Word wrapping demo -->"
            "<p>This is a longer paragraph to demonstrate word wrapping. "
            "Words should break at spaces, not in the middle of a word. "
            "The text flows naturally from line to line.</p>"
            "<!-- Navigation link back to home -->"
            "<p>Go <a href=\"home.htm\">back to home</a></p>";
        fs_write_file(demo, htm, str_len(htm));
    }

    /* Create browser features demo page */
    int features = fs_create("features.htm", FS_FILE);
    if (features >= 0) {
        const char *htm =
            "<style>"
            "h1 { color: steelblue; text-align: center; }"
            "h2 { color: crimson; border-bottom: 2px solid crimson; }"
            ".box { border: 2px solid navy; margin: 8; padding: 8; }"
            ".info { background: khaki; padding: 6; margin-bottom: 8; }"
            "pre { background: lightgray; padding: 8; white-space: pre; }"
            ".small { font-size: 10; color: gray; }"
            ".spaced { letter-spacing: 4; }"
            "ul.nodots { list-style: none; }"
            "</style>"
            "<h1>Browser Features Demo</h1>"
            /* HTML Entities */
            "<h2>HTML Entities</h2>"
            "<p>&lt;html&gt; &amp; &quot;quotes&quot; &apos;apos&apos;</p>"
            "<p>Numeric: &#65;&#66;&#67; (ABC) | Hex: &#x48;&#x49; (HI)</p>"
            "<p>Non-breaking&nbsp;&nbsp;&nbsp;spaces&nbsp;&nbsp;&nbsp;here</p>"
            /* CSS: border, margin, padding */
            "<h2>CSS: Borders &amp; Spacing</h2>"
            "<div class=\"box\">This div has a 2px navy border "
            "with margin and padding.</div>"
            "<div class=\"info\">Khaki background with padding</div>"
            /* CSS: white-space pre */
            "<h2>White-Space: Pre</h2>"
            "<pre>Line 1\n  indented\n    more\nback</pre>"
            /* CSS: letter-spacing */
            "<h2>Letter Spacing</h2>"
            "<p class=\"spaced\">W I D E text with letter-spacing</p>"
            /* CSS: list-style */
            "<h2>List Styles</h2>"
            "<p>No bullets:</p>"
            "<ul class=\"nodots\"><li>Clean item 1</li>"
            "<li>Clean item 2</li></ul>"
            /* Forms */
            "<h2>Form Elements</h2>"
            "<form>"
            "<p>Text: <input type=\"text\" placeholder=\"Type here\"></p>"
            "<p>Pass: <input type=\"password\" value=\"secret\"></p>"
            "<p>Check: <input type=\"checkbox\"> Enable option</p>"
            "<p>Select: <select>"
            "<option>Option A</option>"
            "<option>Option B</option>"
            "<option>Option C</option>"
            "</select></p>"
            "<p><button>Click Me</button></p>"
            "<p>Textarea:</p>"
            "<textarea>Edit this text...</textarea>"
            "</form>"
            /* Navigation */
            "<hr>"
            "<p><a href=\"home.htm\">Home</a> | "
            "<a href=\"demo.htm\">CSS Demo</a></p>"
            "<p class=\"small\">Use Tab to cycle form fields. "
            "Click to focus. Esc to unfocus.</p>";
        fs_write_file(features, htm, str_len(htm));
    }

    /* Create JavaScript demo page */
    int jsdemo = fs_create("jsdemo.htm", FS_FILE);
    if (jsdemo >= 0) {
        const char *htm =
            "<style>"
            "h1 { color: steelblue; text-align: center; }"
            "h2 { color: crimson; }"
            ".demo { border: 2px solid navy; padding: 8; margin: 8; }"
            ".info { background: lightyellow; padding: 6; }"
            "</style>"
            "<script>"
            "function greet() {"
            "  alert('Hello from JavaScript!');"
            "  document.title = 'Greeted!';"
            "}"
            "function goHome() {"
            "  navigate('home.htm');"
            "}"
            "function about() {"
            "  alert('AI_OS Browser v0.4 - Now with JS!');"
            "}"
            "</script>"
            "<h1>JavaScript Demo</h1>"
            "<div class=\"demo\">"
            "<h2>Alert</h2>"
            "<p>Click to show a message box:</p>"
            "<p><button onclick=\"alert('Hello World!')\">Say Hello</button> "
            "<button onclick=\"alert('This is an alert dialog!')\">Info</button></p>"
            "</div>"
            "<div class=\"demo\">"
            "<h2>Functions</h2>"
            "<p>Buttons calling script functions:</p>"
            "<p><button onclick=\"greet()\">Greet</button> "
            "<button onclick=\"about()\">About</button></p>"
            "</div>"
            "<div class=\"demo\">"
            "<h2>Navigation</h2>"
            "<p>Navigate via JavaScript:</p>"
            "<p><button onclick=\"navigate('home.htm')\">Go Home</button> "
            "<button onclick=\"navigate('features.htm')\">Features</button></p>"
            "</div>"
            "<div class=\"demo\">"
            "<h2>Change Title</h2>"
            "<p><button onclick=\"document.title = 'Custom Title!'\">Set Title</button> "
            "<button onclick=\"document.title = 'Browser'\">Reset Title</button></p>"
            "</div>"
            "<hr>"
            "<div class=\"info\">"
            "<p>Supported: alert(), navigate(), document.title, "
            "function calls, variables</p>"
            "</div>"
            "<p><a href=\"home.htm\">Home</a> | "
            "<a href=\"features.htm\">Features</a> | "
            "<a href=\"tabledemo.htm\">Tables</a></p>";
        fs_write_file(jsdemo, htm, str_len(htm));
    }

    /* Table demo page */
    int tabledemo = fs_create("tabledemo.htm", FS_FILE);
    if (tabledemo >= 0) {
        const char *htm =
            "<style>"
            "h1 { color: navy; text-align: center; }"
            "h2 { color: teal; }"
            "</style>"
            "<h1>Table Demo</h1>"
            "<h2>File System</h2>"
            "<table>"
            "<tr><th>Name</th><th>Type</th><th>Size</th></tr>"
            "<tr><td>home.htm</td><td>HTML</td><td>1.2K</td></tr>"
            "<tr><td>demo.htm</td><td>HTML</td><td>890B</td></tr>"
            "<tr><td>readme.txt</td><td>Text</td><td>256B</td></tr>"
            "<tr><td>bookmarks.dat</td><td>Data</td><td>64B</td></tr>"
            "</table>"
            "<h2>Colors</h2>"
            "<table>"
            "<tr><th>Color</th><th>Hex</th><th>Use</th></tr>"
            "<tr><td>Red</td><td>#FF0000</td><td>Errors</td></tr>"
            "<tr><td>Green</td><td>#00FF00</td><td>Success</td></tr>"
            "<tr><td>Blue</td><td>#0000FF</td><td>Links</td></tr>"
            "</table>"
            "<hr>"
            "<h2>Interactive JS Demo</h2>"
            "<script>"
            "function askName() {"
            "  var name = prompt('What is your name?');"
            "  if (name) { alert('Hello ' + name + '!'); }"
            "  else { alert('No name entered'); }"
            "}"
            "function counter() {"
            "  count = '1';"
            "  alert('Count: ' + count);"
            "}"
            "</script>"
            "<p><button onclick=\"askName()\">Ask Name</button> "
            "<button onclick=\"counter()\">Count</button></p>"
            "<p>Try: Ctrl+U = view source | Ctrl+B = bookmarks | * = star page</p>"
            "<hr>"
            "<p><a href=\"home.htm\">Home</a> | "
            "<a href=\"jsdemo.htm\">JS Demo</a> | "
            "<a href=\"features.htm\">Features</a></p>";
        fs_write_file(tabledemo, htm, str_len(htm));
    }
}

void fs_enable_autosave(void) {
    disk_autosave = true;
}

static int alloc_node(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!nodes[i].used) return i;
    }
    return -1;
}

int fs_create(const char *name, uint8_t type) {
    /* Check if name already exists in cwd */
    if (fs_find(name) >= 0) return -1;

    int idx = alloc_node();
    if (idx < 0) return -1;

    nodes[idx].used = true;
    str_copy(nodes[idx].name, name);
    nodes[idx].type = type;
    nodes[idx].size = 0;
    nodes[idx].data = (char *)0;
    nodes[idx].parent_idx = cwd_idx;
    nodes[idx].child_count = 0;

    /* Add to parent's children */
    struct fs_node *parent = &nodes[cwd_idx];
    if (parent->child_count < MAX_CHILDREN) {
        parent->children[parent->child_count++] = idx;
        auto_save();
        return idx;
    }

    nodes[idx].used = false;
    return -1;
}

int fs_find(const char *name) {
    struct fs_node *dir = &nodes[cwd_idx];
    for (int i = 0; i < dir->child_count; i++) {
        int child = dir->children[i];
        if (nodes[child].used && str_eq(nodes[child].name, name)) {
            return child;
        }
    }
    return -1;
}

int fs_write_file(int idx, const char *data, uint32_t size) {
    if (idx < 0 || idx >= MAX_FILES || !nodes[idx].used) return -1;
    if (nodes[idx].type != FS_FILE) return -1;
    if (size > MAX_FILE_DATA - 1) size = MAX_FILE_DATA - 1;

    /* Allocate data buffer on first write */
    if (!nodes[idx].data) {
        nodes[idx].data = (char *)kmalloc(MAX_FILE_DATA);
        if (!nodes[idx].data) return -1;
    }

    mem_copy(nodes[idx].data, data, size);
    nodes[idx].data[size] = '\0';
    nodes[idx].size = size;
    auto_save();
    return 0;
}

int fs_append_file(int idx, const char *data, uint32_t size) {
    if (idx < 0 || idx >= MAX_FILES || !nodes[idx].used) return -1;
    if (nodes[idx].type != FS_FILE) return -1;

    /* Allocate data buffer on first write */
    if (!nodes[idx].data) {
        nodes[idx].data = (char *)kmalloc(MAX_FILE_DATA);
        if (!nodes[idx].data) return -1;
        nodes[idx].size = 0;
    }

    if (nodes[idx].size + size > MAX_FILE_DATA - 1) {
        size = MAX_FILE_DATA - 1 - nodes[idx].size;
    }
    if (size == 0) return -1;

    mem_copy(nodes[idx].data + nodes[idx].size, data, size);
    nodes[idx].size += size;
    nodes[idx].data[nodes[idx].size] = '\0';
    auto_save();
    return 0;
}

int fs_read_file(int idx, char *buffer, uint32_t max_size) {
    if (idx < 0 || idx >= MAX_FILES || !nodes[idx].used) return -1;
    if (nodes[idx].type != FS_FILE) return -1;
    if (!nodes[idx].data) return 0;

    uint32_t to_read = nodes[idx].size;
    if (to_read > max_size - 1) to_read = max_size - 1;
    mem_copy(buffer, nodes[idx].data, to_read);
    buffer[to_read] = '\0';
    return (int)to_read;
}

int fs_delete(const char *name) {
    int idx = fs_find(name);
    if (idx < 0) return -1;

    /* Can't delete non-empty directories */
    if (nodes[idx].type == FS_DIR && nodes[idx].child_count > 0) return -2;

    /* Remove from parent's children list */
    struct fs_node *parent = &nodes[cwd_idx];
    for (int i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == idx) {
            /* Shift remaining children */
            for (int j = i; j < parent->child_count - 1; j++) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->child_count--;
            break;
        }
    }

    nodes[idx].used = false;
    if (nodes[idx].data) {
        kfree(nodes[idx].data);
        nodes[idx].data = (char *)0;
    }
    auto_save();
    return 0;
}

int fs_change_dir(const char *name) {
    if (str_eq(name, "/")) {
        cwd_idx = 0;
        return 0;
    }
    if (str_eq(name, "..")) {
        cwd_idx = nodes[cwd_idx].parent_idx;
        return 0;
    }
    if (str_eq(name, ".")) {
        return 0;
    }

    int idx = fs_find(name);
    if (idx < 0) return -1;
    if (nodes[idx].type != FS_DIR) return -2;
    cwd_idx = idx;
    return 0;
}

int fs_get_cwd(void) {
    return cwd_idx;
}

const char *fs_get_cwd_name(void) {
    return nodes[cwd_idx].name;
}

struct fs_node *fs_get_node(int idx) {
    if (idx < 0 || idx >= MAX_FILES || !nodes[idx].used) return NULL;
    return &nodes[idx];
}

void fs_get_path(int idx, char *buf, int buf_size) {
    if (idx == 0) {
        buf[0] = '/';
        buf[1] = '\0';
        return;
    }

    /* Build path by walking up to root */
    char parts[8][MAX_FILENAME];
    int depth = 0;
    int cur = idx;

    while (cur != 0 && depth < 8) {
        str_copy(parts[depth], nodes[cur].name);
        depth++;
        cur = nodes[cur].parent_idx;
    }

    buf[0] = '\0';
    int pos = 0;
    for (int i = depth - 1; i >= 0 && pos < buf_size - 2; i--) {
        buf[pos++] = '/';
        for (int j = 0; parts[i][j] && pos < buf_size - 1; j++) {
            buf[pos++] = parts[i][j];
        }
    }
    buf[pos] = '\0';
}

/* ===========================================================================
 * Disk Persistence — Serialize/Deserialize filesystem to ATA disk
 *
 * Disk layout:
 *   Sector 0:     Header (magic, node_count, data_start_sector)
 *   Sectors 1-16: Node table (64 nodes × 128 bytes = 8192 bytes)
 *   Sectors 17+:  File data (each file's data starts on a sector boundary)
 * =========================================================================== */

#define DISK_MAGIC "AIOSFS\x01"
#define DISK_MAGIC_LEN 7
#define DISK_NODE_SIZE 128
#define DISK_HEADER_SECTOR 0
#define DISK_NODE_SECTOR   1
#define DISK_NODE_SECTORS  16   /* 64 * 128 / 512 = 16 */
#define DISK_DATA_SECTOR   17

/* On-disk node: 128 bytes (packed manually into sector buffer) */
/* Layout:
 *   0-31:   name[32]
 *  32:      type
 *  33:      used
 *  34-35:   padding
 *  36-39:   size
 *  40-43:   parent_idx
 *  44-47:   child_count
 *  48-111:  children[16] (16 × 4 bytes)
 * 112-115:  data_sector (sector offset from DISK_DATA_SECTOR, 0xFFFFFFFF = none)
 * 116-119:  data_length
 * 120-127:  reserved
 */

static void write32(char *buf, int off, uint32_t val) {
    buf[off]   = (char)(val & 0xFF);
    buf[off+1] = (char)((val >> 8) & 0xFF);
    buf[off+2] = (char)((val >> 16) & 0xFF);
    buf[off+3] = (char)((val >> 24) & 0xFF);
}

static uint32_t read32(const char *buf, int off) {
    return (uint8_t)buf[off]
         | ((uint8_t)buf[off+1] << 8)
         | ((uint8_t)buf[off+2] << 16)
         | ((uint8_t)buf[off+3] << 24);
}

/* Round up to sector count */
static uint32_t sectors_for(uint32_t bytes) {
    return (bytes + 511) / 512;
}

int fs_save_to_disk(void) {
    if (!ata_available()) return -1;

    char sector[512];

    /* --- Pass 1: calculate data sector offsets --- */
    uint32_t data_sector_offsets[MAX_FILES];
    uint32_t cur_data_sector = 0;  /* relative to DISK_DATA_SECTOR */

    for (int i = 0; i < MAX_FILES; i++) {
        if (nodes[i].used && nodes[i].type == FS_FILE && nodes[i].data && nodes[i].size > 0) {
            data_sector_offsets[i] = cur_data_sector;
            cur_data_sector += sectors_for(nodes[i].size);
        } else {
            data_sector_offsets[i] = 0xFFFFFFFF;
        }
    }

    /* --- Write header (sector 0) --- */
    mem_set(sector, 0, 512);
    mem_copy(sector, DISK_MAGIC, DISK_MAGIC_LEN);
    write32(sector, 8, MAX_FILES);           /* node_count */
    write32(sector, 12, DISK_NODE_SIZE);     /* node_size */
    write32(sector, 16, DISK_DATA_SECTOR);   /* data_start_sector */
    write32(sector, 20, cur_data_sector);    /* total data sectors used */

    if (ata_write_sectors(DISK_HEADER_SECTOR, 1, sector) < 0) {
        fs_dbg("DISK: header write fail\n");
        return -1;
    }

    /* --- Write node table (sectors 1-16) --- */
    /* 4 nodes per sector (128 * 4 = 512) */
    for (int s = 0; s < DISK_NODE_SECTORS; s++) {
        mem_set(sector, 0, 512);
        for (int n = 0; n < 4; n++) {
            int idx = s * 4 + n;
            int off = n * DISK_NODE_SIZE;
            struct fs_node *node = &nodes[idx];

            mem_copy(sector + off, node->name, MAX_FILENAME);
            sector[off + 32] = (char)node->type;
            sector[off + 33] = node->used ? 1 : 0;
            /* 34-35: padding */
            write32(sector, off + 36, node->size);
            write32(sector, off + 40, (uint32_t)node->parent_idx);
            write32(sector, off + 44, (uint32_t)node->child_count);
            for (int c = 0; c < MAX_CHILDREN; c++) {
                write32(sector, off + 48 + c * 4, (uint32_t)node->children[c]);
            }
            write32(sector, off + 112, data_sector_offsets[idx]);
            write32(sector, off + 116, node->size);
        }
        if (ata_write_sectors(DISK_NODE_SECTOR + (uint32_t)s, 1, sector) < 0) {
            fs_dbg("DISK: node write fail\n");
            return -1;
        }
    }

    /* --- Write file data (sector 17+) --- */
    for (int i = 0; i < MAX_FILES; i++) {
        if (data_sector_offsets[i] == 0xFFFFFFFF) continue;

        uint32_t sz = nodes[i].size;
        uint32_t nsectors = sectors_for(sz);
        uint32_t written = 0;

        for (uint32_t s = 0; s < nsectors; s++) {
            mem_set(sector, 0, 512);
            uint32_t chunk = sz - written;
            if (chunk > 512) chunk = 512;
            mem_copy(sector, nodes[i].data + written, chunk);
            written += chunk;

            if (ata_write_sectors(DISK_DATA_SECTOR + data_sector_offsets[i] + s, 1, sector) < 0) {
                fs_dbg("DISK: data write fail\n");
                return -1;
            }
        }
    }

    fs_dbg("DISK: saved ");
    fs_dbg_hex(cur_data_sector);
    fs_dbg(" data sectors\n");
    return 0;
}

int fs_load_from_disk(void) {
    if (!ata_available()) return -1;

    char sector[512];

    /* --- Read and validate header --- */
    if (ata_read_sectors(DISK_HEADER_SECTOR, 1, sector) < 0) {
        fs_dbg("DISK: header read fail\n");
        return -1;
    }

    /* Check magic */
    for (int i = 0; i < DISK_MAGIC_LEN; i++) {
        if (sector[i] != DISK_MAGIC[i]) {
            fs_dbg("DISK: no filesystem\n");
            return -1;
        }
    }

    uint32_t node_count = read32(sector, 8);
    if (node_count != MAX_FILES) {
        fs_dbg("DISK: bad node count\n");
        return -1;
    }

    /* --- Clear existing nodes --- */
    for (int i = 0; i < MAX_FILES; i++) {
        if (nodes[i].data) {
            kfree(nodes[i].data);
            nodes[i].data = (char *)0;
        }
        nodes[i].used = false;
    }

    /* --- Read node table (sectors 1-16) --- */
    for (int s = 0; s < DISK_NODE_SECTORS; s++) {
        if (ata_read_sectors(DISK_NODE_SECTOR + (uint32_t)s, 1, sector) < 0) {
            fs_dbg("DISK: node read fail\n");
            return -1;
        }

        for (int n = 0; n < 4; n++) {
            int idx = s * 4 + n;
            int off = n * DISK_NODE_SIZE;
            struct fs_node *node = &nodes[idx];

            mem_copy(node->name, sector + off, MAX_FILENAME);
            node->name[MAX_FILENAME - 1] = '\0';
            node->type = (uint8_t)sector[off + 32];
            node->used = (sector[off + 33] != 0);
            node->size = read32(sector, off + 36);
            node->parent_idx = (int)read32(sector, off + 40);
            node->child_count = (int)read32(sector, off + 44);
            for (int c = 0; c < MAX_CHILDREN; c++) {
                node->children[c] = (int)read32(sector, off + 48 + c * 4);
            }
            node->data = (char *)0;

            /* Read file data if present */
            uint32_t data_sec = read32(sector, off + 112);
            uint32_t data_len = read32(sector, off + 116);

            if (node->used && node->type == FS_FILE && data_sec != 0xFFFFFFFF && data_len > 0) {
                node->data = (char *)kmalloc(MAX_FILE_DATA);
                if (!node->data) {
                    fs_dbg("DISK: alloc fail\n");
                    node->size = 0;
                    continue;
                }

                /* Read data sectors */
                uint32_t nsectors = sectors_for(data_len);
                uint32_t loaded = 0;
                char dbuf[512];
                for (uint32_t ds = 0; ds < nsectors; ds++) {
                    if (ata_read_sectors(DISK_DATA_SECTOR + data_sec + ds, 1, dbuf) < 0) {
                        fs_dbg("DISK: data read fail\n");
                        break;
                    }
                    uint32_t chunk = data_len - loaded;
                    if (chunk > 512) chunk = 512;
                    mem_copy(node->data + loaded, dbuf, chunk);
                    loaded += chunk;
                }
                node->data[data_len] = '\0';
                node->size = data_len;
            }
        }
    }

    cwd_idx = 0;
    fs_dbg("DISK: loaded ok\n");
    return 0;
}
