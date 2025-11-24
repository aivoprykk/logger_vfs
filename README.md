# Logger VFS Component

A comprehensive Virtual File System (VFS) abstraction layer for ESP32-based GPS logger applications, providing unified file system operations across multiple storage backends including SD cards, SPI flash partitions, and various file system types.

## Features

### Multi-File System Support
- **FAT File System**: Full FAT32/FAT16 support for SD cards and SPI flash partitions
- **SPIFFS**: Lightweight file system optimized for SPI NOR flash
- **LittleFS**: Power-loss resilient file system with dynamic wear leveling
- **SD Card Integration**: Native SD card support via SPI interface with configurable pin mappings

### Unified API
- Consistent file operations across all supported file systems
- Thread-safe path buffer management
- Automatic mount point resolution
- Unified error handling and logging

### Storage Management
- Real-time space monitoring and reporting
- Configurable partition sizes and mount points
- Automatic space updates via timer callbacks
- Support for multiple concurrent partitions

### Event-Driven Architecture
- Comprehensive event system for mount/unmount operations
- Async operation status notifications
- Error event reporting for debugging
- Integration with ESP-IDF event loop

### Performance & Reliability
- Optimized write operations with retry mechanisms
- Binary semaphore-based thread safety
- Configurable logging verbosity
- Debug statistics for SD card performance analysis

## Installation

### ESP-IDF Integration
Add this component to your ESP-IDF project:

```cmake
# In your project's CMakeLists.txt
set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/components logger_vfs)
```

### PlatformIO Integration
Add to your `platformio.ini`:

```ini
[env]
lib_deps =
    ; Add other dependencies
    file://./components/logger_vfs
```

## Configuration

### Kconfig Options

#### File System Selection
```kconfig
CONFIG_LOGGER_VFS_ENABLED=y                    # Enable VFS component
CONFIG_USE_SD_CARD=y                           # Enable SD card support
CONFIG_USE_FATFS=y                             # Enable FAT file system on SPI flash
CONFIG_USE_SPIFFS=n                            # Enable SPIFFS file system
CONFIG_USE_LITTLEFS=n                          # Enable LittleFS file system
```

#### SD Card Configuration
```kconfig
CONFIG_SD_USE_SPI=y                            # Use SPI interface (recommended)
CONFIG_SD_USE_SDMMC=n                          # Use SDMMC interface
CONFIG_SD_MMC_BUS_WIDTH_1=y                    # 1-bit bus width
CONFIG_SD_MMC_BUS_WIDTH_4=n                    # 4-bit bus width (requires more pins)
```

#### Pin Configuration (ESP32)
```kconfig
CONFIG_SD_PIN_CS=15                            # Chip select GPIO
CONFIG_SD_PIN_CLK=14                           # Clock GPIO
CONFIG_SD_PIN_MOSI=13                          # MOSI GPIO
CONFIG_SD_PIN_MISO=2                           # MISO GPIO
```

#### Pin Configuration (ESP32-S3)
```kconfig
CONFIG_SD_PIN_CS=11                            # Chip select GPIO
CONFIG_SD_PIN_CLK=12                           # Clock GPIO
CONFIG_SD_PIN_MOSI=13                          # MOSI GPIO
CONFIG_SD_PIN_MISO=14                          # MISO GPIO
```

#### File System Mount Points
```kconfig
# FAT file system
CONFIG_FATFS_MOUNT_POINT="/fatfs"
CONFIG_FATFS_PARTITION_LABEL="fatstore"

# SPIFFS
CONFIG_SPIFFS_MOUNT_POINT="/spiffs"
CONFIG_SPIFFS_PARTITION_LABEL="spifstore"

# LittleFS
CONFIG_LITTLEFS_MOUNT_POINT="/littlefs"
CONFIG_LITTLEFS_PARTITION_LABEL="littlestore"
```

#### Logging Configuration
```kconfig
CONFIG_LOGGER_VFS_LOG_LEVEL_INFO=y             # Log level (TRACE, DEBUG, INFO, WARN, ERROR, USER, NONE)
```

### Partition Table Configuration

Add partitions to your `partitions.csv`:

```csv
# Name,   Type, SubType, Offset,  Size, Flags
fatstore, data, fat,     0x110000, 0x2F0000,
spifstore,data, spiffs,  0x400000, 0x100000,
littlestore,data,littlefs,0x500000, 0x100000,
```

## Usage

### Basic Initialization

```c
#include "vfs.h"

// Initialize VFS system
esp_err_t ret = vfs_init();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize VFS: %s", esp_err_to_name(ret));
    return ret;
}

// Deinitialize when done
vfs_deinit();
```

### File Operations

```c
#include "vfs.h"

// Check if file exists
if (s_xfile_exists("/sdcard/gps_log.txt")) {
    ESP_LOGI(TAG, "File exists");
}

// Read from file
char *data = s_read_from_file("config.txt", "/fatfs");
if (data) {
    ESP_LOGI(TAG, "File content: %s", data);
    free(data);
}

// Write to file
const char *content = "GPS Logger Configuration";
int written = s_write("config.txt", "/fatfs", (char*)content, strlen(content));

// Open file stream
FILE *f = s_open_file("log.txt", "/sdcard", "a");
if (f) {
    fprintf(f, "New log entry\n");
    fclose(f);
}

// Rename file
s_rename_file("old_name.txt", "new_name.txt", "/fatfs");
```

### Space Management

```c
#include "vfs.h"

// Get file system space information
uint64_t total_bytes, free_bytes, used_bytes;
esp_err_t ret = vfs_fs_space("/sdcard", 0, &total_bytes, &free_bytes, &used_bytes);
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Total: %llu bytes, Free: %llu bytes, Used: %llu bytes",
             total_bytes, free_bytes, used_bytes);
}

// Print space information
vfs_print_space("/sdcard", total_bytes, free_bytes, used_bytes);

// Update space information (called periodically)
vfs_update_space(NULL);
```

### Partition Selection

```c
#include "vfs.h"

// Select active partition for logging
// 0 = config partition, 1 = GPS log partition
esp_err_t ret = vfs_select_part(1);  // Select GPS log partition
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to select partition");
}
```

### Event Handling

```c
#include "vfs_events.h"

// Register event handler
esp_event_handler_register(VFS_EVENT, ESP_EVENT_ANY_ID, vfs_event_handler, NULL);

// Event handler function
static void vfs_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data)
{
    switch(event_id) {
        case VFS_EVENT_SDCARD_MOUNTED:
            ESP_LOGI(TAG, "SD card mounted successfully");
            break;
        case VFS_EVENT_SDCARD_MOUNT_FAILED:
            ESP_LOGE(TAG, "SD card mount failed");
            break;
        case VFS_EVENT_FAT_PARTITION_WRITE_FAILED:
            ESP_LOGE(TAG, "FAT partition write failed");
            break;
        // Handle other events...
    }
}
```

## API Reference

### Core Functions

#### Initialization & Deinitialization
- `vfs_init()` - Initialize VFS system
- `vfs_deinit()` - Deinitialize VFS system

#### File Operations
- `s_open_file(const char *name, const char *base, const char *mode)` - Open file stream
- `s_read_from_file(const char *name, const char *base)` - Read entire file content
- `s_write(const char *name, const char *base, char *data, size_t len)` - Write data to file
- `s_rename_file(const char *old, const char *n, const char *base)` - Rename file
- `s_xfile_exists(const char *filename)` - Check if file exists

#### Space Management
- `vfs_fs_space(const char *mp, uint8_t type, uint64_t *total, uint64_t *free, uint64_t *used)` - Get space info
- `vfs_print_space(const char *mp, uint64_t total, uint64_t free, uint64_t used)` - Print space info
- `vfs_update_space(void *arg)` - Update space information
- `vfs_space_str(char *arg, size_t arglen)` - Format space as string

#### Partition Management
- `vfs_select_part(uint8_t log_part_locked)` - Select active partition
- `vfs_get_part(const char *mount_point)` - Get partition configuration
- `vfs_get_part_index(const char *mount_point)` - Get partition index

### Data Structures

#### vfs_config_t
```c
typedef struct vfs_config_s {
    const char *mount_point;      // Mount point path
    uint64_t total_bytes;         // Total space in bytes
    uint64_t free_bytes;          // Free space in bytes
    uint64_t used_bytes;          // Used space in bytes
    uint8_t is_mounted;           // Mount status flag
    uint8_t write_attempts;       // Write attempt counter
    vfs_part_t part_type;         // Partition type enum
} vfs_config_t;
```

#### vfs_part_t
```c
typedef enum {
    VFS_PART_FATFS,              // FAT file system
    VFS_PART_SPIFFS,             // SPIFFS file system
    VFS_PART_LITTLEFS,           // LittleFS file system
    VFS_PART_SDCARD,             // SD card
    VFS_PART_MAX                 // Maximum partition types
} vfs_part_t;
```

### Events

#### SD Card Events
- `VFS_EVENT_SDCARD_MOUNTED` - SD card successfully mounted
- `VFS_EVENT_SDCARD_MOUNT_FAILED` - SD card mount failed
- `VFS_EVENT_SDCARD_WRITE_FAILED` - SD card write operation failed
- `VFS_EVENT_SDCARD_UNMOUNTED` - SD card unmounted
- `VFS_EVENT_SDCARD_INIT_DONE` - SD card initialization completed
- `VFS_EVENT_SDCARD_DEINIT_DONE` - SD card deinitialization completed

#### FAT Partition Events
- `VFS_EVENT_FAT_PARTITION_MOUNTED` - FAT partition mounted
- `VFS_EVENT_FAT_PARTITION_MOUNT_FAILED` - FAT partition mount failed
- `VFS_EVENT_FAT_PARTITION_WRITE_FAILED` - FAT partition write failed
- `VFS_EVENT_FAT_PARTITION_UNMOUNTED` - FAT partition unmounted
- `VFS_EVENT_FAT_PARTITION_INIT_DONE` - FAT partition init done
- `VFS_EVENT_FAT_PARTITION_DEINIT_DONE` - FAT partition deinit done

#### SPIFFS Events
- `VFS_EVENT_SPIFFS_PARTITION_MOUNTED` - SPIFFS partition mounted
- `VFS_EVENT_SPIFFS_PARTITION_MOUNT_FAILED` - SPIFFS partition mount failed
- `VFS_EVENT_SPIFFS_PARTITION_WRITE_FAILED` - SPIFFS partition write failed
- `VFS_EVENT_SPIFFS_PARTITION_UNMOUNTED` - SPIFFS partition unmounted
- `VFS_EVENT_SPIFFS_PARTITION_INIT_DONE` - SPIFFS partition init done
- `VFS_EVENT_SPIFFS_PARTITION_DEINIT_DONE` - SPIFFS partition deinit done

#### LittleFS Events
- `VFS_EVENT_LITTEFS_PARTITION_MOUNTED` - LittleFS partition mounted
- `VFS_EVENT_LITTEFS_PARTITION_MOUNT_FAILED` - LittleFS partition mount failed
- `VFS_EVENT_LITTEFS_PARTITION_WRITE_FAILED` - LittleFS partition write failed
- `VFS_EVENT_LITTEFS_PARTITION_UNMOUNTED` - LittleFS partition unmounted
- `VFS_EVENT_LITTEFS_PARTITION_INIT_DONE` - LittleFS partition init done
- `VFS_EVENT_LITTEFS_PARTITION_DEINIT_DONE` - LittleFS partition deinit done

#### General Events
- `VFS_EVENT_LOG_PARTITION_CHANGED` - Active log partition changed

## Examples

### Complete GPS Logger Setup

```c
#include "vfs.h"
#include "vfs_events.h"

static const char *TAG = "gps_logger";

static void vfs_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data)
{
    ESP_LOGI(TAG, "VFS Event: %s", vfs_event_strings(event_id));
}

void app_main(void) {
    // Initialize VFS
    ESP_ERROR_CHECK(vfs_init());

    // Register event handler
    ESP_ERROR_CHECK(esp_event_handler_register(VFS_EVENT, ESP_EVENT_ANY_ID,
                                              vfs_event_handler, NULL));

    // Select GPS logging partition
    ESP_ERROR_CHECK(vfs_select_part(1));

    // Log GPS data
    while (true) {
        // Your GPS logging logic here
        const char *gps_data = "GPS data string";

        if (s_write("gps_log.txt", NULL, (char*)gps_data, strlen(gps_data)) == ESP_OK) {
            ESP_LOGI(TAG, "GPS data logged successfully");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### SD Card Hotplug Support

```c
#include "vfs_fat_sdspi.h"

void monitor_sd_card(void) {
    while (true) {
        if (sdcard_is_mounted()) {
            // SD card is available, log to it
            s_write("hotplug_log.txt", "/sdcard", "SD card available", 16);
        } else {
            // SD card not available, use internal storage
            s_write("hotplug_log.txt", "/fatfs", "Using internal storage", 21);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
```

## Troubleshooting

### Common Issues

#### SD Card Not Detected
- Check pin connections match Kconfig settings
- Verify SD card is properly formatted (FAT32)
- Check power supply to SD card module
- Enable debug logging: `CONFIG_LOGGER_VFS_LOG_LEVEL_DEBUG=y`

#### File System Mount Failures
- Verify partition table configuration
- Check partition labels match Kconfig settings
- Ensure sufficient flash space for partitions
- Check for corrupted file systems

#### Write Operation Failures
- Verify file system is mounted and writable
- Check available space with `vfs_fs_space()`
- Enable write retry mechanisms
- Monitor VFS events for detailed error information

#### Performance Issues
- Enable SD debug stats: `CONFIG_SD_DEBUG_STATS=y`
- Check SPI clock speed configuration
- Monitor CPU usage during file operations
- Consider using DMA for large transfers

### Debug Configuration

Enable detailed debugging:

```kconfig
CONFIG_LOGGER_VFS_LOG_LEVEL_DEBUG=y
CONFIG_SD_DEBUG_STATS=y
CONFIG_DEBUG_PIN_CONNECTIONS=y
```

### Error Codes

Common ESP-IDF error codes:
- `ESP_ERR_INVALID_ARG` - Invalid arguments
- `ESP_ERR_NO_MEM` - Out of memory
- `ESP_ERR_INVALID_STATE` - Invalid VFS state
- `ESP_FAIL` - General failure

### Logging

Enable verbose logging to troubleshoot issues:

```c
// In code
ESP_LOGI(TAG, "VFS Context: initialized=%d, config_part=%d, gps_part=%d",
         vfs_ctx.vfs_initialized, vfs_ctx.config_part, vfs_ctx.gps_log_part);
```

## Dependencies

- ESP-IDF v4.4 or later
- logger_common component
- esp_vfs_fat (for FAT support)
- esp_spiffs (for SPIFFS support)
- esp_littlefs (for LittleFS support)

## Contributing

1. Follow ESP-IDF coding standards
2. Add comprehensive error handling
3. Include event notifications for state changes
4. Update documentation for new features
5. Test with multiple file system configurations

## License

See LICENSE file in component directory.