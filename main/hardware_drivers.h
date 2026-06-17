#ifndef HARDWARE_DRIVERS_H
#define HARDWARE_DRIVERS_H

#include "app_config.h"
#include "esp_err.h"
#include "driver/i2s_std.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t hw_init_i2s_pcg(void);
esp_err_t hw_init_sd_card(void);
void      hw_deinit_sd_card(void);

i2s_chan_handle_t hw_get_i2s(void);

#ifdef __cplusplus
}
#endif

#endif // HARDWARE_DRIVERS_H
