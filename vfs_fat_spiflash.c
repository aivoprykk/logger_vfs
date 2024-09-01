

#include <errno.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"

#include "vfs_fat_spiflash.h"
#include "vfs_private.h"
#include "logger_events.h"
// #include "logger_common.h"


#ifdef CONFIG_USE_FATFS
// #define WRITE_BUFFER_SIZE 4096

typedef struct wl_context_s {
    uint8_t mounted;
    const char *mount_point;
    const char *base_label;
    wl_handle_t volume_handle;
    size_t allocation_unit_size;
} wl_context_t;
# define WL_CONTEXT_INIT {0, CONFIG_FATFS_MOUNT_POINT, CONFIG_FATFS_PARTITION_LABEL, WL_INVALID_HANDLE, CONFIG_WL_SECTOR_SIZE}

static struct wl_context_s wl_ctx = WL_CONTEXT_INIT;

static const char *TAG = "vfs_fat_spiflash";

#define FATFS_LONG_NAMES 1

int fatfs_init() {
    IMEAS_START();
#if defined(CONFIG_FATFS_MODE_READ_ONLY)
    int ro = 1;
#else
    int ro = 0;
#endif
    ILOG(TAG, "[%s] Mounting FAT filesystem to mountpoint:%s, label:%s, %d", __func__, wl_ctx.mount_point, wl_ctx.base_label, ro);
    // To mount device we need name of device partition, define mount_point
    // and allow format partition in case if it is new one and was not formatted
    // before
    esp_err_t err = ESP_OK;
    if(has_fatfs_partition() == 0) {
        ESP_LOGW(TAG, "[%s] FATFS partition not found", __func__);
        goto end;
    }
    const esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 4,
        .format_if_mount_failed = false,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        .disk_status_check_enable = false};
#if defined(CONFIG_FATFS_MODE_READ_ONLY)
    err = esp_vfs_fat_spiflash_mount_ro(wl_ctx.mount_point, wl_ctx.base_label, &mount_config);
#else
    err = esp_vfs_fat_spiflash_mount_rw_wl(wl_ctx.mount_point, wl_ctx.base_label, &mount_config, &wl_ctx.volume_handle);
#endif
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s] Failed to mount FATFS (%s)", __func__, esp_err_to_name(err));
        esp_event_post(LOGGER_EVENT, LOGGER_EVENT_FAT_PARTITION_MOUNT_FAILED, 0, 0, portMAX_DELAY);
        goto end;
    } else {
        wl_ctx.mounted = 1;
        esp_event_post(LOGGER_EVENT, LOGGER_EVENT_FAT_PARTITION_MOUNTED, 0, 0, portMAX_DELAY);
    }
    end:
    IMEAS_END(TAG, "[%s] took %llu us", __func__);
    return err;
}

int fatfs_uninit() {
    ILOG(TAG, "[%s]", __func__);
    if (!wl_ctx.mounted)
        return 0;
    #ifdef DEBUG
    #endif
#if defined(CONFIG_FATFS_MODE_READ_ONLY)
    esp_vfs_fat_spiflash_unmount_ro(wl_ctx.mount_point, wl_ctx.base_label);
#else
    esp_vfs_fat_spiflash_unmount_rw_wl(wl_ctx.mount_point, wl_ctx.volume_handle);
#endif
    ILOG(TAG, "[%s] Filesystem unmounted", __func__);
    wl_ctx.mounted = 0;
    esp_event_post(LOGGER_EVENT, LOGGER_EVENT_FAT_PARTITION_UNMOUNTED, 0, 0, portMAX_DELAY);
    return 0;
}

bool fatfs_is_mounted(void) {
    return wl_ctx.mounted;
}

#endif
