/* ===========================================================================
 * RAM Filesystem
 * In-memory filesystem with directories and files
 * =========================================================================== */
#include "fs.h"
#include "string.h"
#include "memory.h"

static struct fs_node nodes[MAX_FILES];
static int cwd_idx = 0;  /* current working directory index */

void fs_init(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        nodes[i].used = false;
    }

    /* Create root directory */
    nodes[0].used = true;
    str_copy(nodes[0].name, "/");
    nodes[0].type = FS_DIR;
    nodes[0].size = 0;
    nodes[0].parent_idx = 0; /* root is its own parent */
    nodes[0].child_count = 0;
    cwd_idx = 0;

    /* Create some default directories */
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
            const char *m = "AI_OS v0.2 - Built by Claude\n";
            fs_write_file(motd, m, str_len(m));
        }
        cwd_idx = old_cwd;
    }
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
    nodes[idx].data[0] = '\0';
    nodes[idx].parent_idx = cwd_idx;
    nodes[idx].child_count = 0;

    /* Add to parent's children */
    struct fs_node *parent = &nodes[cwd_idx];
    if (parent->child_count < MAX_CHILDREN) {
        parent->children[parent->child_count++] = idx;
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

    mem_copy(nodes[idx].data, data, size);
    nodes[idx].data[size] = '\0';
    nodes[idx].size = size;
    return 0;
}

int fs_append_file(int idx, const char *data, uint32_t size) {
    if (idx < 0 || idx >= MAX_FILES || !nodes[idx].used) return -1;
    if (nodes[idx].type != FS_FILE) return -1;
    if (nodes[idx].size + size > MAX_FILE_DATA - 1) {
        size = MAX_FILE_DATA - 1 - nodes[idx].size;
    }
    if (size == 0) return -1;

    mem_copy(nodes[idx].data + nodes[idx].size, data, size);
    nodes[idx].size += size;
    nodes[idx].data[nodes[idx].size] = '\0';
    return 0;
}

int fs_read_file(int idx, char *buffer, uint32_t max_size) {
    if (idx < 0 || idx >= MAX_FILES || !nodes[idx].used) return -1;
    if (nodes[idx].type != FS_FILE) return -1;

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
