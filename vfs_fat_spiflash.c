#include "vfs_private.h"
#ifdef CONFIG_USE_FATFS

#include "vfs_fat_spiflash.h"

#include "esp_vfs_fat.h"

#include "vfs_events.h"
#include "hal/efuse_hal.h"

typedef struct wl_context_s {
    uint8_t mounted;
    const char *mount_point;
    const char *base_label;
    wl_handle_t volume_handle;
    size_t allocation_unit_size;
    uint8_t available;
} wl_context_t;
# define WL_CONTEXT_INIT {0, CONFIG_FATFS_MOUNT_POINT, CONFIG_FATFS_PARTITION_LABEL, WL_INVALID_HANDLE, CONFIG_WL_SECTOR_SIZE, 1}

static struct wl_context_s wl_ctx = WL_CONTEXT_INIT;

static const char *TAG = "vfs_fat_spiflash";

#define FATFS_LONG_NAMES 1

int fatfs_init() {
    if(heap_caps_get_total_size(MALLOC_CAP_8BIT) < VFS_MIN_MEM_SIZE_FOR_FLASH_MOUNT) {
        WLOG(TAG, "[%s] Not enough mem (%u < 180000) to mount FATFS for this chip.", __func__, heap_caps_get_total_size(MALLOC_CAP_8BIT));
        wl_ctx.available = 0;
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_event_post(VFS_EVENT, VFS_EVENT_FAT_PARTITION_INIT_DONE, 0, 0, portMAX_DELAY);
    return ESP_OK;
}

int fatfs_mount() {
#if defined(CONFIG_FATFS_MODE_READ_ONLY)
    int ro = 1;
#else
    int ro = 0;
#endif
    ILOG(TAG, "[%s] Mounting FAT filesystem to mountpoint:%s, label:%s, %d", __func__, wl_ctx.mount_point, wl_ctx.base_label, ro);
    // To mount device we need name of device partition, define mount_point
    // and allow format partition in case if it is new one and was not formatted
    // before
    esp_err_t ret = ESP_OK;
    if(has_fatfs_partition() == 0) {
        WLOG(TAG, "[%s] FATFS partition not found", __func__);
        goto done;
    }
    const esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 4,
        .format_if_mount_failed = false,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        .disk_status_check_enable = false};
    if(ro)
        ret = esp_vfs_fat_spiflash_mount_ro(wl_ctx.mount_point, wl_ctx.base_label, &mount_config);
    else
        ret = esp_vfs_fat_spiflash_mount_rw_wl(wl_ctx.mount_point, wl_ctx.base_label, &mount_config, &wl_ctx.volume_handle);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem.");
         } else {
            ELOG(TAG, "Failed to initialize fat partition (%s).", esp_err_to_name(ret));
        }
        // esp_event_post(VFS_EVENT, VFS_EVENT_SDCARD_MOUNT_FAILED, 0, 0, portMAX_DELAY);
        goto done;
    }
    else {
        wl_ctx.mounted = 1;
    }
    done:
    return ret;
}

int fatfs_format(const char *mountpoint) {
    ILOG(TAG, "[%s] Formatting FAT filesystem on mount_point:%s, label:%s", __func__, wl_ctx.mount_point, wl_ctx.base_label);
    // For now, only support the default mountpoint
    // const char * mp = wl_ctx.mount_point;
    // while(*mp=='/') ++mp;
    if (strcmp(mountpoint, wl_ctx.mount_point) != 0) {
        ESP_LOGE(TAG, "Unsupported mountpoint for format: %s", mountpoint);
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = esp_vfs_fat_spiflash_format_rw_wl(wl_ctx.mount_point, wl_ctx.base_label);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to format FAT filesystem. (%s)", esp_err_to_name(ret));
    }
    return ret;
}

void fatfs_uninit() {
#if C_LOG_LEVEL <= LOG_INFO_NUM
    ILOG(TAG, "[%s]", __func__);
#endif
}

void fatfs_umount() {
#if C_LOG_LEVEL <= LOG_INFO_NUM
    ILOG(TAG, "[%s]", __func__);
#endif
    esp_err_t ret = ESP_OK;
    if (wl_ctx.mounted) {
#if defined(CONFIG_FATFS_MODE_READ_ONLY)
        int ro = 1;
#else
        int ro = 0;
#endif
        if(ro)
            ret = esp_vfs_fat_spiflash_unmount_ro(wl_ctx.mount_point, wl_ctx.base_label);
        else
            ret = esp_vfs_fat_spiflash_unmount_rw_wl(wl_ctx.mount_point, wl_ctx.volume_handle);
#if (C_LOG_LEVEL <= LOG_INFO_NUM)
        if (ret == ESP_OK)
            ILOG(TAG, "[%s] Filesystem unmounted", __func__);
#endif
        wl_ctx.mounted = 0;
        esp_event_post(VFS_EVENT, VFS_EVENT_FAT_PARTITION_UNMOUNTED, 0, 0, portMAX_DELAY);
        UNUSED_PARAMETER(ret);
    }
}

bool fatfs_is_mounted(void) {
    return wl_ctx.mounted;
}

#endif
