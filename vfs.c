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
ESP_EVENT_DEFINE_BASE(VFS_EVENT);

const char * const vfs_event_strings[] = { VFS_EVENT_LIST(STRINGIFY) };

static const char *TAG = "vfs";
static esp_timer_handle_t sd_timer = 0;
static esp_timer_handle_t fs_size_timer = 0;
vfs_t vfs_ctx = VFS_DEDAULTS();

static void sd_mount_cb(void* arg) {
    ILOG(TAG, "[%s]", __func__);
    if(!sdcard_is_mounted()) {
        if(sdcard_mount()==ESP_OK) {
            uint8_t i = 0;
            while(i<VFS_MAX_PARTS) {
                if(vfs_ctx.parts[i].part_type == VFS_PART_SDCARD) {
                    vfs_ctx.parts[i].is_mounted = 1;
                    break;
                }
                ++i;
            }
            esp_event_post(VFS_EVENT, VFS_EVENT_SDCARD_MOUNTED, NULL, 0, portMAX_DELAY);
        }
        else if (!esp_timer_is_active(sd_timer)) {
            esp_event_post(VFS_EVENT, VFS_EVENT_SDCARD_MOUNT_FAILED, NULL, 0, portMAX_DELAY);
            const esp_timer_create_args_t sd_timer_args = {
                .callback = &sd_mount_cb,
                .name = "sd_mount",
                .arg = 0
            };
            ESP_ERROR_CHECK(esp_timer_create(&sd_timer_args, &sd_timer));
            ESP_ERROR_CHECK(esp_timer_start_periodic(sd_timer, 1000000)); // 500ms
        }
    }
    else if (esp_timer_is_active(sd_timer)) {
            esp_timer_stop(sd_timer);
            esp_timer_delete(sd_timer);
    }
}
const vfs_config_t * vfs_get_part(const char * mount_point) {
    ILOG(TAG, "[%s]", __func__);
    uint8_t i = 0;
    while(i<VFS_MAX_PARTS) {
        if(vfs_ctx.parts[i].mount_point && strstr(mount_point, vfs_ctx.parts[i].mount_point) == mount_point) return &vfs_ctx.parts[i];
        ++i;
    }
    return 0;
}

int vfs_get_part_index(const char * mount_point) {
    ILOG(TAG, "[%s]", __func__);
    uint8_t i = 0;
    while(i<VFS_MAX_PARTS) {
        if(vfs_ctx.parts[i].mount_point && strstr(mount_point, vfs_ctx.parts[i].mount_point) == mount_point) return i;
        ++i;
    }
    return -1;
}

int vfs_select_part(void) {
    ILOG(TAG, "[%s]", __func__);
    uint8_t i = 0;
    uint64_t free_size = 0;
    struct stat sb = {0};
    int statok;
    char filepath[VFS_FILE_PATH_MAX] = {0};
    strbf_t pathbuf;
    strbf_inits(&pathbuf, &filepath[0], VFS_FILE_PATH_MAX);
    while(i<VFS_MAX_PARTS) {
        if(vfs_ctx.parts[i].part_type == VFS_PART_MAX || !vfs_ctx.parts[i].mount_point) {
            goto next;
        }
        if(vfs_ctx.parts[i].is_mounted) {
            vfs_fs_space(vfs_ctx.parts[i].mount_point, vfs_ctx.parts[i].part_type, &vfs_ctx.parts[i].total_bytes, &vfs_ctx.parts[i].free_bytes, &vfs_ctx.parts[i].used_bytes);
            ILOG(TAG, "[%s] part: %hhu, mountpoint: %s", __func__, i, vfs_ctx.parts[i].mount_point);
            if(vfs_ctx.config_part == VFS_PART_MAX) {
                ILOG(TAG, "[%s] Config part: %hhu, mountpoint: %s", __func__, i, vfs_ctx.parts[i].mount_point);
                vfs_ctx.config_part = i;
            }
            if((vfs_ctx.gps_log_part == VFS_PART_MAX || free_size < vfs_ctx.parts[i].free_bytes)) {
                free_size = vfs_ctx.parts[i].free_bytes;
                if(vfs_ctx.parts[i].free_bytes > 9000000) { // 10MB
                    ILOG(TAG, "[%s] GPS log part: %hhu", __func__, i);
                    vfs_ctx.gps_log_part = i;
                }
            }
            if(vfs_ctx.web_part == VFS_PART_MAX) {
                strbf_puts(&pathbuf, vfs_ctx.parts[i].mount_point);
                strbf_put_path(&pathbuf, "/www");
                strbf_finish(&pathbuf);
                statok = stat(pathbuf.start, &sb);
                ILOG(TAG, "[%s] Try web part: %hhu, path: %s, statok: %d", __func__, i, pathbuf.start, statok);
                if (!statok && S_ISDIR(sb.st_mode)) {
                    ILOG(TAG, "[%s] Web part: %hhu, ", __func__, i);
                    vfs_ctx.web_part = i;
                }
                strbf_shape(&pathbuf, 0);
            }
        }
        next:
        ++i;
    }
    assert(vfs_ctx.web_part != VFS_PART_MAX && vfs_ctx.gps_log_part != VFS_PART_MAX && vfs_ctx.config_part != VFS_PART_MAX);
    return ESP_OK;
}

int vfs_init(void) {
    ILOG(TAG, "[%s]", __func__);
    uint8_t i = 0, j;
    while(i < VFS_PART_MAX) {
        j = 0;
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
                    sd_mount_cb(0);
                }
                break;
#endif
#ifdef CONFIG_USE_FATFS
            case VFS_PART_FATTFS:
                if(!fatfs_init()) {
                    vfs_ctx.parts[j].mount_point = CONFIG_FATFS_MOUNT_POINT;
                    vfs_ctx.parts[j].part_type = VFS_PART_FATTFS;
                    vfs_ctx.parts[j].is_mounted = 1;
                }
                break;
#endif
#ifdef CONFIG_USE_SPIFFS
            case VFS_PART_SPIFFS:
                if(spiffs_init()){
                    vfs_ctx.parts[j].mount_point = CONFIG_SPIFFS_MOUNT_POINT;
                    vfs_ctx.parts[j].part_type = VFS_PART_SPIFFS;
                    vfs_ctx.parts[j].is_mounted = 1;
                }
                break;
#endif
#ifdef CONFIG_USE_LITTLEFS
            case VFS_PART_LITTLEFS:
                if(!littlefs_init()) {
                    vfs_ctx.parts[j].mount_point = CONFIG_LITTLEFS_MOUNT_POINT;
                    vfs_ctx.parts[j].part_type = VFS_PART_LITTLEFS;
                    vfs_ctx.parts[j].is_mounted = 1;
                }
                break;
#endif
            default:
                break;
        }
        ++i;
    }
    const esp_timer_create_args_t timer_args = {
        .callback = vfs_update_space,
        .name = "fs_size",
        .arg = 0
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &fs_size_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(fs_size_timer, 60000000)); // 60s
    return ESP_OK;
}

int vfs_uninit(void) {
    if (esp_timer_is_active(fs_size_timer)) {
            esp_timer_stop(fs_size_timer);
            esp_timer_delete(fs_size_timer);
    }
    if (esp_timer_is_active(sd_timer)) {
            esp_timer_stop(sd_timer);
            esp_timer_delete(sd_timer);
    }
#ifdef CONFIG_USE_SD_CARD
    if(sdcard_is_mounted()) {
        sdcard_umount();
    }
    sdcard_uninit();
#endif
#ifdef CONFIG_USE_FATFS
    fatfs_uninit();
#endif
#ifdef CONFIG_USE_SPIFFS
    spiffs_uninit();
#endif
#ifdef CONFIG_USE_LITTLEFS
    littlefs_deinit();
#endif
    return ESP_OK;
}

static const char * const down_str[] = {"bytes", "Kb", "Mb", "Gb"};
static const char * const strs[] = {
    "Failed to get partition information"
};

int vfs_print_space(const char *mp, uint64_t total_bytes, uint64_t free_bytes, uint64_t used_bytes) {
    double bytes[3] = {total_bytes, free_bytes, used_bytes};
    uint8_t down[3] = {0, 0, 0};
    for(uint8_t i=0; i<3; ++i) {
        while(down[i] < 3 && bytes[i] > 10000) {
            bytes[i] /= 1000;
            ++down[i];
        }
    }
    ILOG(TAG, "[%s] Mountpoint: %s Total space: %.3lf %s, Free space: %.3lf %s, Used space: %.3lf %s", __func__, mp,
        bytes[0], down_str[down[0]], 
        bytes[1], down_str[down[1]], 
        bytes[2], down_str[down[2]]);
    return ESP_OK;
}

void vfs_update_space(void*arg) {
    ILOG(TAG, "[%s]", __func__);
    uint8_t i = 0;
    while(i<VFS_MAX_PARTS) {
        if(vfs_ctx.parts[i].mount_point && vfs_ctx.parts[i].is_mounted) {
            vfs_fs_space(vfs_ctx.parts[i].mount_point, vfs_ctx.parts[i].part_type, &vfs_ctx.parts[i].total_bytes, &vfs_ctx.parts[i].free_bytes, &vfs_ctx.parts[i].used_bytes);
        }
        ++i;
    }
}

int vfs_fs_space(const char * mp, uint8_t type, uint64_t *total_bytes, uint64_t *free_bytes, uint64_t *used_bytes) {
    ILOG(TAG, "[%s]", __FUNCTION__);
    assert(total_bytes && used_bytes && free_bytes);
    esp_err_t ret;
#if defined(CONFIG_USE_SD_CARD) || defined(CONFIG_USE_FATFS)
    ret = esp_vfs_fat_info(mp, total_bytes, free_bytes);
#elif defined(CONFIG_USE_SPIFFS)
    ret = esp_spiffs_info(mp, total_bytes, used_bytes);
#elif defined(CONFIG_USE_LITTLEFS)
    ret = esp_littlefs_info(mp, total_bytes, used_bytes);
#endif
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "%s (%s)", strs[0], esp_err_to_name(ret));
        return ret;
    }
    if(ret) return ret;
#if defined(CONFIG_USE_SD_CARD) || defined(CONFIG_USE_FATFS)
    *used_bytes = *total_bytes - *free_bytes;
#else
    *free_bytes = *total_bytes - *used_bytes;
#endif
#if (CONFIG_LOGGER_COMMON_LOG_LEVEL < 2 || CONFIG_LOGGER_GLOBAL_LOG_LEVEL < 2)
    vfs_print_space(mp, *total_bytes, *free_bytes, *used_bytes);
#endif
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
    ILOG(TAG, "[%s] %s %s", __FUNCTION__, base ? base : "", name);
    assert(topath);
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
    ILOG(TAG, "[%s] %s %s", __FUNCTION__, base ? base : "", name);
    if (name == 0 || name[0] == 0)
        return 0;
    if (mode == 0)
        mode = "rb";
    char path[PATH_MAX_CHAR_SIZE] = {0};
    const char *p;
    get_file_path_width_base(&(path[0]), PATH_MAX_CHAR_SIZE, name, base);
    p = (*path ? path : name);
    FILE *f = fopen(p, mode);
    if (f == NULL) {
        ESP_LOGE(TAG, "[%s] open '%s' failed '%s'.", __FUNCTION__, p, strerror(errno));
        return 0;
    }
    ILOG(TAG, "[%s] file:'%s', mode:'%s'", __FUNCTION__, p, mode);
    return f;
}

int s_open(const char *name, const char *base, const char *mode) {
    ILOG(TAG, "[%s] %s %s", __FUNCTION__, base ? base : "", name);
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
        ESP_LOGE(TAG, "[%s] open '%s' failed: '%s'", __FUNCTION__, p, strerror(errno));
        return -1;
    }
    ILOG(TAG, "[%s] file:'%s', mode:'%s'", __FUNCTION__, p, mode);
    return f;
}

esp_err_t s_write_file(const char *name, const char *base, char *data) {
    ILOG(TAG, "[%s] %s %s", __FUNCTION__, base ? base : "", name);
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
    ILOG(TAG, "[%s] %s %s", __FUNCTION__, base ? base : "", name);
    if (name == 0 || name[0] == 0)
        return ESP_FAIL;
    int f = s_open(name, base, "w+");
    if (f == -1) {
        return ESP_FAIL;
    }
    int bytes = write(f, data, len ? len : strlen(data));
    if (bytes < 0) {
        ESP_LOGE(TAG, "Failed to write (%s) fd:%d", strerror(errno), f);
    }
    if (fsync(f)) {
        ESP_LOGE(TAG, "Failed to sync (%s) fd:%d", strerror(errno), f);
    }
    if (close(f)) {
        ESP_LOGE(TAG, "Failed to close (%s) fd:%d", strerror(errno), f);
    }
    return bytes;
}

char *s_read_from_file(const char *name, const char *base) {
    ILOG(TAG, "[%s] %s %s", __FUNCTION__, base ? base : "", name);
    if (name == 0 || name[0] == 0)
        return 0;
    char *buffer = 0;
    int f = s_open(name, base, "rb");
    if (f >= 0) {
        off_t flength = s_xstat_file_size(f);
        buffer = malloc(flength + 1 * sizeof(char));
        int err = read(f, buffer, sizeof(char) * flength);
        if (err < 0) {
            ESP_LOGE(TAG, "Failed to read (%s) fd:%d", strerror(errno), f);
            free(buffer);
            buffer = 0;
        } else
            buffer[flength] = 0;
        if (close(f)) {
            ESP_LOGE(TAG, "Failed to close (%s)", strerror(errno));
        }
    }
    return buffer;
}

int s_rename_file_n(const char *old, const char *new, uint8_t rmifexists) {
    ILOG(TAG, "[%s] %s %s", __FUNCTION__, old, new);
    if (!old || !new)
        return -1;
    if (!s_xfile_exists(old))
        return -1;
    if (s_xfile_exists(new) && rmifexists) {
        if (unlink(new) < 0) {
            ESP_LOGE(TAG, "[%s] Failed to unlink (%s)", __FILE__, strerror(errno));
            return -1;
        }
    }
    if (rename(old, new) < 0) {
        ESP_LOGE(TAG, "[%s] Failed to rename [%s to %s], (%s)", __FILE__, old, new, strerror(errno));
        return -1;
    }
    return 0;
}

int s_rename_file(const char *old, const char *new, const char *base) {
    ILOG(TAG, "[%s] %s %s %s", __FUNCTION__, base ? base : "", old, new);
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