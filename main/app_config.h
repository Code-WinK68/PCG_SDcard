#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "driver/gpio.h"

/* ============================================================
 *  PCG PARAMETERS
 * ============================================================ */
#define PCG_SAMPLE_RATE      16000      /* Hz */
#define PCG_BLOCK_SAMPLES    40         /* samples/block - exactly 2.5ms at 16kHz */
#define SYNC_TOLERANCE        2         /* |actual - 40| <= 2 still counts as GOOD */

/* ============================================================
 *  HARDWARE - ICS-43434 (I2S Philips Mono) - ESP32-WROOM-32
 * ============================================================ */
#define I2S_MIC_PORT         I2S_NUM_0
#define I2S_BCLK_PIN         GPIO_NUM_26
#define I2S_WS_PIN           GPIO_NUM_25
#define I2S_DATA_PIN         GPIO_NUM_34   /* input-only, correct for DIN */

/* ============================================================
 *  SD CARD (SPI) - ESP32-WROOM-32
 * ============================================================ */
#define SD_SPI_HOST          SPI2_HOST
#define SD_MOSI_PIN          GPIO_NUM_13
#define SD_MISO_PIN          GPIO_NUM_12
#define SD_CLK_PIN           GPIO_NUM_14
#define SD_CS_PIN            GPIO_NUM_15
#define SD_MOUNT_POINT       "/sdcard"
#define SD_FILENAME          "/sdcard/pcg_data.csv"

/* ============================================================
 *  SD WRITE BATCHING - accumulate N blocks then write once
 *
 *  CSV_LINE_MAX worst-case calculation:
 *    block_index(10) + ts(20) + actual_count(2) + sync_flag(4) ~ 40 bytes
 *    40 PCG columns, each int32 negative max "-8388608" (8 chars + comma) = 9 bytes
 *    40 x 9 = 360 bytes
 *    Total ~ 40 + 360 + newline = 401 bytes -> use 450 for safety margin
 * ============================================================ */
#define BATCH_BLOCKS         50         /* 50 x 2.5ms = 125ms per SD write */
#define CSV_LINE_MAX          450       /* max bytes per CSV row (40 columns, with margin) */

#endif /* APP_CONFIG_H */