#include "vfs_private.h"
#ifdef CONFIG_LOGGER_VFS_ENABLED
#include "inttypes.h"
#include "esp_partition.h"

static const char *TAG = "vfs.partition";

// Get the string name of type enum values used in this example
static const char* get_type_str(esp_partition_type_t type)
{
#if (C_LOG_LEVEL < 3)
    ILOG(TAG, "[%s] with type %d ...", __func__, type);
#endif
    switch(type) {
        case ESP_PARTITION_TYPE_APP:
            return "ESP_PARTITION_TYPE_APP";
        case ESP_PARTITION_TYPE_DATA:
            return "ESP_PARTITION_TYPE_DATA";
        default:
            return "UNKNOWN_PARTITION_TYPE"; // type not used in this example
    }
}

#if (C_LOG_LEVEL < 3)
// Get the string name of subtype enum values used in this example
static const char* get_subtype_str(esp_partition_subtype_t subtype)
{
    ILOG(TAG, "[%s] with subtype %d ...", __func__, subtype);
    switch(subtype) {
        case ESP_PARTITION_SUBTYPE_DATA_NVS:
            return "ESP_PARTITION_SUBTYPE_DATA_NVS";
        case ESP_PARTITION_SUBTYPE_DATA_PHY:
            return "ESP_PARTITION_SUBTYPE_DATA_PHY";
        case ESP_PARTITION_SUBTYPE_APP_FACTORY:
            return "ESP_PARTITION_SUBTYPE_APP_FACTORY";
        case ESP_PARTITION_SUBTYPE_DATA_FAT:
            return "ESP_PARTITION_SUBTYPE_DATA_FAT";
        case ESP_PARTITION_SUBTYPE_DATA_SPIFFS:
            return "ESP_PARTITION_SUBTYPE_DATA_SPIFFS";
        default:
            return "UNKNOWN_PARTITION_SUBTYPE"; // subtype not used in this example
    }
}
#endif

// Find the partition using given parameters
static int find_partition(esp_partition_type_t type, esp_partition_subtype_t subtype, const char* name)
{
#if (C_LOG_LEVEL < 3)
    ILOG(TAG, "[%s] with type %s, subtype %s, label %s...", __func__, get_type_str(type), get_subtype_str(subtype),
                    name == NULL ? "NULL (unspecified)" : name);
#endif
    const esp_partition_t * part  = esp_partition_find_first(type, subtype, name);
    if (part == NULL) {
        ESP_LOGE(TAG, "Partition not found");
        return 0;
    }
#if (C_LOG_LEVEL < 3)
    ILOG(TAG, "Partition found, address: 0x%" PRIx32 ", size: %"PRIu32, part->address, part->size);
#endif
    return 1;
}

int has_fatfs_partition()
{
#if (C_LOG_LEVEL < 3)
    ILOG(TAG, "[%s] ...", __func__);
#endif
    #if defined(CONFIG_USE_FATFS)
    return find_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, CONFIG_FATFS_PARTITION_LABEL);
    #else
    return 0;
    #endif
}

int has_spiffs_partition()
{
#if (C_LOG_LEVEL < 3)
    ILOG(TAG, "[%s] ...", __func__);
#endif
    #if defined(CONFIG_USE_SPIFFS)
    return find_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, CONFIG_SPIFFS_PARTITION_LABEL);
    #else
    return 0;
    #endif
}

int has_littlefs_partition()
{
#if (C_LOG_LEVEL < 3)
    ILOG(TAG, "[%s] ...", __func__);
#endif
    #if defined(CONFIG_USE_LITTLEFS)
    return find_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, CONFIG_LITTLEFS_PARTITION_LABEL);
    #else
    return 0;
    #endif
}

#endif // CONFIG_LOGGER_VFS_ENABLED
