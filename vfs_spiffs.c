#include "vfs_private.h"
#ifdef CONFIG_USE_SPIFFS

#include "esp_spiffs.h"

#include "vfs_spiffs.h"

static const char *TAG = "vfs_spiffs";
typedef struct wl_context_s {
    uint8_t mounted;
    const char *mount_point;
    const char *base_label;
} wl_context_t;
# define WL_CONTEXT_INIT {0, CONFIG_SPIFFS_MOUNT_POINT, CONFIG_SPIFFS_PARTITION_LABEL}
static struct wl_context_s wl_ctx = WL_CONTEXT_INIT;

int spiffs_init(void) {
    FUNC_ENTRY(TAG);
    return ESP_OK;
}
int spiffs_mount(void) {
    FUNC_ENTRY(TAG);
    if(has_spiffs_partition() == 0) {
        WLOG(TAG, "[%s] SPIFFS partition not found", __func__);
        goto done;
    }
    esp_vfs_spiffs_conf_t conf = {.base_path = wl_ctx.mount_point,
                                  .partition_label = wl_ctx.base_label,
                                  .max_files = 8,
                                  .format_if_mount_failed = false};
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ELOG(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ELOG(TAG, "Failed to find SPIFFS partition");
        } else {
            ELOG(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        goto done;
    }
    wl_ctx.mounted = 1;
    done:
    return ret;
}

void spiffs_uninit() {
    FUNC_ENTRY(TAG);
}

void spiffs_umount() {
    FUNC_ENTRY(TAG);
    if(!wl_ctx.mounted) {
        esp_vfs_spiffs_unregister(NULL);
        wl_ctx.mounted = 0;
    }
}

bool spiffs_is_mounted() {
    return wl_ctx.mounted;
}

#endif
