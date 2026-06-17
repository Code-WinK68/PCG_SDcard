#include "hardware_drivers.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/spi_master.h"
#include "sdmmc_cmd.h"

static const char *TAG = "HW";

static i2s_chan_handle_t s_i2s  = NULL;
static sdmmc_card_t     *s_card = NULL;

i2s_chan_handle_t hw_get_i2s(void) { return s_i2s; }

/* ------------------------------------------------------------------ */
esp_err_t hw_init_i2s_pcg(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_MIC_PORT, I2S_ROLE_MASTER);
    /* DMA buffer = exactly 1 block (40 frames) - read finishes at exactly 2.5ms, no extra latency */
    chan_cfg.dma_desc_num  = 4;
    chan_cfg.dma_frame_num = PCG_BLOCK_SAMPLES;

    esp_err_t r = i2s_new_channel(&chan_cfg, NULL, &s_i2s);
    if (r != ESP_OK) { ESP_LOGE(TAG, "I2S new_channel: %s", esp_err_to_name(r)); return r; }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(PCG_SAMPLE_RATE),
        /* ICS-43434: Philips standard, 32-bit frame, take 24-bit MSB, mono (L/R=GND -> LEFT channel) */
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                         I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .bclk = I2S_BCLK_PIN,
            .ws   = I2S_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din  = I2S_DATA_PIN,
        },
    };

    r = i2s_channel_init_std_mode(s_i2s, &std_cfg);
    if (r != ESP_OK) { ESP_LOGE(TAG, "I2S init_std_mode: %s", esp_err_to_name(r)); return r; }

    r = i2s_channel_enable(s_i2s);
    if (r == ESP_OK) {
        ESP_LOGI(TAG, "I2S0 Philips Mono %dHz 32bit OK (BCLK=%d WS=%d DIN=%d)",
                 PCG_SAMPLE_RATE, I2S_BCLK_PIN, I2S_WS_PIN, I2S_DATA_PIN);
    }
    return r;
}

/* ------------------------------------------------------------------ */
esp_err_t hw_init_sd_card(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mnt = {
        .format_if_mount_failed = false,
        .max_files              = 4,
        .allocation_unit_size   = 16 * 1024,
    };

    spi_bus_config_t bus = {
        .mosi_io_num     = SD_MOSI_PIN,
        .miso_io_num     = SD_MISO_PIN,
        .sclk_io_num     = SD_CLK_PIN,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t r = spi_bus_initialize(SD_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (r != ESP_OK) { ESP_LOGE(TAG, "SPI bus init: %s", esp_err_to_name(r)); return r; }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;

    sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev.gpio_cs = SD_CS_PIN;
    dev.host_id = SD_SPI_HOST;

    r = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &dev, &mnt, &s_card);
    if (r == ESP_OK) {
        ESP_LOGI(TAG, "SD Card mount OK -> %s", SD_MOUNT_POINT);
    } else {
        ESP_LOGE(TAG, "SD Card mount FAIL: %s", esp_err_to_name(r));
    }
    return r;
}

void hw_deinit_sd_card(void)
{
    if (s_card) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
        spi_bus_free(SD_SPI_HOST);
        ESP_LOGI(TAG, "SD Card unmount OK");
    }
}