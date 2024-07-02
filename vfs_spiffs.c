

#include <errno.h>

#include "esp_log.h"

#include "vfs_spiffs.h"
#include "vfs_private.h"
#include "logger_events.h"
#include "logger_common.h"


#ifdef CONFIG_USE_SPIFFS
#include "esp_spiffs.h"
static const char *TAG = "vfs_spiffs";
TIMER_INIT
typedef struct wl_context_s {
    uint8_t mounted;
    const char *mount_point;
    const char *base_label;
} wl_context_t;
# define WL_CONTEXT_INIT {0, CONFIG_SPIFFS_MOUNT_POINT, CONFIG_SPIFFS_PARTITION_LABEL}
static struct wl_context_s wl_ctx = WL_CONTEXT_INIT;

int spiffs_init(void) {
    TIMER_S
    if(has_spiffs_partition() == 0) {
        ESP_LOGI(TAG, "SPIFFS partition not found");
        goto done;
    }
    esp_vfs_spiffs_conf_t conf = {.base_path = wl_ctx.mount_point,
                                  .partition_label = wl_ctx.base_label,
                                  .max_files = 8,
                                  .format_if_mount_failed = false};
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        goto done;
    }
    wl_ctx.mounted = 1;
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    done:
    TIMER_E
    return ESP_OK;
}

int spiffs_uninit() {
    if(wl_ctx.mounted == 0) {
        return 0;
    }
    esp_vfs_spiffs_unregister(NULL);
    #ifdef DEBUG
    ESP_LOGD(TAG, "SPIFFS unmounted");
    #endif
    wl_ctx.mounted = 0;
    return 0;
}
#endif