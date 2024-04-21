

#include <errno.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"

#include "vfs_fat_spiflash.h"
#include "vfs_private.h"
#include "logger_events.h"
#include "logger_common.h"


#ifdef CONFIG_USE_FATFS
#define WRITE_BUFFER_SIZE (16 * 1024)
typedef struct wl_context_s {
    uint8_t mounted;
    const char *mount_point;
    const char *base_label;
    wl_handle_t volume_handle;
    size_t allocation_unit_size;
} wl_context_t;
# define WL_CONTEXT_INIT {0, CONFIG_FATFS_MOUNT_POINT, CONFIG_FATFS_PARTITION_LABEL, WL_INVALID_HANDLE, WRITE_BUFFER_SIZE}

static struct wl_context_s wl_ctx = WL_CONTEXT_INIT;

static const char *TAG = "vfs_fat_spiflash";
#define FATFS_MODE_READ_ONLY 0
#define FATFS_LONG_NAMES 1

int fatfs_init() {
    TIMER_S
    ESP_LOGI(TAG, "Mounting FAT filesystem to mountpoint:%s, label:%s", wl_ctx.mount_point, wl_ctx.base_label);
    // To mount device we need name of device partition, define mount_point
    // and allow format partition in case if it is new one and was not formatted
    // before
    esp_err_t err = ESP_OK;
    if(has_fatfs_partition() == 0) {
        ESP_LOGI(TAG, "FATFS partition not found");
        goto end;
    }
    const esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 8,
        .format_if_mount_failed = false,
        .allocation_unit_size = wl_ctx.allocation_unit_size};
    if (FATFS_MODE_READ_ONLY) {
        err = esp_vfs_fat_spiflash_mount_ro(wl_ctx.mount_point, wl_ctx.base_label, &mount_config);
    } else {
        err = esp_vfs_fat_spiflash_mount_rw_wl(wl_ctx.mount_point, wl_ctx.base_label, &mount_config, &wl_ctx.volume_handle);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        esp_event_post(LOGGER_EVENT, LOGGER_EVENT_FAT_PARTITION_MOUNT_FAILED, 0, 0, portMAX_DELAY);
        goto end;
    } else {
        wl_ctx.mounted = 1;
        esp_event_post(LOGGER_EVENT, LOGGER_EVENT_FAT_PARTITION_MOUNTED, 0, 0, portMAX_DELAY);
    }
    end:
    TIMER_E
    return err;
}

int fatfs_uninit() {
    if (!wl_ctx.mounted)
        return 0;
    #ifdef DEBUG
    ESP_LOGI(TAG, "Unmounting FAT filesystem");
    #endif
    if (FATFS_MODE_READ_ONLY) {
        esp_vfs_fat_spiflash_unmount_ro(wl_ctx.mount_point, wl_ctx.base_label);
    } else {
        esp_vfs_fat_spiflash_unmount_rw_wl(wl_ctx.mount_point, wl_ctx.volume_handle);
    }
    #ifdef DEBUG
    ESP_LOGI(TAG, "Done");
    #endif
    wl_ctx.mounted = 0;
    esp_event_post(LOGGER_EVENT, LOGGER_EVENT_FAT_PARTITION_UNMOUNTED, 0, 0, portMAX_DELAY);
    return 0;
}
#endif