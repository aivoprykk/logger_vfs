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

#if (CONFIG_LOGGER_VFS_LOG_LEVEL <= 2)

#include "esp_timer.h"
#include "esp_log.h"

#ifndef LOG_INFO
#define LOG_INFO(a, b, ...) ESP_LOGI(a, b, __VA_ARGS__)
#endif
#ifndef MEAS_START
#define MEAS_START() uint64_t _start = (esp_timer_get_time())
#endif
#ifndef MEAS_END
#define MEAS_END(a, b, ...) \
    ESP_LOGI(a, b, __VA_ARGS__, (esp_timer_get_time() - _start))
#endif
#endif

#if defined(CONFIG_LOGGER_VFS_LOG_LEVEL_TRACE) // "A lot of logs to give detailed information"

#define DLOG LOG_INFO
#define DMEAS_START MEAS_START
#define DMEAS_END MEAS_END
#define ILOG LOG_INFO
#define IMEAS_START MEAS_START
#define IMEAS_END MEAS_END
#define WLOG LOG_INFO
#define WMEAS_START MEAS_START
#define WMEAS_END MEAS_END

#elif defined(CONFIG_LOGGER_VFS_LOG_LEVEL_INFO) // "Log important events"

#define DLOG(a, b, ...) ((void)0)
#define DMEAS_START() ((void)0)
#define DMEAS_END(a, b, ...) ((void)0)
#define ILOG LOG_INFO
#define IMEAS_START MEAS_START
#define IMEAS_END MEAS_END
#define WLOG LOG_INFO
#define WMEAS_START MEAS_START
#define WMEAS_END MEAS_END

#elif defined(CONFIG_LOGGER_VFS_LOG_LEVEL_WARN) // "Log if something unwanted happened but didn't cause a problem"

#define DLOG(a, b, ...) ((void)0)
#define DMEAS_START() ((void)0)
#define DMEAS_END(a, b, ...) ((void)0)
#define ILOG(a, b, ...) ((void)0)
#define IMEAS_START() ((void)0)
#define IMEAS_END(a, b, ...) ((void)0)
#define WLOG LOG_INFO
#define WMEAS_START MEAS_START
#define WMEAS_END MEAS_END

#else // "Do not log anything"

#define DLOG(a, b, ...) ((void)0)
#define DMEAS_START() ((void)0)
#define DMEAS_END(a, b, ...) ((void)0)
#define ILOG(a, b, ...) ((void)0)
#define IMEAS_START() ((void)0)
#define IMEAS_END(a, b, ...) ((void)0)
#define WLOG(a, b, ...) ((void)0)
#define WMEAS_START() ((void)0)
#define WMEAS_END(a, b, ...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* B790F55A_9B95_40FD_ABDB_89580F2559CF */
