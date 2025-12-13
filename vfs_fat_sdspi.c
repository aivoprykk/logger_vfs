

#include "vfs_private.h"
#if defined(CONFIG_USE_SD_CARD)

#include <string.h>

#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_vfs_fat.h"
#include "esp_timer.h"
#include "sdmmc_cmd.h"
#ifdef CONFIG_DEBUG_PIN_CONNECTIONS
#include "test_io.h"
#endif

#include "vfs_fat_sdspi.h"

#include "vfs_events.h"

#if SOC_SDMMC_IO_POWER_EXTERNAL
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif

#if defined(CONFIG_SD_SPI1_HOST)
#define SDCARD_HOST SPI1_HOST
#elif defined(CONFIG_SD_SPI2_HOST)
#define SDCARD_HOST SPI2_HOST
#elif defined(CONFIG_SD_SPI3_HOST)
#define SDCARD_HOST SPI3_HOST
#endif


static const char *TAG = "vfs_fat_sdspi";

typedef struct wl_context_s {
    uint8_t mounted;
    const char *mount_point;
    sdmmc_card_t * volume_handle;
    sdmmc_host_t host;
    size_t allocation_unit_size;
#if defined(CONFIG_SD_USE_SPI)
    spi_bus_config_t bus_cfg;
    sdspi_device_config_t device_config;
#else
    #define _IS_UHS1    (CONFIG_SDMMC_SPEED_UHS_I_SDR50 || CONFIG_SDMMC_SPEED_UHS_I_DDR50)
    sdmmc_slot_config_t device_config;
#endif
} wl_context_t;

# define WL_CONTEXT_INIT {0, CONFIG_SD_MOUNT_POINT, 0, {0}, CONFIG_WL_SECTOR_SIZE}

static struct wl_context_s wl_ctx = WL_CONTEXT_INIT;

static uint32_t init_host_frequency() {
    FUNC_ENTRY(TAG);
    assert(wl_ctx.volume_handle->max_freq_khz <= wl_ctx.volume_handle->host.max_freq_khz);

    /* Find highest frequency in the following list,
     * which is below volume_handle->max_freq_khz.
     */
    const uint32_t freq_values[] = {
        SDMMC_FREQ_SDR50,
        SDMMC_FREQ_52M,
        SDMMC_FREQ_DDR50,
        SDMMC_FREQ_HIGHSPEED, 
        SDMMC_FREQ_26M,
        SDMMC_FREQ_DEFAULT, 
        10000
    };
    const int n_freq_values = sizeof(freq_values) / sizeof(freq_values[0]);

    uint32_t selected_freq = SDMMC_FREQ_PROBING;
    for (int i = 0; i < n_freq_values; ++i) {
        uint32_t freq = freq_values[i];
        if (wl_ctx.volume_handle->max_freq_khz >= freq) {
            ILOG(TAG, "Set card max allowed frequency to %lu", freq);
            selected_freq = freq;
            break;
        }
    }
    return selected_freq;
}

#ifdef CONFIG_DEBUG_PIN_CONNECTIONS
#if defined(CONFIG_SD_USE_SPI)
const char* names[] = {"CLK ", "MOSI", "MISO", "CS"};
#else
const char* names[] = {"CLK", "CMD", "D0", "D1", "D2", "D3"};
#endif

const int pins[] = {
#if defined(CONFIG_SD_USE_SPI)
    CONFIG_SD_PIN_CLK,
    CONFIG_SD_PIN_MOSI,
    CONFIG_SD_PIN_MISO,
    CONFIG_SD_PIN_CS
#else
    CONFIG_SD_PIN_CLK,
    CONFIG_SD_PIN_CMD,
    CONFIG_SD_PIN_D0
    #ifdef CONFIG_SD_MMC_BUS_WIDTH_4
    ,CONFIG_SD_PIN_D1,
    CONFIG_SD_PIN_D2,
    CONFIG_SD_PIN_D3
    #endif
#endif
};

const int pin_count = sizeof(pins)/sizeof(pins[0]);

#if CONFIG_ENABLE_ADC_FEATURE
const int adc_channels[] = {
#if defined(CONFIG_SD_USE_SPI)
    CONFIG_SD_ADC_PIN_CLK,
    CONFIG_SD_ADC_PIN_MOSI,
    CONFIG_SD_ADC_PIN_MISO,
    CONFIG_SD_ADC_PIN_CS
#else
    CONFIG_SD_ADC_PIN_CLK,
    CONFIG_SD_ADC_PIN_CMD,
    CONFIG_SD_ADC_PIN_D0
    #ifdef CONFIG_SD_MMC_BUS_WIDTH_4
    ,CONFIG_SD_ADC_PIN_D1,
    CONFIG_SD_ADC_PIN_D2,
    CONFIG_SD_ADC_PIN_D3
    #endif
#endif
};
const int adc_units[] = {
#if defined(CONFIG_SD_USE_SPI)
    CONFIG_SD_ADC_PIN_CLK,
    CONFIG_SD_ADC_PIN_MOSI,
    CONFIG_SD_ADC_PIN_MISO,
    CONFIG_SD_ADC_PIN_CS
#else
    CONFIG_SD_ADC_PIN_CLK,
    CONFIG_SD_ADC_PIN_CMD,
    CONFIG_SD_ADC_PIN_D0
    #ifdef CONFIG_SD_MMC_BUS_WIDTH_4
    ,CONFIG_SD_ADC_PIN_D1,
    CONFIG_SD_ADC_PIN_D2,
    CONFIG_SD_ADC_PIN_D3
    #endif
#endif
};
#endif //CONFIG_ENABLE_ADC_FEATURE
const int modes[] = {
#if defined(CONFIG_SD_USE_SPI)
    GPIO_MODE_INPUT,
    GPIO_MODE_INPUT,
    GPIO_MODE_INPUT,
    GPIO_MODE_INPUT
#else
    GPIO_MODE_INPUT,
    GPIO_MODE_INPUT,
    GPIO_MODE_INPUT
    #ifdef CONFIG_SD_MMC_BUS_WIDTH_4
    ,GPIO_MODE_INPUT,
    GPIO_MODE_INPUT,
    GPIO_MODE_INPUT
    #endif
#endif
};
pin_configuration_t pin_test_config = {
    .names = names,
    .pins = pins,
    .modes = modes,
#if CONFIG_ENABLE_ADC_FEATURE
    .adc_channels = adc_channels,
    .adc_units = adc_units,
#endif
};
#endif //CONFIG_DEBUG_PIN_CONNECTIONS


int sdcard_init(void) {
    FUNC_ENTRY(TAG);
    esp_err_t ret = ESP_OK;

    sdmmc_host_t lhost = SDSPI_HOST_DEFAULT();
    memcpy(&wl_ctx.host, &lhost, sizeof(sdmmc_host_t));
    wl_ctx.host.slot = SDCARD_HOST;
    
    // Set shorter timeouts to avoid blocking when no SD card is present
    // Default is 60000ms (60s), reduce to 2000ms (2s)
    wl_ctx.host.command_timeout_ms = 2000;
    
    // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT
    // (20MHz) For setting a specific frequency, use host.max_freq_khz (range
    // 400kHz - 40MHz for SDMMC) Example: for fixed frequency of 10MHz, use
    // wl_ctx.host.max_freq_khz = SDMMC_FREQ_DEFAULT;
#if CONFIG_SD_MMC_SPEED_HS
    wl_ctx.host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
#elif CONFIG_SD_MMC_SPEED_UHS_I_SDR50
    wl_ctx.host.max_freq_khz = SDMMC_FREQ_SDR50;
    wl_ctx.host.flags &= ~SDMMC_HOST_FLAG_DDR;
#elif CONFIG_SD_MMC_SPEED_UHS_I_DDR50
    wl_ctx.host.max_freq_khz = SDMMC_FREQ_DDR50;
#elif CONFIG_SD_MMC_SPEED_PROBING
    wl_ctx.host.max_freq_khz = SDMMC_FREQ_PROBING;
#endif
#if CONFIG_SD_PWR_CTRL_LDO_INTERNAL_IO
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = CONFIG_SD_PWR_CTRL_LDO_IO_ID,
    };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;

    ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create a new on-chip LDO power control driver");
        return ret;
    }
    host.pwr_ctrl_handle = pwr_ctrl_handle;
#endif

#if defined(CONFIG_SD_USE_SPI)

    ILOG(TAG, "[%s] Using SDSPI peripheral", __func__);
#if (CONFIG_SD_PIN_CLK >= 0)
        gpio_set_pull_mode(CONFIG_SD_PIN_CLK, GPIO_PULLUP_ONLY);
#endif
#if (CONFIG_SD_PIN_CS >= 0)
        gpio_set_pull_mode(CONFIG_SD_PIN_CS, GPIO_PULLUP_ONLY);
#endif
#if (CONFIG_SD_PIN_MISO >= 0)
        gpio_set_pull_mode(CONFIG_SD_PIN_MISO, GPIO_PULLUP_ONLY);
#endif
#if (CONFIG_SD_PIN_MOSI >= 0)
        gpio_set_pull_mode(CONFIG_SD_PIN_MOSI, GPIO_PULLUP_ONLY);
#endif
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CONFIG_SD_PIN_MOSI,
        .miso_io_num = CONFIG_SD_PIN_MISO,
        .sclk_io_num = CONFIG_SD_PIN_CLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 4000,
    };
    memcpy(&wl_ctx.bus_cfg, &bus_cfg, sizeof(spi_bus_config_t));
    ILOG(TAG, "[%s] Initializing sdspi device at slot: %d", __func__, wl_ctx.host.slot);
    ret = spi_bus_initialize(wl_ctx.host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        WLOG(TAG, "[%s] Failed to initialize sdspi device (%s).", __func__, esp_err_to_name(ret));
        goto done;
    }
    // This initializes the slot without card detect (CD) and write protect (WP)
    // signals. Modify device_config->gpio_cd and device_config->gpio_wp if your
    // board has these signals.
    sdspi_device_config_t device_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    device_config.gpio_cs = CONFIG_SD_PIN_CS;
    device_config.host_id = wl_ctx.host.slot;
    memcpy(&wl_ctx.device_config, &device_config, sizeof(sdspi_device_config_t));

#else

    ILOG(TAG, "[%s] Using SMMMC peripheral", __func__);

    sdmmc_slot_config_t device_config = SDMMC_SLOT_CONFIG_DEFAULT();
#if _IS_UHS1
    device_config.flags |= SDMMC_SLOT_FLAG_UHS1;
#endif
    // Set bus width to use:
#ifdef CONFIG_SD_SD_MMC_BUS_WIDTH_4
    device_config.width = 4;
#else
    device_config.width = 1;
#endif

    // On chips where the GPIOs used for SD card can be configured, set them in
    // the device_config structure:
// #ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
    device_config.clk = CONFIG_SD_PIN_CLK;
    device_config.cmd = CONFIG_SD_PIN_CMD;
    device_config.d0 = CONFIG_SD_PIN_D0;
#ifdef CONFIG_SD_MMC_BUS_WIDTH_4
    device_config.d1 = CONFIG_SD_PIN_D1;
    device_config.d2 = CONFIG_SD_PIN_D2;
    device_config.d3 = CONFIG_SD_PIN_D3;
#endif  // CONFIG_SD_SD_MMC_BUS_WIDTH_4
// #endif  // CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
    memcpy(&wl_ctx.device_config, &device_config, sizeof(sdmmc_slot_config_t));

#endif

    esp_event_post(VFS_EVENT, VFS_EVENT_SDCARD_INIT_DONE, 0, 0, pdMS_TO_TICKS(100));
    ILOG(TAG, "[%s] done", __func__);
#if defined(CONFIG_SD_USE_SPI)
    done:
#endif
    return ret;
}

int sdcard_mount(void) {
    FUNC_ENTRY(TAG);
    esp_err_t ret = ESP_OK;
    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    // FATFS out_fs;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5, // Need 5 for txtlog, ubxlog, sbplog, gpxlog + config files
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        .disk_status_check_enable = false};
    ILOG(TAG, "[%s] Mounting SD FAT filesystem at %s", __func__, wl_ctx.mount_point);

    // Add timeout protection for mount operation
    uint32_t start_time = esp_timer_get_time() / 1000; // Convert to ms
    
#if defined(CONFIG_SD_USE_SPI)
    ret = esp_vfs_fat_sdspi_mount(wl_ctx.mount_point, &wl_ctx.host, &wl_ctx.device_config, &mount_config, &wl_ctx.volume_handle);
#else
    ret = esp_vfs_fat_sdmmc_mount(wl_ctx.mount_point, &wl_ctx.host, &wl_ctx.device_config, &mount_config, &wl_ctx.volume_handle);
#endif

    uint32_t mount_time = (esp_timer_get_time() / 1000) - start_time;
    if (mount_time > 5000) {  // Log if mount took longer than 5 seconds
        WLOG(TAG, "[%s] Mount operation took %lu ms", __func__, mount_time);
    }

    vTaskDelay(pdMS_TO_TICKS(20)); // Reduced from 50ms to 20ms

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            WLOG(TAG, "[%s] Failed to mount filesystem.", __func__);
         } else {
            WLOG(TAG, "[%s] Failed to initialize the card (%s). "
                 "Make sure SD card lines have pull-up resistors in place.", __func__, esp_err_to_name(ret));
#ifdef CONFIG_DEBUG_PIN_CONNECTIONS
            check_pins(&pin_test_config, pin_count);
#endif
        }
        // esp_event_post(VFS_EVENT, VFS_EVENT_SDCARD_MOUNT_FAILED, 0, 0, portMAX_DELAY);
        goto done;
    }
    else {
        wl_ctx.mounted = 1;
    }
    ILOG(TAG, "[%s] Filesystem mounted at %s", __FUNCTION__, wl_ctx.mount_point);
    if (!ret && wl_ctx.volume_handle) {
        // esp_event_post(VFS_EVENT, VFS_EVENT_SDCARD_MOUNTED, 0, 0, portMAX_DELAY);
        /* uint32_t f = init_host_frequency(volume_handle);
        if (f > SDMMC_FREQ_DEFAULT)
            sdspi_host_set_card_clk(host, f); */
#ifdef CONFIG_SD_DEBUG_STATS
        /* // Card has been initialized, print its properties
        sdmmc_card_print_info(stdout, wl_ctx.volume_handle);
        // sdcard_space(); */
        /* char * buf = s_read_from_file("config->txt", mount_point);
        if(buf) {
          ILOG(TAG,"%s", buf);
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
    FUNC_ENTRY(TAG);
    // All done, unmount partition and disable SDMMC peripheral
    if(wl_ctx.mounted) {
        if (!wl_ctx.volume_handle) {
            ESP_LOGE(TAG, "Card not present");
            return;
        }
        esp_err_t ret;
        ret = esp_vfs_fat_sdcard_unmount(wl_ctx.mount_point, wl_ctx.volume_handle);
#if (C_LOG_LEVEL <= LOG_INFO_NUM)
        if (ret == ESP_OK)
            ILOG(TAG, "[%s] Card unmounted", __func__);
#endif
        wl_ctx.mounted = 0;
        esp_event_post(VFS_EVENT, VFS_EVENT_SDCARD_UNMOUNTED, 0, 0, pdMS_TO_TICKS(100));
        UNUSED_PARAMETER(ret);
    }
}

void sdcard_uninit(void) {
    FUNC_ENTRY(TAG);
    esp_err_t ret;
#if defined(CONFIG_SD_USE_SPI)
    spi_bus_free(wl_ctx.host.slot);
#endif
#if CONFIG_SD_PWR_CTRL_LDO_INTERNAL_IO
    ret = sd_pwr_ctrl_del_on_chip_ldo(pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete the on-chip LDO power control driver");
        return;
    }
#endif
    esp_event_post(VFS_EVENT, VFS_EVENT_SDCARD_DEINIT_DONE, 0, 0, pdMS_TO_TICKS(100));
    UNUSED_PARAMETER(ret);
}

bool sdcard_is_mounted(void) {
    return wl_ctx.mounted;
}

#endif
