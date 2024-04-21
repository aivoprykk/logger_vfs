
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <sys/types.h>

#include "esp_err.h"
#include "esp_log.h"

#include "vfs.h"
#include "vfs_private.h"
#include "logger_common.h"

static const char *TAG = "vfs";

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

int get_file_path_width_base(char *topath, size_t pathlen, const char *name,
                             const char *base) {
    ESP_LOGI(TAG, "[%s] %s", __FUNCTION__, name);
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
    ESP_LOGI(TAG, "[%s] file:'%s', mode:'%s'", __FUNCTION__, p, mode);
    return f;
}

int s_open(const char *name, const char *base, const char *mode) {
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
    ESP_LOGI(TAG, "[%s] file:'%s', mode:'%s'", __FUNCTION__, p, mode);
    return f;
}

esp_err_t s_write_file(const char *name, const char *base, char *data) {
    if (name == 0 || name[0] == 0)
        return ESP_FAIL;
    ESP_LOGI(TAG, "[%s] file:%s", __FUNCTION__, name);
    FILE *f = s_open_file(name, base, "w");
    if (f == NULL) {
        return ESP_FAIL;
    }
    fwrite(data, strlen(data), sizeof(data), f);
    fclose(f);
    return ESP_OK;
}

esp_err_t s_write(const char *name, const char *base, char *data, size_t len) {
    if (name == 0 || name[0] == 0)
        return ESP_FAIL;
    ESP_LOGI(TAG, "[%s] file:%s", __FUNCTION__, name);
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
