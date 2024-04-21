#ifndef B790F55A_9B95_40FD_ABDB_89580F2559CF
#define B790F55A_9B95_40FD_ABDB_89580F2559CF

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <sys/types.h>

off_t s_xstat_file_size(int f);
int get_file_path_width_base(char *topath, size_t pathlen, const char *name, const char *base);
int has_fatfs_partition();
int has_spiffs_partition();
int has_littlefs_partition();
#ifdef __cplusplus
}
#endif

#endif /* B790F55A_9B95_40FD_ABDB_89580F2559CF */
