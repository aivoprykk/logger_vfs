#ifndef F96BB488_D84E_42B5_BF4F_4B1C3CED8DFA
#define F96BB488_D84E_42B5_BF4F_4B1C3CED8DFA

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_event.h"
#include "logger_common.h"

#define VFS_EVENT_BASE 0x90  // Component ID 9
// Declare an event base
ESP_EVENT_DECLARE_BASE(VFS_EVENT);        // declaration of the VFS_EVENT family
#define VFS_EVENT_ENUM(x) VFS_EVENT_ ## x,
#define VFS_EVENT_ENUM_L(x, y) VFS_EVENT_ ## y ## _ ## x,

#define VFS_EVENT_LIST(l, n) \
    l(MOUNTED, n) \
    l(MOUNT_FAILED, n) \
    l(WRITE_FAILED, n) \
    l(UNMOUNTED, n) \
    l(INIT_DONE, n) \
    l(DEINIT_DONE, n)

enum {
#if defined(CONFIG_USE_SD_CARD)
    VFS_EVENT_LIST(VFS_EVENT_ENUM_L, SDCARD)
#endif
#if defined(CONFIG_USE_FATFS)
    VFS_EVENT_LIST(VFS_EVENT_ENUM_L, FAT_PARTITION)
#endif
#if defined(CONFIG_USE_LITTLEFS)
    VFS_EVENT_LIST(VFS_EVENT_ENUM_L, LITTEFS_PARTITION)
#endif
#if defined(CONFIG_USE_SPIFFS)
    VFS_EVENT_LIST(VFS_EVENT_ENUM_L, SPIFFS_PARTITION)
#endif
    VFS_EVENT_LOG_PARTITION_CHANGED
};

const char * vfs_event_strings(int id);

#ifdef __cplusplus
}
#endif

#endif /* F96BB488_D84E_42B5_BF4F_4B1C3CED8DFA */
