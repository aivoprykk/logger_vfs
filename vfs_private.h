#ifndef B790F55A_9B95_40FD_ABDB_89580F2559CF
#define B790F55A_9B95_40FD_ABDB_89580F2559CF

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#include <errno.h>

#include "esp_err.h"

#include "sdkconfig.h"
#if (defined(CONFIG_LOGGER_USE_GLOBAL_LOG_LEVEL) && CONFIG_LOGGER_GLOBAL_LOG_LEVEL < CONFIG_LOGGER_VFS_LOG_LEVEL)
#define C_LOG_LEVEL CONFIG_LOGGER_GLOBAL_LOG_LEVEL
#else
#define C_LOG_LEVEL CONFIG_LOGGER_VFS_LOG_LEVEL
#endif
#include "common_log.h"

enum vfs_data_e {
  VFS_MIN_MEM_SIZE_FOR_FLASH_MOUNT = 180000,
};

#define PATH_MAX_CHAR_SIZE 64
off_t s_xstat_file_size(int f);
int get_file_path_width_base(char *topath, size_t pathlen, const char *name, const char *base);
int has_fatfs_partition();
int has_spiffs_partition();
int has_littlefs_partition();
int write_speed(const char *name, const char * mount_point);
int s_remove_file(const char *name, const char *base);
bool vfs_suspend_for_maintenance(void);
void vfs_resume_from_maintenance(void);

#ifdef __cplusplus
}
#endif

#endif /* B790F55A_9B95_40FD_ABDB_89580F2559CF */
