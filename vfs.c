#include "numstr.h"
#include "vfs_private.h"
#if defined(CONFIG_LOGGER_VFS_ENABLED)

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "vfs.h"
#include "strbf.h"

#include "vfs_events.h"

#if defined(CONFIG_USE_SD_CARD) || defined(CONFIG_USE_FATFS)
#include "esp_vfs_fat.h"
#endif
#ifdef CONFIG_USE_SD_CARD
#include "vfs_fat_sdspi.h"
#endif
#ifdef CONFIG_USE_FATFS
#include "vfs_fat_spiflash.h"
#endif
#ifdef CONFIG_USE_SPIFFS
#include "vfs_spiffs.h"
#include "esp_spiffs.h"
#endif
#ifdef CONFIG_USE_LITTLEFS
#include "vfs_littlefs.h"
#include "esp_littlefs.h"
#endif

#include "esp_timer.h"

// Forward declarations for app mode checking
typedef enum {
    APP_MODE_UNKNOWN = 0,
    APP_MODE_BOOT,
    APP_MODE_WIFI,
    APP_MODE_GPS,
    APP_MODE_SLEEP,
    APP_MODE_SHUT_DOWN,
    APP_MODE_RESTART,
    APP_MODE_CHARGE
} app_mode_t;

struct main_ctx_s {
    app_mode_t app_mode;
    // ... other fields not needed here
};

extern struct main_ctx_s m_app_ctx;

#define STRINGIFY_EVENT(x, y) STRINGIFY_(VFS_EVENT_ ## y ## _ ## x),

ESP_EVENT_DEFINE_BASE(VFS_EVENT);

#if (C_LOG_LEVEL <= LOG_INFO_NUM)
static const char * const _vfs_event_strings[] = {
#if defined(CONFIG_USE_SD_CARD)
    VFS_EVENT_LIST(STRINGIFY_EVENT, SDCARD)
#endif
#if defined(CONFIG_USE_FATFS)
    VFS_EVENT_LIST(STRINGIFY_EVENT, FAT_PARTITION)
#endif
#if defined(CONFIG_USE_LITTLEFS)
    VFS_EVENT_LIST(STRINGIFY_EVENT, LITTEFS_PARTITION)
#endif
#if defined(CONFIG_USE_SPIFFS)
    VFS_EVENT_LIST(STRINGIFY_EVENT, SPIFFS_PARTITION)
#endif
    "VFS_EVENT_LOG_PARTITION_CHANGED"
};
const char * vfs_event_strings(int id) {
    return id < lengthof(_vfs_event_strings) ? _vfs_event_strings[id] : "VFS_EVENT_UNKNOWN";
}
#else
const char * vfs_event_strings(int id) {return "VFS_EVENT";}
#endif

static const char *TAG = "vfs";
static esp_timer_handle_t sd_timer = 0;
static esp_timer_handle_t fs_size_timer = 0;
vfs_t vfs_ctx = VFS_DEDAULTS();
static int do_space_print = 0;
// Shared path buffer to reduce stack allocation
static char shared_path_buffer[PATH_MAX_CHAR_SIZE];
// Binary semaphore (not mutex) to avoid priority inheritance issues with timeouts
static SemaphoreHandle_t path_buffer_mutex = NULL;
// Compatibility: keep mount semaphore for parts of code that still reference it
static SemaphoreHandle_t mount_sem = NULL;
// Work queue for mount/file operations (single worker serializes long ops)
static QueueHandle_t vfs_work_queue = NULL;
static TaskHandle_t mount_task_handle = NULL;
static bool mount_task_is_running = false;
static volatile bool vfs_shutdown_in_progress = false;  /* Track shutdown state separately */

/* Local spinlock for small critical sections (use portENTER/EXIT_CRITICAL to be explicit) */
static portMUX_TYPE vfs_lock = portMUX_INITIALIZER_UNLOCKED;

// Bitmask to track pending work items and avoid duplicates (coalescing)
static volatile uint32_t vfs_pending_work_mask = 0;
#define VFS_WORK_BIT(t) (1u << (uint32_t)(t))

/* Per-work-type pending argument storage so repeated posts update the latest arg
 * (coalescing with "latest wins" semantics). Size is conservative. */
static void *vfs_pending_args[8] = {0};

#if defined(CONFIG_GPS_LOG_ENABLED)
/* Generic work interface registered by consumers (e.g., gps_log) to decouple VFS from modules. */
struct gps_context_s;
typedef struct vfs_work_if_s vfs_work_if_t; // from vfs.h
static vfs_work_if_t g_vfs_iface = {0};

void vfs_register_work_interface(const vfs_work_if_t *iface) {
    portENTER_CRITICAL(&vfs_lock);
    if (iface) {
        g_vfs_iface = *iface; // copy by value
    } else {
        memset(&g_vfs_iface, 0, sizeof(g_vfs_iface));
    }
    portEXIT_CRITICAL(&vfs_lock);

    if (iface) ILOG(TAG, "[vfs] work interface registered");
    else ILOG(TAG, "[vfs] work interface unregistered");
}
#endif

// Forward declare vfs_post_work for use in timer callback
esp_err_t vfs_post_work(vfs_work_type_t type, void *arg);


static esp_err_t try_open_write(const char *name, const char * mount_point, void (*cb)(void*), void *arg) {
    FUNC_ENTRYD(TAG);
    if (name == 0 || *name == 0)
        return ESP_FAIL;
        
    // Safety check: Don't operate if VFS is not initialized
    // if (!vfs_ctx.vfs_initialized) {
    //     WLOG(TAG, "[%s] VFS not initialized, cannot open file", __func__);
    //     return ESP_FAIL;
    // }
    int attempts = 1; // Reduced from 2 to 1 to speed up operation
    FILE *f = 0;
    try_again:
    f = s_open_file(name, mount_point, "w");
    if (f == NULL) {
        WLOG(TAG, "[%s] Failed to open file in %s.", __func__, mount_point);
        if(attempts-- > 0) {
            s_remove_file(name, mount_point);
            // Small delay instead of potentially blocking operation
            vTaskDelay(pdMS_TO_TICKS(1));
            goto try_again;
        }
        return ESP_FAIL;
    }
    if(cb) cb(arg);
    fclose(f);
    return ESP_OK;
}

#ifdef CONFIG_SD_DEBUG_STATS
#define TIME_ARRAY_SIZE 500
#define PRINT_DIFF 0
#define WRITE_BUFFER_SIZE (16 * 1024)

esp_err_t write_speed(const char *name, const char * mount_point) {
    FUNC_ENTRY(TAG);
    if (name == 0 || *name == 0)
        return ESP_FAIL;
    // ILOG(TAG, "[%s] file:%s", __FUNCTION__, name);
    FILE *f = s_open_file(name, mount_point, "w");
    if (f == NULL) {
        ELOG(TAG, "Failed to open file for writing");
        return ESP_FAIL;
    }
    uint64_t time_array[TIME_ARRAY_SIZE];
    char write_buffer[WRITE_BUFFER_SIZE];
    FUNC_ENTRY_ARGS(TAG, " Init write buffer ");
    // initialize write buffer
    for (int i = 0; i < WRITE_BUFFER_SIZE; i++) {
        write_buffer[i] = ' ' + (i % 64);
    }

    // ILOG(TAG, "[%s] Write to file ", __FUNCTION__);
    uint64_t start = esp_timer_get_time();
    for (int counter = 0; counter < TIME_ARRAY_SIZE; counter++) {
        fwrite(write_buffer, 1, WRITE_BUFFER_SIZE, f);
        time_array[counter] = esp_timer_get_time();
    }
    fclose(f);
    // ILOG(TAG, "[%s] File written ", __FUNCTION__);

    uint64_t sum = 0;
    uint64_t maximum = 0;
    uint64_t minimum = UINT64_MAX;
    for (int i = 0; i < TIME_ARRAY_SIZE; i++) {
        uint64_t end = time_array[i];
        uint64_t diff = end - start;
        maximum = (diff > maximum) ? diff : maximum;
        minimum = (diff < minimum) ? diff : minimum;
        sum += diff;
        start = end;
    }
    uint64_t average = sum / TIME_ARRAY_SIZE;
    ILOG(TAG, "write buffer size = %d", WRITE_BUFFER_SIZE);
    ILOG(TAG, "sum=%llu microseconds, average=%llu microseconds", sum, average);
    ILOG(TAG, "maximum=%llu microseconds, minimum=%llu microseconds", maximum, minimum);
    ILOG(TAG, "highest write speed = %llu byte/s", ((uint64_t)WRITE_BUFFER_SIZE) * 1000 * 1000 / minimum);
    ILOG(TAG, "average write speed = %llu byte/s", ((uint64_t)WRITE_BUFFER_SIZE) * 1000 * 1000 / average);
    ILOG(TAG, "lowest write speed = %llu byte/s", ((uint64_t)WRITE_BUFFER_SIZE) * 1000 * 1000 / maximum);
    return ESP_OK;
}
#endif

static esp_err_t m_mount_x(vfs_config_t *p) {
    FUNC_ENTRYD(TAG);
    int8_t j = 0, ret = 0;
    bool (*mounted)(void) = 0;
    int (*mount)(void) = 0, msg_mounted = 0, msg_mount_failed = 0, msg_write_failed = 0;
    void (*umount)(void) = 0;
    if(!p || p->part_type >= VFS_PART_MAX || !p->mount_point) return ESP_FAIL;

    // Index of the partition within vfs_ctx.parts (for routing decisions)
    int part_idx = (int)(p - vfs_ctx.parts);
    if (part_idx < 0 || part_idx >= VFS_MAX_PARTS) return ESP_FAIL;
#ifdef CONFIG_USE_FATFS
    if(p->part_type == VFS_PART_FATFS) {
        mounted = fatfs_is_mounted;
        mount = fatfs_mount;
        umount = fatfs_umount;
        msg_mounted = VFS_EVENT_FAT_PARTITION_MOUNTED;
        msg_mount_failed = VFS_EVENT_FAT_PARTITION_MOUNT_FAILED;
        msg_write_failed = VFS_EVENT_FAT_PARTITION_WRITE_FAILED;
    }
    else
#endif
#ifdef CONFIG_USE_LITTLEFS
    if(p->part_type == VFS_PART_LITTLEFS) {
        mounted = littlefs_is_mounted;
        mount = littlefs_mount;
        umount = littlefs_umount;
        msg_mounted = VFS_EVENT_LITTEFS_PARTITION_MOUNTED;
        msg_mount_failed = VFS_EVENT_LITTLEFS_PARTITION_MOUNT_FAILED;
        msg_write_failed = VFS_EVENT_LITTLEFS_PARTITION_WRITE_FAILED;
    }
    else
#endif
#ifdef CONFIG_USE_SPIFFS
    if(p->part_type == VFS_PART_SPIFFS) {
        mounted = spiffs_is_mounted;
        mount = spiffs_mount;
        umount = spiffs_umount;
        msg_mounted = VFS_EVENT_SPIFFS_PARTITION_MOUNTED;
        msg_mount_failed = VFS_EVENT_SPIFFS_PARTITION_MOUNT_FAILED;
        msg_write_failed = VFS_EVENT_SPIFFS_PARTITION_WRITE_FAILED;
    }
    else
#endif
#ifdef CONFIG_USE_SD_CARD
    if(p->part_type == VFS_PART_SDCARD) {
        mounted = sdcard_is_mounted;
        mount = sdcard_mount;
        umount = sdcard_umount;
        msg_mounted = VFS_EVENT_SDCARD_MOUNTED;
        msg_mount_failed = VFS_EVENT_SDCARD_MOUNT_FAILED;
        msg_write_failed = VFS_EVENT_SDCARD_WRITE_FAILED;
    }
    else
#endif
    {
        FUNC_ENTRY_ARGS(TAG, " Unknown partition type");
        return ESP_FAIL;
    }

    if(!mounted()) {
        if(mount() == ESP_OK) {
            ret = 1;
            vTaskDelay(pdMS_TO_TICKS(5)); // Reduced from 10ms delay_ms to 5ms vTaskDelay
            goto test_write;
        }
        else {
            FUNC_ENTRY_ARGS(TAG, " Failed to mount part");
            ret = p->part_type == VFS_PART_FATFS ? ret : -1;
        }
    }
    else {
        test_write:
        if(try_open_write(".txt", p->mount_point, 0, 0)) {
            FUNC_ENTRY_ARGS(TAG, " Failed to open file");
            ret = -2;
        }
    }
    if(ret == -2) {
        p->write_attempts++;

        // Only trigger reselection and events if this failing partition is the active log target
        bool is_current_log_part = (vfs_ctx.gps_log_part == part_idx);

        if (is_current_log_part) {
            // Attempt to find alternative log partition if current one failed to write
            vfs_select_part(0);

            // If still the same or no alternative, notify failure
            if (vfs_ctx.gps_log_part == part_idx) {
                esp_event_post(VFS_EVENT, msg_write_failed, NULL, 0, pdMS_TO_TICKS(100));
            }
        }

        // Backoff/unmount after several failed attempts regardless of selection
        if(p->write_attempts > 3) {
            umount();
            goto umnt;
        }
    }
    else if(ret != 0) {
        umnt:
        if(p->write_attempts > 0) {
            p->write_attempts = 0;
        }
        p->is_mounted = ret > 0 ? 1 : 0;

        // Only trigger partition selection when this partition is relevant (current log part)
        if(ret != -1 && vfs_ctx.gps_log_part == part_idx) {
            vfs_select_part(0);
        }

        if(ret == 1) {
            esp_event_post(VFS_EVENT, msg_mounted, NULL, 0, pdMS_TO_TICKS(100));
        }
        else if(ret == -1) {
            esp_event_post(VFS_EVENT, msg_mount_failed, NULL, 0, pdMS_TO_TICKS(100));
        }
        else if(ret == -2 && vfs_ctx.gps_log_part == part_idx) {
            // Only post write_failed if this is the active log partition
            esp_event_post(VFS_EVENT, msg_write_failed, NULL, 0, pdMS_TO_TICKS(100));
        }
    }
    return ret >= 0 ? ESP_OK : ESP_FAIL;
}

/* Helper: Mount all configured partitions. Returns true if any mount succeeded. */
static bool vfs_mount_all_parts(bool yield_on_each) {
    uint8_t i = 0;
    bool any_success = false;
    
    while (i < VFS_MAX_PARTS) {
        switch (vfs_ctx.parts[i].part_type) {
            case VFS_PART_SDCARD:
            case VFS_PART_FATFS:
            case VFS_PART_LITTLEFS:
                if (m_mount_x(&vfs_ctx.parts[i]) == ESP_OK) {
                    any_success = true;
                }
                break;
            default:
                break;
        }
        ++i;
        if (yield_on_each) taskYIELD();
    }
    return any_success;
}

static void my_mount_cb(void* arg) {
    /* Timer context must stay short; enqueue mount-check work */
    if (m_app_ctx.app_mode == APP_MODE_CHARGE) {
        if (sd_timer && esp_timer_is_active(sd_timer)) {
            esp_timer_stop(sd_timer);
        }
        return;
    }
    // Non-blocking enqueue - ignore if queue full
    vfs_post_work(VFS_WORK_MOUNT_CHECK, NULL);
}

static void vfs_mount_worker(void *arg) {
    FUNC_ENTRYD(TAG);
    mount_task_is_running = true;
    vfs_work_item_t item;

    while (mount_task_is_running) {
        if (!vfs_work_queue) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Wait for next work item
        if (xQueueReceive(vfs_work_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (vfs_shutdown_in_progress) {
            break;
        }

        /* Take any updated argument that may have been coalesced by vfs_post_work().
         * Coalescing policy: latest arg wins. */
        portENTER_CRITICAL(&vfs_lock);
        void *latest_arg = NULL;
        if ((uint32_t)item.type < (sizeof(vfs_pending_args)/sizeof(vfs_pending_args[0]))) {
            latest_arg = vfs_pending_args[item.type];
            vfs_pending_args[item.type] = NULL;
        }
        portEXIT_CRITICAL(&vfs_lock);

        switch (item.type) {
            case VFS_WORK_MOUNT_CHECK: {
                // Short critical section protected by path buffer mutex
                if (!path_buffer_mutex || xSemaphoreTake(path_buffer_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
                    break;
                }
                bool space_update_needed = vfs_mount_all_parts(true);
                if (space_update_needed) {
                    uint8_t i = 0;
                    while (i < VFS_MAX_PARTS) {
                        if (vfs_ctx.parts[i].mount_point && vfs_ctx.parts[i].is_mounted && vfs_ctx.parts[i].free_bytes == 0) {
                            vfs_fs_space(vfs_ctx.parts[i].mount_point, vfs_ctx.parts[i].part_type,
                                         &vfs_ctx.parts[i].total_bytes, &vfs_ctx.parts[i].free_bytes,
                                         &vfs_ctx.parts[i].used_bytes);
                        }
                        ++i;
                        taskYIELD();
                    }
                }
                xSemaphoreGive(path_buffer_mutex);
                break;
            }
            case VFS_WORK_OPEN_FILES: {
#if defined(CONFIG_GPS_LOG_ENABLED)
                struct gps_context_s *gps = (struct gps_context_s*)latest_arg;
                void *ctx = gps ? gps : g_vfs_iface.ctx;
                if (!ctx) {
                    WLOG(TAG, "[vfs_worker] OPEN_FILES: no context provided, skipping");
                    break;
                }
                ILOG(TAG, "[vfs_worker] OPEN_FILES requested");
                {
                    vfs_work_if_t local_iface;
                    portENTER_CRITICAL(&vfs_lock);
                    local_iface = g_vfs_iface;
                    portEXIT_CRITICAL(&vfs_lock);
                    if (local_iface.process) local_iface.process(VFS_WORK_OPEN_FILES, ctx);
                }
#endif
                break;
            }
            case VFS_WORK_FLUSH_FILES: {
#if defined(CONFIG_GPS_LOG_ENABLED)
                struct gps_context_s *gps = (struct gps_context_s*)latest_arg;
                void *ctx = gps ? gps : g_vfs_iface.ctx;
                if (!ctx) {
                    WLOG(TAG, "[vfs_worker] FLUSH_FILES: no context provided, skipping");
                    break;
                }
                DLOG(TAG, "[vfs_worker] FLUSH_FILES requested");
                {
                    vfs_work_if_t local_iface;
                    portENTER_CRITICAL(&vfs_lock);
                    local_iface = g_vfs_iface;
                    portEXIT_CRITICAL(&vfs_lock);
                    if (local_iface.process) local_iface.process(VFS_WORK_FLUSH_FILES, ctx);
                }
#endif
                break;
            }
            case VFS_WORK_CLOSE_FILES: {
#if defined(CONFIG_GPS_LOG_ENABLED)
                struct gps_context_s *gps = (struct gps_context_s*)latest_arg;
                void *ctx = gps ? gps : g_vfs_iface.ctx;
                if (!ctx) {
                    WLOG(TAG, "[vfs_worker] CLOSE_FILES: no context provided, skipping");
                    break;
                }
                ILOG(TAG, "[vfs_worker] CLOSE_FILES requested");
                {
                    vfs_work_if_t local_iface;
                    portENTER_CRITICAL(&vfs_lock);
                    local_iface = g_vfs_iface;
                    portEXIT_CRITICAL(&vfs_lock);
                    if (local_iface.process) local_iface.process(VFS_WORK_CLOSE_FILES, ctx);
                }
#endif
                break;
            }
            case VFS_WORK_PARTITION_CHANGED: {
#if defined(CONFIG_GPS_LOG_ENABLED)
                struct gps_context_s *gps = (struct gps_context_s*)latest_arg;
                void *ctx = gps ? gps : g_vfs_iface.ctx;
                if (!ctx) {
                    WLOG(TAG, "[vfs_worker] PARTITION_CHANGED: no context provided, skipping");
                    break;
                }
                ILOG(TAG, "[vfs_worker] PARTITION_CHANGED requested");
                {
                    vfs_work_if_t local_iface;
                    portENTER_CRITICAL(&vfs_lock);
                    local_iface = g_vfs_iface;
                    portEXIT_CRITICAL(&vfs_lock);
                    if (local_iface.process) local_iface.process(VFS_WORK_PARTITION_CHANGED, ctx);
                }
#endif
                break;
            }
            case VFS_WORK_SAVE_SESSION: {
#if defined(CONFIG_GPS_LOG_ENABLED)
                struct gps_context_s *gps = (struct gps_context_s*)latest_arg;
                void *ctx = gps ? gps : g_vfs_iface.ctx;
                if (!ctx) {
                    WLOG(TAG, "[vfs_worker] SAVE_SESSION: no context provided, skipping");
                    break;
                }
                ILOG(TAG, "[vfs_worker] SAVE_SESSION requested");
                {
                    vfs_work_if_t local_iface;
                    portENTER_CRITICAL(&vfs_lock);
                    local_iface = g_vfs_iface;
                    portEXIT_CRITICAL(&vfs_lock);
                    if (local_iface.process) local_iface.process(VFS_WORK_SAVE_SESSION, ctx);
                }
#endif
                break;
            }
            default:
                break;
        }

        // Clear pending flag for this work item now that it's handled (allow requeue)
        portENTER_CRITICAL(&vfs_lock);
        vfs_pending_work_mask &= ~VFS_WORK_BIT(item.type);
        portEXIT_CRITICAL(&vfs_lock);
    }

    /* Allow task delete from deinit without double-free */
    FUNC_ENTRY_ARGSD(TAG, "Mount worker task exiting");
    mount_task_handle = NULL;
    vTaskDelete(NULL);
}

const vfs_config_t * vfs_get_part(const char * mount_point) {
    FUNC_ENTRY(TAG);
    uint8_t i = 0;
    while(i<VFS_MAX_PARTS) {
        if(vfs_ctx.parts[i].mount_point && strstr(mount_point, vfs_ctx.parts[i].mount_point) == mount_point) return &vfs_ctx.parts[i];
        ++i;
    }
    return 0;
}

int vfs_get_part_index(const char * mount_point) {
    FUNC_ENTRY(TAG);
    uint8_t i = 0;
    while(i<VFS_MAX_PARTS) {
        if(vfs_ctx.parts[i].mount_point && strstr(mount_point, vfs_ctx.parts[i].mount_point) == mount_point) return i;
        ++i;
    }
    return -1;
}

int vfs_select_part(uint8_t log_part_loked) {
    FUNC_ENTRY(TAG);
    uint8_t i = 0, j = 0;
    uint64_t free_size = 0;
    struct stat sb = {0};
    int statok;
    char filepath[VFS_FILE_PATH_MAX] = {0};
    strbf_t pathbuf;
    strbf_inits(&pathbuf, &filepath[0], VFS_FILE_PATH_MAX);
    if(vfs_ctx.config_part != VFS_PART_MAX && !vfs_ctx.parts[vfs_ctx.config_part].is_mounted) {
        FUNC_ENTRY_ARGS(TAG, " config part is not available, remove from registry.");
        vfs_ctx.config_part = VFS_PART_MAX;
        ++j;
    }
    if(vfs_ctx.gps_log_part != VFS_PART_MAX && !vfs_ctx.parts[vfs_ctx.gps_log_part].is_mounted) {
        FUNC_ENTRY_ARGS(TAG, " gps log part is not available, remove from registry.");
        vfs_ctx.gps_log_part = VFS_PART_MAX;
        ++j;
    }
    while(i<VFS_MAX_PARTS) {
        if(vfs_ctx.parts[i].part_type == VFS_PART_MAX || !vfs_ctx.parts[i].mount_point) {
            goto next;
        }
        if(vfs_ctx.parts[i].is_mounted) {
            vfs_fs_space(vfs_ctx.parts[i].mount_point, vfs_ctx.parts[i].part_type, &vfs_ctx.parts[i].total_bytes, &vfs_ctx.parts[i].free_bytes, &vfs_ctx.parts[i].used_bytes);
            FUNC_ENTRY_ARGS(TAG, " part: %" PRIu8 ", mountpoint: %s", i, vfs_ctx.parts[i].mount_point);
            if(vfs_ctx.config_part == VFS_PART_MAX) {
                FUNC_ENTRY_ARGS(TAG, " Config part: %" PRIu8 ", mountpoint: %s", i, vfs_ctx.parts[i].mount_point);
                vfs_ctx.config_part = i;
            }
            // if((vfs_ctx.gps_log_part == VFS_PART_MAX || free_size < vfs_ctx.parts[i].free_bytes)) {
                // free_size = vfs_ctx.parts[i].free_bytes;
            if(vfs_ctx.gps_log_part == VFS_PART_MAX || (!log_part_loked && vfs_ctx.parts[vfs_ctx.gps_log_part].free_bytes < vfs_ctx.parts[i].free_bytes)) {
                // if(vfs_ctx.parts[i].free_bytes > 9000000) { // 10MB
                FUNC_ENTRY_ARGS(TAG, " GPS log part: %" PRIu8 ", mountpoint: %s", i, vfs_ctx.parts[i].mount_point);
                vfs_ctx.gps_log_part = i;
                esp_event_post(VFS_EVENT, VFS_EVENT_LOG_PARTITION_CHANGED, NULL, 0, pdMS_TO_TICKS(100));
            }
            //}
            // if(vfs_ctx.web_part == VFS_PART_MAX) {
            //     strbf_puts(&pathbuf, vfs_ctx.parts[i].mount_point);
            //     strbf_put_path(&pathbuf, "/www");
            //     strbf_finish(&pathbuf);
            //     statok = stat(pathbuf.start, &sb);
            //     ILOG(TAG, "[%s] Try web part: %" PRIu8 ", path: %s, statok: %d", __func__, i, pathbuf.start, statok);
            //     if (!statok && S_ISDIR(sb.st_mode)) {
            //         ILOG(TAG, "[%s] Web part: %" PRIu8 ", ", __func__, i);
            //         vfs_ctx.web_part = i;
            //     }
            //     strbf_shape(&pathbuf, 0);
            // }
        }
        next:
        ++i;
    }
    if(vfs_ctx.gps_log_part == VFS_PART_MAX && j) {
        FUNC_ENTRY_ARGS(TAG, " No GPS log part available anymore, sry...");
        esp_event_post(VFS_EVENT, VFS_EVENT_LOG_PARTITION_CHANGED, NULL, 0, pdMS_TO_TICKS(100));
    }
    //assert(
        // vfs_ctx.web_part != VFS_PART_MAX && 
    //    vfs_ctx.gps_log_part != VFS_PART_MAX && vfs_ctx.config_part != VFS_PART_MAX);
    return ESP_OK;
}

int vfs_init(void) {
    FUNC_ENTRY(TAG);
    if(vfs_ctx.vfs_initialized) return ESP_OK;
    int8_t i = 0, j, k = 0;
#if defined(LOG_LOCAL_LEVEL)
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif
    
    /* Clear shutdown flag at start of initialization */
    vfs_shutdown_in_progress = false;
    
    // Initialize path buffer semaphore for heap optimization
    // Using binary semaphore instead of mutex to avoid FreeRTOS priority inheritance
    // assertion failures when using timeouts (tasks.c:5261)
    if (!path_buffer_mutex) {
        path_buffer_mutex = xSemaphoreCreateBinary();
        if (!path_buffer_mutex) {
            ELOG(TAG, "[%s] Failed to create path buffer semaphore", __func__);
        } else {
            // Binary semaphore starts in "taken" state, give it to make it available
            xSemaphoreGive(path_buffer_mutex);
        }
    }

    // Mount worker synchronization
    if (!mount_sem) {
        mount_sem = xSemaphoreCreateBinary();
        if (!mount_sem) {
            ELOG(TAG, "[%s] Failed to create mount semaphore", __func__);
        }
    }
    
    while(i < VFS_PART_MAX) {
        j = 0, k = 0;
        while(j < VFS_MAX_PARTS) {
            if(!vfs_ctx.parts[j].mount_point) break;
            ++j;
        }
        switch(i) {
#ifdef CONFIG_USE_SD_CARD
            case VFS_PART_SDCARD:
                if(!sdcard_init()){
                    vfs_ctx.parts[j].mount_point = CONFIG_SD_MOUNT_POINT;
                    vfs_ctx.parts[j].part_type = VFS_PART_SDCARD;
                    ++k;
                }
                break;
#endif
#ifdef CONFIG_USE_FATFS
            case VFS_PART_FATFS:
                if(!fatfs_init()) {
                    vfs_ctx.parts[j].mount_point = CONFIG_FATFS_MOUNT_POINT;
                    vfs_ctx.parts[j].part_type = VFS_PART_FATFS;
                    ++k;
                }
                break;
#endif
#ifdef CONFIG_USE_SPIFFS
            case VFS_PART_SPIFFS:
                if(!spiffs_init()){
                    vfs_ctx.parts[j].mount_point = CONFIG_SPIFFS_MOUNT_POINT;
                    vfs_ctx.parts[j].part_type = VFS_PART_SPIFFS;
                    ++k;
                }
                break;
#endif
#ifdef CONFIG_USE_LITTLEFS
            case VFS_PART_LITTLEFS:
                if(!littlefs_init()) {
                    vfs_ctx.parts[j].mount_point = CONFIG_LITTLEFS_MOUNT_POINT;
                    vfs_ctx.parts[j].part_type = VFS_PART_LITTLEFS;
                    ++k;
                }
                break;
#endif
            default:
                break;
        }
        ++i;
    }
    if (k && mount_sem) {
        // Do initial mount synchronously to initialize vfs_ctx before returning
        vfs_mount_all_parts(false);
        vfs_select_part(0);  // Initialize partition selection with initial mount results

        // Create work queue and start worker task for mount / file ops
        if (!vfs_work_queue) {
            vfs_work_queue = xQueueCreate(8, sizeof(vfs_work_item_t));
            if (!vfs_work_queue) {
                ELOG(TAG, "[%s] Failed to create vfs work queue", __func__);
            }
        }

        if (!mount_task_handle) {
            if (xTaskCreate(vfs_mount_worker, "vfs_worker", 4096, NULL, tskIDLE_PRIORITY + 1, &mount_task_handle) != pdPASS) {
                ELOG(TAG, "[%s] Failed to create mount worker task", __func__);
            }
        }

        // Start periodic timer that enqueues mount-check work
        if (!sd_timer) {
            const esp_timer_create_args_t sd_timer_args = {
                .callback = &my_mount_cb,
                .name = "sd_mount",
                .arg = 0
            };
            if (!esp_timer_create(&sd_timer_args, &sd_timer)) {
                esp_timer_start_periodic(sd_timer, SEC_TO_US(2)); // 2s
            } else {
                ELOG(TAG, "[%s] Failed to create vfs timer", __func__);
            }
        }

        ++do_space_print;
    }
    // const esp_timer_create_args_t timer_args = {
    //     .callback = vfs_update_space,
    //     .name = "fs_size",
    //     .arg = 0
    // };
    // if(!esp_timer_create(&timer_args, &fs_size_timer))
    //     esp_timer_start_periodic(fs_size_timer, SEC_TO_US(60)); // 60s
    // else {
    //     ELOG(TAG, "[%s] Failed to create fs size timer", __func__);
    // }
    vfs_ctx.vfs_initialized = 1;
    return ESP_OK;
}

void vfs_pause_monitoring(bool pause) {
    if (pause) {
        // Stop the periodic timer when entering charge mode or pausing
        if (sd_timer && esp_timer_is_active(sd_timer)) {
            esp_timer_stop(sd_timer);
            FUNC_ENTRY_ARGS(TAG, "SD mount timer paused");
        }
    } else {
        // Resume the periodic timer when exiting charge mode
        if (sd_timer && !esp_timer_is_active(sd_timer)) {
            esp_timer_start_periodic(sd_timer, SEC_TO_US(2));
            FUNC_ENTRY_ARGS(TAG, "SD mount timer resumed");
        }
    }
}

/**
 * @brief Suspend VFS mount monitoring for maintenance operations (format, etc.)
 * @return true if successfully suspended, false if VFS not initialized
 * 
 * Stops the timer and acquires the path_buffer_mutex to ensure worker is idle.
 * Must be paired with vfs_resume_from_maintenance().
 */
bool vfs_suspend_for_maintenance(void) {
    if (!vfs_ctx.vfs_initialized) {
        return false;
    }
    
    // Stop timer to prevent new worker wakeups
    if (sd_timer && esp_timer_is_active(sd_timer)) {
        esp_timer_stop(sd_timer);
    }
    
    // Acquire mutex to wait for worker to finish current iteration
    if (path_buffer_mutex && xSemaphoreTake(path_buffer_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        FUNC_ENTRY_ARGS(TAG, "VFS suspended for maintenance");
        return true;
    }
    
    ELOG(TAG, "[%s] Failed to acquire mutex for maintenance", __func__);
    return false;
}

/**
 * @brief Resume VFS mount monitoring after maintenance operations
 * 
 * Releases the path_buffer_mutex and restarts the periodic timer.
 */
void vfs_resume_from_maintenance(void) {
    // Release mutex
    if (path_buffer_mutex) {
        xSemaphoreGive(path_buffer_mutex);
    }
    
    // Restart timer
    if (vfs_ctx.vfs_initialized && sd_timer && !esp_timer_is_active(sd_timer)) {
        esp_timer_start_periodic(sd_timer, SEC_TO_US(2));
        FUNC_ENTRY_ARGS(TAG, "VFS resumed after maintenance");
    }
}

int vfs_deinit(void) {
    if (!vfs_ctx.vfs_initialized) return ESP_OK;

    /* Step 1: Acquire mutex to prevent timer callbacks from running */
    /* This blocks until any active callback completes */
    bool mutex_acquired = false;
    if (path_buffer_mutex) {
        mutex_acquired = xSemaphoreTake(path_buffer_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
        if (!mutex_acquired) {
            ELOG(TAG, "[%s] Failed to acquire mutex for shutdown", __func__);
        }
    }

    /* Step 2: Set shutdown flag to prevent new callbacks from running */
    vfs_shutdown_in_progress = true;

    /* Step 3: Mark VFS as not initialized */
    vfs_ctx.vfs_initialized = 0;

    /* Step 4: Stop and delete timers while holding mutex */
    /* Timer callbacks check shutdown flag after acquiring mutex, so they'll exit */
    if (sd_timer) {
        if (esp_timer_is_active(sd_timer)) {
            esp_timer_stop(sd_timer);
        }
        esp_timer_delete(sd_timer);
        sd_timer = NULL;
    }

    if (fs_size_timer) {
        if (esp_timer_is_active(fs_size_timer)) {
            esp_timer_stop(fs_size_timer);
        }
        esp_timer_delete(fs_size_timer);
        fs_size_timer = NULL;
    }

    /* Stop mount worker */
    mount_task_is_running = false;
    if (vfs_work_queue) {
        /* Enqueue a sentinel to wake the worker so it can exit cleanly */
        vfs_work_item_t stop_item = { .type = VFS_WORK_MOUNT_CHECK, .arg = NULL };
        xQueueSend(vfs_work_queue, &stop_item, pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    if (mount_task_handle) {
        vTaskDelete(mount_task_handle);
        mount_task_handle = NULL;
    }
    if (vfs_work_queue) {
        vQueueDelete(vfs_work_queue);
        vfs_work_queue = NULL;
    }

    /* Step 4: Small delay to ensure timer deletion completes */
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Step 5: Now safe to unmount filesystems - no callbacks can access them */
#ifdef CONFIG_USE_SD_CARD
    if(sdcard_is_mounted()) {
        sdcard_umount();
    }
    sdcard_uninit();
#endif
#ifdef CONFIG_USE_FATFS
    if(fatfs_is_mounted()) {
        fatfs_umount();
    }
    fatfs_uninit();
#endif
#ifdef CONFIG_USE_SPIFFS
    if(spiffs_is_mounted()) {
        spiffs_umount();
    }
    spiffs_uninit();
#endif
#ifdef CONFIG_USE_LITTLEFS
    if(littlefs_is_mounted()) {
        littlefs_umount();
    }
    littlefs_uninit();
#endif

    /* Step 6: Release mutex and delete it last */
    if (mutex_acquired && path_buffer_mutex) {
        xSemaphoreGive(path_buffer_mutex);
    }

    if (path_buffer_mutex) {
        vSemaphoreDelete(path_buffer_mutex);
        path_buffer_mutex = NULL;
    }

    if (mount_sem) {
        vSemaphoreDelete(mount_sem);
        mount_sem = NULL;
    }

    /* Clear shutdown flag to allow re-initialization */
    vfs_shutdown_in_progress = false;

    /* Clear any pending work mask to avoid stale coalescing bits */
    portENTER_CRITICAL(&vfs_lock);
    vfs_pending_work_mask = 0;
    /* also clear any pending args */
    for (size_t _i = 0; _i < sizeof(vfs_pending_args)/sizeof(vfs_pending_args[0]); ++_i) vfs_pending_args[_i] = NULL;
    portEXIT_CRITICAL(&vfs_lock);

    return ESP_OK;
}

// Post VFS work to the worker queue (non-blocking). Returns ESP_OK on success.
esp_err_t vfs_post_work(vfs_work_type_t type, void *arg) {
    if (!vfs_work_queue) return ESP_FAIL;

    uint32_t bit = VFS_WORK_BIT(type);
    // Avoid duplicate work: if bit already set, coalesce and return success
    portENTER_CRITICAL(&vfs_lock);
    if (vfs_pending_work_mask & bit) {
        /* Already pending: update the stored arg so the worker will handle the latest
         * argument when it processes this work item (latest-wins coalescing). */
        vfs_pending_args[type] = arg;
        portEXIT_CRITICAL(&vfs_lock);
        FUNC_ENTRY_ARGSD(TAG, " Work %d already pending, updated arg", type);
        return ESP_OK; // consider as success (already queued or processing)
    }
    /* Not pending yet: mark pending and store arg */
    vfs_pending_work_mask |= bit;
    vfs_pending_args[type] = arg;
    portEXIT_CRITICAL(&vfs_lock);

    vfs_work_item_t item = { .type = type, .arg = arg };
    if (xQueueSend(vfs_work_queue, &item, 0) == pdTRUE) return ESP_OK;
    if (xQueueSendToBack(vfs_work_queue, &item, pdMS_TO_TICKS(10)) == pdTRUE) return ESP_OK;

    // failed to enqueue, clear pending bit and arg
    portENTER_CRITICAL(&vfs_lock);
    vfs_pending_work_mask &= ~bit;
    vfs_pending_args[type] = NULL;
    portEXIT_CRITICAL(&vfs_lock);

    WLOG(TAG, "[%s] vfs work queue full", __func__);
    return ESP_FAIL;
}

    


static const char * const down_str[] = {"b", "Kb", "Mb", "Gb"};
#if (C_LOG_LEVEL <= LOG_WARN_NUM)
static const char * const strs[] = {
    "Failed to get partition information"
};
#endif

static int vfs_mp_str(uint64_t b, char *buf) {
    if(!buf) return ESP_FAIL;
    uint8_t down = 0;
    char * p = buf;
    double bytes = (double)b;
    while(down < 3 && bytes > 10000) {
        bytes /= 1000;
        ++down;
    }
    size_t l = f2_to_char(bytes, p);
    p += l;
    *p = ' ';
    ++p;
    l = strlen(down_str[down]);
    memcpy(p, down_str[down], l);
    p += l;
    *p = 0;
    return p-buf;
}

int vfs_print_space(const char *mp, uint64_t total_bytes, uint64_t free_bytes, uint64_t used_bytes) {
    char buf[3][32] = {0};
    vfs_mp_str(total_bytes, buf[0]);
    vfs_mp_str(free_bytes, buf[1]);
    vfs_mp_str(used_bytes, buf[2]);
    WLOG(TAG, "Mountpoint: %s Total: %s, Free: %s, Used: %s", mp, buf[0], buf[1], buf[2]);
    // do_space_print = 0;
    return ESP_OK;
}

void vfs_update_space(void*arg) {
    FUNC_ENTRY(TAG);
    uint8_t i = 0;
    while(i<VFS_MAX_PARTS) {
        if(vfs_ctx.parts[i].mount_point && vfs_ctx.parts[i].is_mounted) {
            vfs_fs_space(vfs_ctx.parts[i].mount_point, vfs_ctx.parts[i].part_type, &vfs_ctx.parts[i].total_bytes, &vfs_ctx.parts[i].free_bytes, &vfs_ctx.parts[i].used_bytes);
        }
        ++i;
    }
}

int vfs_space_str(char*arg, size_t arglen) {
    FUNC_ENTRYD(TAG);
    uint8_t i = vfs_ctx.gps_log_part;
    size_t len = 0;
    const char *p = arg;
    if(i >= VFS_MAX_PARTS) { 
        goto end;
    }
    // while(i<VFS_MAX_PARTS) {
        if(vfs_ctx.parts[i].mount_point && vfs_ctx.parts[i].is_mounted) {
            // if(i > 0) {
            //     *arg = ',', ++arg;
            // }
            len = strlen(vfs_ctx.parts[i].mount_point)-1;
            memcpy(arg, vfs_ctx.parts[i].mount_point+1, len >= arglen ? arglen - 1 : len);
            arg += len, *arg = ' ', ++arg;
            arg += vfs_mp_str(vfs_ctx.parts[i].free_bytes, arg);
        }
        else {
            memcpy(arg, "- nolog -", 9), arg += 9;
        }
    //    ++i;
    // }
    end:
    *arg = 0;
    return (arg - p);
}

int vfs_fs_space(const char * mp, uint8_t type, uint64_t *total_bytes, uint64_t *free_bytes, uint64_t *used_bytes) {
    FUNC_ENTRY(TAG);
    // assert(total_bytes && used_bytes && free_bytes);
    esp_err_t ret;
#if defined(CONFIG_USE_SD_CARD) || defined(CONFIG_USE_FATFS)
    ret = esp_vfs_fat_info(mp, total_bytes, free_bytes);
#elif defined(CONFIG_USE_SPIFFS)
    ret = esp_spiffs_info(mp, total_bytes, used_bytes);
#elif defined(CONFIG_USE_LITTLEFS)
    ret = esp_littlefs_info(mp, total_bytes, used_bytes);
#endif
    if (ret != ESP_OK) {
        WLOG(TAG, "%s (%s)", strs[0], esp_err_to_name(ret));
        return ret;
    }
    if(ret) return ret;
#if defined(CONFIG_USE_SD_CARD) || defined(CONFIG_USE_FATFS)
    *used_bytes = *total_bytes - *free_bytes;
#else
    *free_bytes = *total_bytes - *used_bytes;
#endif
    if (do_space_print)
        vfs_print_space(mp, *total_bytes, *free_bytes, *used_bytes);
    return ESP_OK;
}

int s_xfile_exists(const char *filename) {
    int rc = (!access(filename, R_OK)) ? 1 : 0;
    return rc;
}

off_t s_xstat_file_size(int f) {
    off_t flength = 0;
    if (f >= 0) {
        struct stat st = {0};
        if (!fstat(f, &st) && S_ISREG(st.st_mode)) {
            flength = st.st_size;
        }
    }
    return flength;
}

int get_file_path_width_base(char *topath, size_t pathlen, const char *name, const char *base) {
    FUNC_ENTRY_ARGSD(TAG, " %s %s", base ? base : "", name);
    if(!topath) return 0;
    if(pathlen == 0) return 0;
    char *p = topath;
    const char *mp = base;
    size_t len = base ? strlen(base) : 0;
    while (mp && (*mp == '/' || *mp == ' ')) {
        ++mp;
        --len;
    }
    *p = '/';
    ++p;
    if (mp && len) {
        memcpy(p, mp, len >= pathlen ? pathlen - 1 : len);
        pathlen -= len >= pathlen ? pathlen - 1 : len;
        p += len >= pathlen ? pathlen - 1 : len;
        while (*p == '/')
            --p;
        *p = '/';
        ++p;
    }
    if (name && *name) {
        mp = name;
        len = strlen(name);
        while (mp && (*mp == '/' || *mp == ' ')) {
            ++mp;
            --len;
        }
        if(mp){
            memcpy(p, mp, len >= pathlen ? pathlen - 1 : len);
            p += len >= pathlen ? pathlen - 1 : len;
        }
    }
    *p = 0;
    if (*(p - 1) == '/')
        *(p - 1) = 0;
    return (p - topath);
}

FILE *s_open_file(const char *name, const char *base, const char *mode) {
    FUNC_ENTRY_ARGSD(TAG, "%s %s", base ? base : "", name);
    if (name == 0 || name[0] == 0) return 0;
    if (mode == 0) mode = "rb";
    
    /* Quick check: don't even try if shutdown is in progress */
    if (vfs_shutdown_in_progress) {
        DLOG(TAG, "[%s] VFS shutdown in progress, cannot open file", __func__);
        return 0;
    }
    
    // Use shared buffer to reduce stack allocation
    if (path_buffer_mutex && xSemaphoreTake(path_buffer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        /* Recheck shutdown flag after acquiring mutex */
        if (vfs_shutdown_in_progress) {
            xSemaphoreGive(path_buffer_mutex);
            DLOG(TAG, "[%s] VFS shutdown started, cannot open file", __func__);
            return 0;
        }
        
        const char *p;
        get_file_path_width_base(shared_path_buffer, PATH_MAX_CHAR_SIZE, name, base);
        p = (*shared_path_buffer ? shared_path_buffer : name);
        FILE *f = fopen(p, mode);
        xSemaphoreGive(path_buffer_mutex);
        
        if (f == NULL) {
            WLOG(TAG, "[%s] open '%s' failed '%s'.", __FUNCTION__, p, strerror(errno));
            return 0;
        }
        return f;
    } else {
        // Fallback to stack buffer if mutex unavailable (during shutdown or not initialized)
        /* Don't try to open files if VFS is shutting down */
        if (vfs_shutdown_in_progress) {
            DLOG(TAG, "[%s] VFS shutdown in progress, cannot open file", __func__);
            return 0;
        }
        
        char path[PATH_MAX_CHAR_SIZE] = {0};
        const char *p;
        get_file_path_width_base(&(path[0]), PATH_MAX_CHAR_SIZE, name, base);
        p = (*path ? path : name);
        FILE *f = fopen(p, mode);
        if (f == NULL) {
            WLOG(TAG, "[%s] open '%s' failed '%s'.", __FUNCTION__, p, strerror(errno));
            return 0;
        }
        return f;
    }
}

int s_open(const char *name, const char *base, const char *mode) {
    FUNC_ENTRY_ARGS(TAG, " %s %s", base ? base : "", name);
    if (name == 0 || name[0] == 0)
        return -1;
    if (mode == 0)
        mode = "rb";
    const char *md = mode;
    int m = O_RDONLY;
    if (*(md) == 'r') {
        ++md;
        m = md && *md && (*md == '+' || *md == 'w') ? O_RDWR : O_RDONLY;
    } else if (*(md) == 'a') {
        ++md;
        m = md && *md && (*md == '+') ? O_WRONLY | O_APPEND | O_CREAT : O_WRONLY | O_APPEND;
        
    } else if (*(md) == 'w') {
        ++md;
        m = (md && *md && (*md == '+') ? O_WRONLY | O_CREAT : O_WRONLY)|O_TRUNC;
    }
    char path[PATH_MAX_CHAR_SIZE] = {0};
    const char *p = path;
    get_file_path_width_base(&(path[0]), PATH_MAX_CHAR_SIZE, name, base);
    p = (*path ? path : name);
    int f = open(p, m);
    if (f < 0) {
        WLOG(TAG, "[%s] open '%s' failed: '%s'", __FUNCTION__, p, strerror(errno));
        return -1;
    }
    return f;
}

esp_err_t s_remove_file(const char *name, const char *base) {
    FUNC_ENTRY_ARGS(TAG, "%s %s", base ? base : "", name);
    if (name == 0 || name[0] == 0)
        return ESP_FAIL;
    char path[PATH_MAX_CHAR_SIZE] = {0};
    const char *p = path;
    get_file_path_width_base(&(path[0]), PATH_MAX_CHAR_SIZE, name, base);
    p = (*path ? path : name);
    if (unlink(p) < 0) {
        WLOG(TAG, "[%s] unlink '%s' failed: '%s'", __FUNCTION__, p, strerror(errno));
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t s_write_file(const char *name, const char *base, char *data) {
    FUNC_ENTRY_ARGS(TAG, "%s %s", base ? base : "", name);
    if (name == 0 || name[0] == 0)
        return ESP_FAIL;
    FILE *f = s_open_file(name, base, "w");
    if (f == NULL) {
        return ESP_FAIL;
    }
    fwrite(data, strlen(data), sizeof(data), f);
    fclose(f);
    return ESP_OK;
}

esp_err_t s_write(const char *name, const char *base, char *data, size_t len) {
    FUNC_ENTRY_ARGS(TAG, "%s %s", base ? base : "", name);
    if (name == 0 || name[0] == 0)
        return ESP_FAIL;
    int f = s_open(name, base, "w+");
    if (f == -1) {
        return ESP_FAIL;
    }
    int bytes = write(f, data, len ? len : strlen(data));
    if (bytes < 0) {
        WLOG(TAG, "Failed to write (%s) fd:%d", strerror(errno), f);
    }
    if (fsync(f)) {
        WLOG(TAG, "Failed to sync (%s) fd:%d", strerror(errno), f);
    }
    if (close(f)) {
        WLOG(TAG, "Failed to close (%s) fd:%d", strerror(errno), f);
    }
    return bytes;
}

char *s_read_from_file(const char *name, const char *base) {
    FUNC_ENTRY_ARGS(TAG, "%s %s", base ? base : "", name);   
    if (name == 0 || name[0] == 0)
        return 0;
    char *buffer = 0;
    int f = s_open(name, base, "rb");
    if (f >= 0) {
        off_t flength = s_xstat_file_size(f);
        buffer = malloc(flength + 1 * sizeof(char));
        int err = read(f, buffer, sizeof(char) * flength);
        if (err < 0) {
            WLOG(TAG, "Failed to read (%s) fd:%d", strerror(errno), f);
            free(buffer);
            buffer = 0;
        } else
            buffer[flength] = 0;
        if (close(f)) {
            WLOG(TAG, "Failed to close (%s)", strerror(errno));
        }
    }
    return buffer;
}

int s_rename_file_n(const char *old, const char *new, uint8_t rmifexists) {
    FUNC_ENTRY_ARGS(TAG, " %s %s", old, new);
    if (!old || !new)
        return -1;
    if (!s_xfile_exists(old))
        return -1;
    if (s_xfile_exists(new) && rmifexists) {
        if (unlink(new) < 0) {
            WLOG(TAG, "[%s] Failed to unlink (%s)", __FILE__, strerror(errno));
            return -1;
        }
    }
    if (rename(old, new) < 0) {
        WLOG(TAG, "[%s] Failed to rename [%s to %s], (%s)", __FILE__, old, new, strerror(errno));
        return -1;
    }
    return 0;
}

int s_rename_file(const char *old, const char *new, const char *base) {
    FUNC_ENTRY_ARGS(TAG, " %s %s %s", base ? base : "", old, new); 
    if (!old || !new)
        return -1;
    char path[PATH_MAX_CHAR_SIZE] = {0};
    const char *p = path;
    get_file_path_width_base(&(path[0]), PATH_MAX_CHAR_SIZE, old, base);
    if (!s_xfile_exists(old))
        return -1;
    char npath[PATH_MAX_CHAR_SIZE] = {0};
    const char *n = npath;
    get_file_path_width_base(&(npath[0]), PATH_MAX_CHAR_SIZE, new, base);
    return s_rename_file_n(p, n, 1);
}

#endif // CONFIG_VFS_ENABLED
