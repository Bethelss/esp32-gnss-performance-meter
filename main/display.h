#pragma once
/*
 * OLED display support for 1.3" 128x64 I2C module (SH1106/SSD1306).
 * Iteration 2: three rotating UBX-oriented diagnostic pages.
 */

#include <stdint.h>
#include <stdbool.h>

#define OLED_I2C_PORT      0
#define OLED_I2C_SCL_PIN   9
#define OLED_I2C_SDA_PIN   8
#define OLED_I2C_FREQ_HZ   400000
#define OLED_WIDTH         128
#define OLED_HEIGHT        64

typedef struct {
    bool     found;
    uint8_t  address;
    bool     init_ok;
    bool     is_sh1106;
    uint8_t  addr_list[8];
    int      addr_count;
} oled_info_t;

int display_init(oled_info_t *info);
void display_show_startup(const oled_info_t *info);

/* Live status pages (rotated by caller every few seconds).
 * Page 0: fix/sv/speed/sAcc/rate/PPS health
 * Page 1: lat/lon/hAcc/vAcc/pDOP/sats
 * Page 2: SIGNAL - satellite visibility, per-constellation, C/N0 stats
 * Page 3: UART baud/UBX stats/PPS diagnostics/receiver version
 */
void display_show_page(int page,
                       const void *solution,   /* gnss_solution_t* */
                       uint32_t pps_count, uint32_t pps_period_us,
                       uint32_t pps_missed, bool pps_ok,
                       const char *ver_str);
