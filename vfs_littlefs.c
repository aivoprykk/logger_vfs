
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_idf_version.h"
#include "esp_flash.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "esp_chip_info.h"
#include "spi_flash_mmap.h"
#endif
#include "esp_littlefs.h"

#include "vfs_littlefs.h"
#include "vfs_private.h"
#include "logger_common.h"
#include "logger_events.h"

#ifdef CONFIG_USE_LITTLEFS
typedef struct wl_context_s {
    uint8_t mounted;
    const char *mount_point;
    const char *base_label;
} wl_context_t;
# define WL_CONTEXT_INIT {0, CONFIG_LITTLEFS_MOUNT_POINT, CONFIG_LITTLEFS_PARTITION_LABEL}

static struct wl_context_s wl_ctx = WL_CONTEXT_INIT;

static const char *TAG = "vfs_littlefs";

int littlefs_init() {
    LOG_INFO(TAG, "[%s]", __func__);
    MEAS_START();

    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU cores, WiFi%s%s, ",
            CONFIG_IDF_TARGET,
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    uint32_t size_flash_chip = 0;
    esp_flash_get_size(NULL, &size_flash_chip);
    printf("%uMB %s flash\n", (unsigned int)size_flash_chip >> 20, (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    esp_err_t ret = ESP_OK;
    if(has_littlefs_partition() == 0) {
        ESP_LOGI(TAG, "LittleFS partition not found");
        goto done;
    }
    ESP_LOGI(TAG, "Initializing LittleFS");
    esp_vfs_littlefs_conf_t conf = {
            .base_path = wl_ctx.mount_point,
            .partition_label = wl_ctx.base_label,
            .format_if_mount_failed = false,
            .dont_mount = false,
            .read_only = false,
    };
    // Use settings defined above to initialize and mount LittleFS filesystem.
    // Note: esp_vfs_littlefs_register is an all-in-one convenience function.
    ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK)
    {
            if (ret == ESP_FAIL)
            {
                    ESP_LOGE(TAG, "Failed to mount or format filesystem");
            }
            else if (ret == ESP_ERR_NOT_FOUND)
            {
                    ESP_LOGE(TAG, "Failed to find LittleFS partition");
            }
            else
            {
                    ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
            }
            goto done;
    }
    wl_ctx.mounted = 1;

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK)
    {
            ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
    }
    else
    {
            ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    done:
    MEAS_END(TAG, "[%s] took %llu us", __func__);
    return ret;
}

int littlefs_deinit() {
    if(wl_ctx.mounted == 0)
        return 0;
    esp_err_t ret = esp_vfs_littlefs_unregister(wl_ctx.base_label);
    wl_ctx.mounted = 0;
    return ret;
}

#endif