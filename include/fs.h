#ifndef FS_H
#define FS_H

#include "types.h"

#define FS_FILE      0
#define FS_DIR       1
#define MAX_FILES    64
#define MAX_FILENAME 32
#define MAX_FILE_DATA  65536
#define MAX_TEXT_DATA  4096
#define MAX_CHILDREN   16

struct fs_node {
    char name[MAX_FILENAME];
    uint8_t type;
    uint32_t size;
    char *data;
    int parent_idx;
    int children[MAX_CHILDREN];
    int child_count;
    bool used;
};

void     fs_init(void);
void     fs_init_defaults(void);
int      fs_create(const char *name, uint8_t type);
int      fs_find(const char *name);
int      fs_write_file(int idx, const char *data, uint32_t size);
int      fs_append_file(int idx, const char *data, uint32_t size);
int      fs_read_file(int idx, char *buffer, uint32_t max_size);
int      fs_delete(const char *name);
int      fs_change_dir(const char *name);
int      fs_get_cwd(void);
const char *fs_get_cwd_name(void);
struct fs_node *fs_get_node(int idx);
void     fs_get_path(int idx, char *buf, int buf_size);
int      fs_save_to_disk(void);
int      fs_load_from_disk(void);
void     fs_enable_autosave(void);

#endif
