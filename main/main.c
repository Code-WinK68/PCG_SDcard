#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app_config.h"
#include "hardware_drivers.h"

static const char *TAG = "PCG_APP";

/// ================================================================ 
///  PCG Task - single task, no core pinning
/// ================================================================
static void task_pcg_all_in_one(void *pv)
{
    (void)pv;  // unused parameter
    
    ESP_LOGI(TAG, "=== PCG Task (single task, no core pinning) starting ===");
    // Get I2S handle from hardware driver - this is safe because we call hw_init_i2s_pcg() before creating the task, so the I2S is guaranteed to be initialized by the time we get here.
    i2s_chan_handle_t i2s = hw_get_i2s();

    // Buffers for I2S data and PCG values, plus variables for CSV batching and stats
    int32_t  i2s_raw[PCG_BLOCK_SAMPLES];   // raw 32-bit I2S data (only upper 24 bits used, shifted later) - size = 160 bytes
    int32_t  pcg_val[PCG_BLOCK_SAMPLES];   // extracted 24-bit PCG values (after shifting) - size = 160 bytes
    size_t   bytes_read = 0;

    // CSV buffer: allocate enough for BATCH_BLOCKS lines of CSV, with max CSV_LINE_MAX chars per line (see app_config.h for rationale)
    char *csv_buf = (char *)malloc(BATCH_BLOCKS * CSV_LINE_MAX);
    if (!csv_buf) {                          // fatal error - not enough RAM for CSV buffer
        ESP_LOGE(TAG, "FATAL: not enough RAM for CSV buffer (%d bytes)",
                 BATCH_BLOCKS * CSV_LINE_MAX);
        vTaskDelete(NULL);                   // delete self to avoid running without SD write capability
        return;
    }
    uint32_t csv_len   = 0;                 // current length of data in csv_buf
    uint32_t row_count = 0;                 // number of rows currently accumulated in csv_buf (for batching)

    // Open SD file for appending - if it fails, we can still run and output to Serial Plotter, just no SD writes
    FILE *f = fopen(SD_FILENAME, "a");  // "a" = append mode, creates file if it doesn't exist
    if (f) {
        char hdr[256]; 
        int  hlen = snprintf(hdr, sizeof(hdr), 
                              "block_index,timestamp_us,actual_count,sync_flag"); // CSV header with metadata columns
        for (int i = 1; i <= PCG_BLOCK_SAMPLES; i++) {                           // add PCG column headers (pcg_1, pcg_2, ..., pcg_40)
            hlen += snprintf(hdr + hlen, sizeof(hdr) - hlen, ",pcg_%d", i);      
        }
        hlen += snprintf(hdr + hlen, sizeof(hdr) - hlen, "\n");
        fwrite(hdr, 1, hlen, f);                                                // write header to SD file
        fflush(f);                                                              // flush to ensure it's written immediately
        ESP_LOGI(TAG, "Opened SD file: %s (header %d bytes)", SD_FILENAME, hlen); // log file open and header write
    } else {
        ESP_LOGW(TAG, "Could not open SD file - Plotter only, NO SD write");      // log warning if SD file open fails
    }

    uint32_t block_index   = 0; // count of blocks read (for CSV metadata)
    uint32_t good_count    = 0; // count of good blocks (actual_count within tolerance)
    uint32_t bad_count     = 0; // count of bad blocks (actual_count outside tolerance)
    uint32_t stat_timer    = 0; // timer for logging stats every 5 seconds (2000 blocks at 2.5ms/block)
    uint32_t timeout_count = 0; // count of I2S read timeouts (for logging)

    /* Print header so Serial Plotter recognizes 2 channels */
    printf("\n>PCG:0\tSYNC_OK:0\n");
    fflush(stdout);

    // Main loop: read I2S, process PCG, output to Serial Plotter, accumulate CSV, write to SD in batches
    while (1) {
        
        esp_err_t ret = i2s_channel_read(i2s, i2s_raw, sizeof(i2s_raw),
                                          &bytes_read, pdMS_TO_TICKS(200));

        if (ret != ESP_OK) {
            timeout_count++;
            /* Log every occurrence at first, but throttle if it becomes frequent
             * so the UART doesn't get flooded */
            if (timeout_count <= 5 || timeout_count % 50 == 0) {
                ESP_LOGW(TAG, "I2S read error: %s (timeout #%lu)",
                         esp_err_to_name(ret), (unsigned long)timeout_count);
            }
            continue;
        }

        uint64_t ts = esp_timer_get_time();
        uint8_t  actual_count = (uint8_t)(bytes_read / sizeof(int32_t));

        /* STEP 2: Check if we got 40 samples (+/- SYNC_TOLERANCE) */
        int diff = (int)actual_count - PCG_BLOCK_SAMPLES;
        if (diff < 0) diff = -diff;
        bool is_good = (diff <= SYNC_TOLERANCE);

        if (is_good) good_count++; else bad_count++;
        block_index++;

        /* Extract 24-bit MSB from 32-bit frame (only for samples actually received) */
        for (int i = 0; i < actual_count && i < PCG_BLOCK_SAMPLES; i++) {
            pcg_val[i] = i2s_raw[i] >> 8;
        }
        /* If samples missing, fill remaining with 0 so CSV has full columns */
        for (int i = actual_count; i < PCG_BLOCK_SAMPLES; i++) {
            pcg_val[i] = 0;
        }

        /* STEP 3: Serial Plotter output */
        /* Take 1 representative sample (first in block) for smooth PCG waveform */
        /* SYNC_OK: 1 = GOOD (enough samples), 0 = BAD (missing/extra samples) */
        printf("PCG:%ld\tSYNC_OK:%d\n",
               (long)(pcg_val[0] >> 12),     /* scale down to a few hundred */
               is_good ? 1 : 0);
        fflush(stdout);

        /* STEP 4: Accumulate CSV, write to SD in batches */
        /* Overflow guard: check enough room for 1 NEW row before writing
         * (worst case 1 row ~ 401 bytes, see CSV_LINE_MAX explanation in app_config.h) */
        if ((BATCH_BLOCKS * CSV_LINE_MAX) - csv_len < CSV_LINE_MAX) {
            /* Buffer fuller than expected (rare, since we sized with margin) - flush now */
            if (f && csv_len > 0) {
                fwrite(csv_buf, 1, csv_len, f);
                fflush(f);
            }
            csv_len   = 0;
            row_count = 0;
        }

        const char *sf = is_good ? "GOOD" : "BAD";
        int n = snprintf(csv_buf + csv_len, BATCH_BLOCKS * CSV_LINE_MAX - csv_len,
                          "%lu,%llu,%d,%s",
                          (unsigned long)block_index,
                          (unsigned long long)ts,
                          actual_count,
                          sf);
        if (n > 0) csv_len += n;

        for (int i = 0; i < PCG_BLOCK_SAMPLES; i++) {
            uint32_t left = (BATCH_BLOCKS * CSV_LINE_MAX) - csv_len;
            if (left < 16) break;  /* safety - not enough room for ",-8388608" (9 bytes) + margin */
            n = snprintf(csv_buf + csv_len, left, ",%ld", (long)pcg_val[i]);
            if (n > 0) csv_len += n;
        }
        n = snprintf(csv_buf + csv_len, (BATCH_BLOCKS * CSV_LINE_MAX) - csv_len, "\n");
        if (n > 0) csv_len += n;

        row_count++;

        if (row_count >= BATCH_BLOCKS) {
            if (f) {
                fwrite(csv_buf, 1, csv_len, f);
                fflush(f);
            }
            csv_len   = 0;
            row_count = 0;
        }

        /* Log stats roughly every 5 seconds (5s / 2.5ms = 2000 blocks) */
        stat_timer++;
        if (stat_timer >= 2000) {
            stat_timer = 0;
            uint32_t total = good_count + bad_count;
            ESP_LOGI(TAG, "---- Stats 5s ----  Total=%lu  GOOD=%lu (%.1f%%)  BAD=%lu (%.1f%%)  Timeouts=%lu",
                     (unsigned long)total,
                     (unsigned long)good_count, total ? 100.f * good_count / total : 0,
                     (unsigned long)bad_count,  total ? 100.f * bad_count  / total : 0,
                     (unsigned long)timeout_count);
        }
    }

    /* Note: while(1) above is infinite, task never exits on its own.
     * This is standard FreeRTOS task design - no cleanup code needed
     * (fclose/free) because the task lives for the device's whole life. */
}

/* ================================================================ */
void app_main(void)
{
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "  PCG SINGLE-TASK - ESP32-WROOM-32");
    ESP_LOGI(TAG, "  16kHz I2S -> check 40 samples/block -> SD Card");
    ESP_LOGI(TAG, "================================================");

    if (hw_init_i2s_pcg() != ESP_OK) { ESP_LOGE(TAG, "FATAL: I2S init failed"); return; }
    if (hw_init_sd_card() != ESP_OK) {
        ESP_LOGW(TAG, "SD Card mount failed - continuing, Plotter only");
    }

    /* Create task with plain xTaskCreate() - NOT PinnedToCore
     * FreeRTOS scheduler decides the core, no hard binding */
    BaseType_t ok = xTaskCreate(task_pcg_all_in_one, "PCG_Task", 8192, NULL, 5, NULL);

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "FATAL: Task creation failed - out of heap?");
        return;
    }

    ESP_LOGI(TAG, "Task running. Open Serial Plotter (921600 baud) to see PCG + SYNC_OK");
}