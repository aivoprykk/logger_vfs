#ifndef F96BB488_D84E_42B5_BF4F_4B1C3CED8DFA
#define F96BB488_D84E_42B5_BF4F_4B1C3CED8DFA

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_event.h"
#include <logger_common.h>

// Declare an event base
ESP_EVENT_DECLARE_BASE(VFS_EVENT);        // declaration of the VFS_EVENT family

#define VFS_EVENT_LIST(l) \
    l(VFS_EVENT_SDCARD_MOUNTED) \
    l(VFS_EVENT_SDCARD_MOUNT_FAILED) \
    l(VFS_EVENT_SDCARD_UNMOUNTED) \
    l(VFS_EVENT_SDCARD_INIT_DONE) \
    l(VFS_EVENT_SDCARD_DEINIT_DONE) \
    l(VFS_EVENT_FAT_PARTITION_MOUNTED) \
    l(VFS_EVENT_FAT_PARTITION_MOUNT_FAILED) \
    l(VFS_EVENT_FAT_PARTITION_UNMOUNTED) \


// declaration of the specific events under the VFS_EVENT family
enum {                                       
    VFS_EVENT_LIST(ENUM)
};

 extern const char * const vfs_event_strings[];

#ifdef __cplusplus
}
#endif

#endif /* F96BB488_D84E_42B5_BF4F_4B1C3CED8DFA */
