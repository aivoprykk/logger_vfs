

#include <errno.h>
#include <string.h>

#include "driver/sdmmc_host.h"
#include "driver/sdmmc_types.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "vfs_fat_sdspi.h"
#include "logger_events.h"
#include "logger_common.h"

#if defined(CONFIG_HAS_BOARD_LILYGO_EPAPER_T5)
#define SDCARD_HOST SPI2_HOST
#else
#define SDCARD_HOST SPI3_HOST
#endif


#define WRITE_BUFFER_SIZE (16 * 1024)
static const char *TAG = "vfs_fat_sdspi";

typedef struct wl_context_s {
    uint8_t mounted;
    const char *mount_point;
    sdmmc_card_t * volume_handle;
    sdmmc_host_t host;
    size_t allocation_unit_size;
    spi_bus_config_t bus_cfg;
    sdspi_device_config_t slot_config;
} wl_context_t;

# define WL_CONTEXT_INIT {0, CONFIG_SD_MOUNT_POINT, 0, {0}, WRITE_BUFFER_SIZE}

static struct wl_context_s wl_ctx = WL_CONTEXT_INIT;

#ifdef CONFIG_SD_DEBUG_STATS
#define TIME_ARRAY_SIZE 500
#define PRINT_DIFF 0

static esp_err_t s_write_speed(const char *name) {
    ESP_LOGI(TAG, "[%s]", __FUNCTION__);
    if (name == 0 || *name == 0)
        return ESP_FAIL;
    ESP_LOGI(TAG, "[%s] file:%s", __FUNCTION__, name);
    FILE *f = s_open_file(name, wl_ctx.mount_point, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return ESP_FAIL;
    }
    uint64_t time_array[TIME_ARRAY_SIZE];
    char write_buffer[WRITE_BUFFER_SIZE];
    ESP_LOGI(TAG, "[%s] Init write buffer ", __FUNCTION__);
    // initialize write buffer
    for (int i = 0; i < WRITE_BUFFER_SIZE; i++) {
        write_buffer[i] = ' ' + (i % 64);
    }

    ESP_LOGI(TAG, "[%s] Write to file ", __FUNCTION__);
    uint64_t start = esp_timer_get_time();
    for (int counter = 0; counter < TIME_ARRAY_SIZE; counter++) {
        fwrite(write_buffer, 1, WRITE_BUFFER_SIZE, f);
        time_array[counter] = esp_timer_get_time();
    }
    fclose(f);
    ESP_LOGI(TAG, "[%s] File written ", __FUNCTION__);

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
    ESP_LOGI(TAG, "write buffer size = %d", WRITE_BUFFER_SIZE);
    ESP_LOGI(TAG, "sum=%llu microseconds, average=%llu microseconds", sum, average);
    ESP_LOGI(TAG, "maximum=%llu microseconds, minimum=%llu microseconds", maximum, minimum);
    ESP_LOGI(TAG, "highest write speed = %llu byte/s", ((uint64_t)WRITE_BUFFER_SIZE) * 1000 * 1000 / minimum);
    ESP_LOGI(TAG, "average write speed = %llu byte/s", ((uint64_t)WRITE_BUFFER_SIZE) * 1000 * 1000 / average);
    ESP_LOGI(TAG, "lowest write speed = %llu byte/s", ((uint64_t)WRITE_BUFFER_SIZE) * 1000 * 1000 / maximum);
    return ESP_OK;
}
#endif

uint32_t sdcard_space() {
    FATFS *fs;
    DWORD fre_clust, fre_sect, tot_sect;
    /* Get volume information and free clusters of drive 0 */
    f_getfree(wl_ctx.mount_point, &fre_clust, &fs);
    /* Get total sectors and free sectors */
    tot_sect = (fs->n_fatent - 2) * fs->csize;
    fre_sect = fre_clust * fs->csize;
    /* Print the free space (assuming 512 bytes/sector) */
    ESP_LOGI(TAG,"%10lu KiB total drive space.\r\n%10lu MB available.\r\n%10lu free clust.\r\n",
        (tot_sect / 2 / 1024), (fre_sect / 2 / 1024), fre_clust);
    return (fre_sect / 2 / 1024);
}

static uint32_t init_host_frequency() {
    assert(wl_ctx.volume_handle->max_freq_khz <= wl_ctx.volume_handle->host.max_freq_khz);

    /* Find highest frequency in the following list,
     * which is below volume_handle->max_freq_khz.
     */
    const uint32_t freq_values[] = {
        SDMMC_FREQ_52M, 
        SDMMC_FREQ_HIGHSPEED, 
        SDMMC_FREQ_26M,
        SDMMC_FREQ_DEFAULT, 
        10000
        // NOTE: in sdspi mode, 20MHz may not work. in that case, add 10MHz here.
    };
    const int n_freq_values = sizeof(freq_values) / sizeof(freq_values[0]);

    uint32_t selected_freq = SDMMC_FREQ_PROBING;
    for (int i = 0; i < n_freq_values; ++i) {
        uint32_t freq = freq_values[i];
        if (wl_ctx.volume_handle->max_freq_khz >= freq) {
            ESP_LOGI(TAG, "Set card max allowed frequency to %lu", freq);
            selected_freq = freq;
            break;
        }
    }
    return selected_freq;
}

int sdcard_init(void) {
    esp_err_t ret = ESP_OK;

    gpio_set_pull_mode(CONFIG_SD_PIN_CLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(CONFIG_SD_PIN_CS, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(CONFIG_SD_PIN_MISO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(CONFIG_SD_PIN_MOSI, GPIO_PULLUP_ONLY);

    ESP_LOGI(TAG, "Initializing SD card");

    // Use settings defined above to initialize SD card and mount FAT
    // filesystem. Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience
    // functions. Please check its source code and implement error recovery when
    // developing production applications.

    ESP_LOGI(TAG, "Using SPI peripheral");

    // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT
    // (20MHz) For setting a specific frequency, use host.max_freq_khz (range
    // 400kHz - 40MHz for SDMMC) Example: for fixed frequency of 10MHz, use
    // host.max_freq_khz = 10000;
    sdmmc_host_t lhost = SDSPI_HOST_DEFAULT();
    //lhost.slot = SDSPI_DEFAULT_HOST;
    wl_ctx.host.slot = SDCARD_HOST;
    memcpy(&wl_ctx.host, &lhost, sizeof(sdmmc_host_t));
    // host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CONFIG_SD_PIN_MOSI,
        .miso_io_num = CONFIG_SD_PIN_MISO,
        .sclk_io_num = CONFIG_SD_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 2000,
    };
    memcpy(&wl_ctx.bus_cfg, &bus_cfg, sizeof(spi_bus_config_t));

    ret = spi_bus_initialize(wl_ctx.host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus (%s),", esp_err_to_name(ret));
        // return ret;
        goto done;
    }
    // This initializes the slot without card detect (CD) and write protect (WP)
    // signals. Modify slot_config->gpio_cd and slot_config->gpio_wp if your
    // board has these signals.
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_SD_PIN_CS;
    slot_config.host_id = wl_ctx.host.slot;
    memcpy(&wl_ctx.slot_config, &slot_config, sizeof(sdspi_device_config_t));

    done:
    return ret;
}

int sdcard_mount(void) {
    esp_err_t ret = ESP_OK;
    ESP_LOGI(TAG, "Mounting filesystem");
    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    // FATFS out_fs;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = wl_ctx.allocation_unit_size};
    ret = esp_vfs_fat_sdspi_mount(wl_ctx.mount_point, &wl_ctx.host, &wl_ctx.slot_config, &mount_config, &wl_ctx.volume_handle);
    
    delay_ms(50);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                          "If you want the card to be formatted, set the "
                          "EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG,
                     "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.",
                     esp_err_to_name(ret));
        }
        esp_event_post(LOGGER_EVENT, LOGGER_EVENT_SDCARD_MOUNT_FAILED, 0, 0, portMAX_DELAY);
        goto done;
    }
    else {
        wl_ctx.mounted = 1;
    }
    ESP_LOGI(TAG, "Filesystem mounted at %s", wl_ctx.mount_point);
    if (!ret && wl_ctx.volume_handle) {
        esp_event_post(LOGGER_EVENT, LOGGER_EVENT_SDCARD_MOUNTED, 0, 0, portMAX_DELAY);
        /* uint32_t f = init_host_frequency(volume_handle);
        if (f > SDMMC_FREQ_DEFAULT)
            sdspi_host_set_card_clk(host, f); */
#ifdef CONFIG_SD_DEBUG_STATS
        /* // Card has been initialized, print its properties
        sdmmc_card_print_info(stdout, wl_ctx.volume_handle);
        // sdcard_space(); */
        /* char * buf = s_read_from_file("config->txt", mount_point);
        if(buf) {
          ESP_LOGI(TAG,"%s", buf);
          free(buf);
        } */
        ret = s_write_speed(".test");
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to write file, sry...");
        }
        struct stat st;
        if (stat(".test", &st) == 0) {
            // Delete it if it exists
            unlink(".test");
        }
#endif
    }
    done:
    return ret;
}

void sdcard_umount(void) {
    // All done, unmount partition and disable SDMMC peripheral
    if(wl_ctx.mounted) {
        if (!wl_ctx.volume_handle) {
            ESP_LOGE(TAG, "Card not present");
            return;
        }
        esp_err_t ret;
        ret = esp_vfs_fat_sdcard_unmount(wl_ctx.mount_point, wl_ctx.volume_handle);
        if (ret == ESP_OK)
            ESP_LOGD(TAG, "Card unmounted");
        esp_event_post(LOGGER_EVENT, LOGGER_EVENT_SDCARD_UNMOUNTED, 0, 0, portMAX_DELAY);
        UNUSED_PARAMETER(ret);
    }
}

void sdcard_uninit(void) {
    esp_err_t ret;
    spi_bus_free(wl_ctx.host.slot);
    UNUSED_PARAMETER(ret);
}

bool sdcard_is_mounted(void) {
    return wl_ctx.mounted;
}
