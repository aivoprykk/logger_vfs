#ifndef A0598F48_FF1D_49EC_AD52_0FF0E29863F9
#define A0598F48_FF1D_49EC_AD52_0FF0E29863F9

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "logger_common.h"
#include "esp_vfs.h"

#define PATH_MAX_CHAR_SIZE 64
#define VFS_FILE_PATH_MAX (ESP_VFS_PATH_MAX + 209)
#define VFS_MAX_PARTS 2
#define VFS_CFG_ENUM_PRE(l) VFS_PART_##l
#define VFS_CFG_ENUM_PRE_N(l)  VFS_CFG_ENUM_PRE(l),
#define VFS_CFG_PART_LIST(l) l(FATFS) l(SPIFFS) l(LITTLEFS) l(SDCARD)
typedef enum {
    VFS_CFG_PART_LIST(VFS_CFG_ENUM_PRE_N)
    VFS_PART_MAX
} vfs_part_t;

typedef struct vfs_config_s {
    const char *mount_point;
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint64_t used_bytes;
    uint8_t is_mounted;
    uint8_t write_attempts;
    vfs_part_t part_type;
} vfs_config_t;

#define REP2(str) str, str
#define REP3(str) str, str, str
#define REP4(str) REP2(str), REP2(str)
#define REP6(str) REP3(str), REP3(str)

#define VFS_CFG_DEFAULT() {REP6(0)}

typedef struct vfs_s {
    vfs_config_t parts[VFS_MAX_PARTS];
    uint8_t config_part;
    uint8_t gps_log_part;
    // uint8_t web_part;
} vfs_t;

#define VFS_DEDAULTS() { \
{VFS_CFG_DEFAULT(), VFS_CFG_DEFAULT()}, \
REP2(VFS_PART_MAX) \
}

extern vfs_t vfs_ctx;

int vfs_init(void);
int vfs_deinit(void);
int vfs_select_part(uint8_t log_part_locked);
int vfs_print_space(const char *mp, uint64_t total_bytes, uint64_t free_bytes, uint64_t used_bytes);
int vfs_fs_space(const char * mp, uint8_t type, uint64_t *total_bytes, uint64_t *free_bytes, uint64_t *used_bytes);
void vfs_update_space(void*arg);
const vfs_config_t * vfs_get_part(const char * mount_point);
int vfs_get_part_index(const char * mount_point);
int s_xfile_exists(const char *filename);
char *s_read_from_file(const char *name, const char * base);
int s_open(const char *name, const char * base, const char *mode);
int s_write(const char *name, const char * base, char *data, size_t len);
FILE *s_open_file(const char *name, const char * base, const char *mode);
int s_rename_file(const char *old, const char * n, const char * base);
int s_rename_file_n(const char *old, const char *n, uint8_t rmifexists);
int vfs_space_str(char*arg, size_t arglen);
#ifdef __cplusplus
}
#endif

#endif /* A0598F48_FF1D_49EC_AD52_0FF0E29863F9 */
