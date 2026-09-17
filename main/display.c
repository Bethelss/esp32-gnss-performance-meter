/*
 * Minimal 128x64 monochrome I2C OLED driver (SH1106 / SSD1306) with a
 * 1 KB framebuffer in internal RAM. Written directly on top of the
 * ESP-IDF new I2C master API (i2c_master.h) - no external component
 * needed, which keeps the project self-contained and easy to audit.
 *
 * Both controllers are page-addressed (8 rows of 8 vertical pixels).
 * Differences handled here:
 *   - SH1106 has a 132-byte column RAM; drawing at column 0 needs a
 *     +2 column offset (seg offset 2) or text appears shifted.
 *   - Init sequences differ slightly (charge pump / display clock).
 *
 * Default controller: SH1106 (most likely for a 1.3" module).
 * Override in display.h or via -DOLED_FORCE_SSD1306=1.
 */

#include "display.h"
#include "gnss/gnss.h"
#include "gnss/ubx_config.h"

#include <string.h>
#include <stdio.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "display";

#if defined(OLED_FORCE_SSD1306) && !defined(OLED_FORCE_SH1106)
#define CONTROLLER_IS_SH1106 0
#else
#define CONTROLLER_IS_SH1106 1
#endif

/* 5x8 font, ASCII 32..126 (subset enough for status text). */
static const uint8_t font5x8[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */
    {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */
    {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */
    {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */
    {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x14,0x08,0x3E,0x08,0x14}, /* * */
    {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x08,0x14,0x22,0x41,0x00}, /* < */
    {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x00,0x22,0x14,0x08,0x00}, /* > */
    {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* @ */
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x3E,0x41,0x49,0x49,0x7A}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x3F,0x40,0x38,0x40,0x3F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x07,0x08,0x70,0x08,0x07}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
    {0x00,0x7F,0x41,0x41,0x00}, /* [ */
    {0x02,0x04,0x08,0x10,0x20}, /* backslash */
    {0x00,0x41,0x41,0x7F,0x00}, /* ] */
    {0x04,0x02,0x01,0x02,0x04}, /* ^ */
    {0x40,0x40,0x40,0x40,0x40}, /* _ */
    {0x00,0x01,0x02,0x04,0x00}, /* ` */
    {0x20,0x54,0x54,0x54,0x78}, /* a */
    {0x7F,0x48,0x44,0x44,0x38}, /* b */
    {0x38,0x44,0x44,0x44,0x20}, /* c */
    {0x38,0x44,0x44,0x48,0x7F}, /* d */
    {0x38,0x54,0x54,0x54,0x18}, /* e */
    {0x08,0x7E,0x09,0x01,0x02}, /* f */
    {0x08,0x14,0x54,0x54,0x3C}, /* g */
    {0x7F,0x08,0x04,0x04,0x78}, /* h */
    {0x00,0x44,0x7D,0x40,0x00}, /* i */
    {0x20,0x40,0x44,0x3D,0x00}, /* j */
    {0x7F,0x10,0x28,0x44,0x00}, /* k */
    {0x00,0x41,0x7F,0x40,0x00}, /* l */
    {0x7C,0x04,0x18,0x04,0x78}, /* m */
    {0x7C,0x08,0x04,0x04,0x78}, /* n */
    {0x38,0x44,0x44,0x44,0x38}, /* o */
    {0x7C,0x14,0x14,0x14,0x08}, /* p */
    {0x08,0x14,0x14,0x18,0x7C}, /* q */
    {0x7C,0x08,0x04,0x04,0x08}, /* r */
    {0x48,0x54,0x54,0x54,0x20}, /* s */
    {0x04,0x3F,0x44,0x40,0x20}, /* t */
    {0x3C,0x40,0x40,0x20,0x7C}, /* u */
    {0x1C,0x20,0x40,0x20,0x1C}, /* v */
    {0x3C,0x40,0x30,0x40,0x3C}, /* w */
    {0x44,0x28,0x10,0x28,0x44}, /* x */
    {0x0C,0x50,0x50,0x50,0x3C}, /* y */
    {0x44,0x64,0x54,0x4C,0x44}, /* z */
    {0x00,0x08,0x36,0x41,0x00}, /* { */
    {0x00,0x00,0x7F,0x00,0x00}, /* | */
    {0x00,0x41,0x36,0x08,0x00}, /* } */
};

static uint8_t s_fb[OLED_HEIGHT / 8][OLED_WIDTH];
static i2c_master_dev_handle_t s_dev;

/*
 * oled_write_cmd - send one COMMAND byte to the controller.
 *
 * I2C protocol of these OLED controllers: every I2C transaction starts
 * with the device address, then a "control byte", then payload:
 *   0x00 = "the payload is a command"   (used here)
 *   0x40 = "the following bytes are pixel data" (see oled_write_data)
 * The 50 in i2c_master_transmit(..., 50) is the timeout in milliseconds.
 */
static int oled_write_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};
    return i2c_master_transmit(s_dev, buf, 2, 50);
}

/*
 * oled_write_data - push display BYTES to the controller.
 * Control byte 0x40 marks "data". Transactions are limited to 16 payload
 * bytes at a time (control byte + 16), which stays well within typical
 * I2C driver limits; 128 columns therefore take 8 transactions per page.
 */
static int oled_write_data(const uint8_t *data, size_t len)
{
    uint8_t buf[17];
    buf[0] = 0x40;
    while (len > 0) {
        size_t chunk = len > 16 ? 16 : len;
        memcpy(buf + 1, data, chunk);
        esp_err_t err = i2c_master_transmit(s_dev, buf, chunk + 1, 50);
        if (err != ESP_OK) return err;
        data += chunk;
        len -= chunk;
    }
    return ESP_OK;
}

/*
 * oled_init_sequence - power-up configuration of the controller.
 * These registers (multiplex rate, charge pump, contrast, scan direction)
 * are per-datasheet; the two controllers differ slightly. Notable bits:
 *   0xAF = display ON (last command)   0xAE = display OFF (first)
 * SH1106 additionally uses an internal DC-DC boost (0xAD/0x8B) instead of
 * the SSD1306-style charge pump (0x8D/0x14).
 */
static void oled_init_sequence(bool sh1106)
{
    if (sh1106) {
        oled_write_cmd(0xAE);       /* display off */
        oled_write_cmd(0x02);       /* set display start line = 2 (SH1106 RAM offset) */
        oled_write_cmd(0x10);       /* set column addr MSB = 0 */
        oled_write_cmd(0x00);       /* set column addr LSB = 0 */
        oled_write_cmd(0x40);       /* start line 0 */
        oled_write_cmd(0x81); oled_write_cmd(0x80); /* contrast */
        oled_write_cmd(0xA0);       /* segment remap normal */
        oled_write_cmd(0xC0);       /* scan direction normal */
        oled_write_cmd(0xA6);       /* normal display */
        oled_write_cmd(0xA8); oled_write_cmd(0x3F); /* multiplex 64 */
        oled_write_cmd(0xA4);       /* display from RAM */
        oled_write_cmd(0xD3); oled_write_cmd(0x00); /* display offset 0 */
        oled_write_cmd(0xD5); oled_write_cmd(0x80); /* clock div */
        oled_write_cmd(0xD9); oled_write_cmd(0x22); /* precharge */
        oled_write_cmd(0xDB); oled_write_cmd(0x35); /* VCOM detect */
        oled_write_cmd(0xAD); oled_write_cmd(0x8B); /* DC-DC on */
        oled_write_cmd(0x33);       /* charge pump */
        oled_write_cmd(0xAF);       /* display on */
    } else {
        oled_write_cmd(0xAE);       /* display off */
        oled_write_cmd(0xD5); oled_write_cmd(0x80); /* clock */
        oled_write_cmd(0xA8); oled_write_cmd(0x3F); /* multiplex */
        oled_write_cmd(0xD3); oled_write_cmd(0x00); /* offset */
        oled_write_cmd(0x40);       /* start line 0 */
        oled_write_cmd(0x8D); oled_write_cmd(0x14); /* charge pump on */
        oled_write_cmd(0x20); oled_write_cmd(0x02); /* page addressing */
        oled_write_cmd(0xA1);       /* segment remap */
        oled_write_cmd(0xC8);       /* scan dir */
        oled_write_cmd(0xDA); oled_write_cmd(0x12); /* COM pins */
        oled_write_cmd(0x81); oled_write_cmd(0xCF); /* contrast */
        oled_write_cmd(0xD9); oled_write_cmd(0xF1); /* precharge */
        oled_write_cmd(0xDB); oled_write_cmd(0x40); /* VCOM detect */
        oled_write_cmd(0xA4);
        oled_write_cmd(0xA6);
        oled_write_cmd(0xAF);       /* display on */
    }
}

/*
 * oled_flush - transfer the whole framebuffer to the panel.
 *
 * The panel memory is organized as 8 "pages" of 8 pixel rows each; one
 * byte = 8 vertical pixels of one column. For each page we:
 *   1. select the page        (0xB0 | page)
 *   2. set the start column   (two commands: low/high nibble)
 *   3. stream 128 data bytes
 * SH1106 detail: its RAM is 132 columns wide but the visible window is
 * 128, so we set the start column to +2 - otherwise everything renders
 * shifted 2 px left with the first column cut off.
 */
static void oled_flush(void)
{
    for (int page = 0; page < OLED_HEIGHT / 8; page++) {
        oled_write_cmd((uint8_t)(0xB0 | page));                  /* page addr */
        if (CONTROLLER_IS_SH1106) {
            oled_write_cmd(0x02);   /* column low = 2 (SH1106 RAM offset) */
            oled_write_cmd(0x10);   /* column high = 0 */
        } else {
            oled_write_cmd(0x00);
            oled_write_cmd(0x10);
        }
        oled_write_data(s_fb[page], OLED_WIDTH);
    }
}

static void fb_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

/*
 * fb_draw_text - write a string into the framebuffer.
 *
 * Coordinates are in text cells, not pixels: each character is 6 columns
 * wide (5 font pixels + 1 gap) and 8 rows tall, so the 128x64 panel fits
 * 21 characters x 8 rows. Font lookup: font5x8[] starts at ASCII 32
 * (space), hence the "- 32". Out-of-range characters become '?'.
 * Only the framebuffer is touched here (fast RAM writes); the actual
 * I2C transfer happens once per frame in oled_flush().
 */
/* Draw one text char at pixel position; x in 6-px cells. */
static void fb_draw_text(int row, int col, const char *s)
{
    int y = row * 8;
    if (y < 0 || y >= OLED_HEIGHT) return;
    int x = col * 6;
    while (*s && x + 5 < OLED_WIDTH) {
        char c = *s++;
        if (c < 32 || c > 126) c = '?';
        const uint8_t *g = font5x8[c - 32];
        for (int i = 0; i < 5; i++) {
            s_fb[y / 8][x + i] = g[i];
        }
        x += 6;
    }
}

/* ---- public API ---- */

/*
 * display_init - bring up the I2C bus, find the panel, initialize it.
 *
 * I2C refresher: a 2-wire bus (SDA data, SCL clock) where every device
 * has a 7-bit address. Communication starts with the address; if no chip
 * answers, the master gets a NACK - which is exactly how the bus scan
 * below detects what is connected (i2c_master_probe tries one address).
 *
 * Steps:
 *   1. create the I2C master bus on GPIO8 (SDA) / GPIO9 (SCL) @ 400 kHz
 *   2. probe every address 0x08..0x77, remember what answers
 *   3. prefer 0x3C, fall back to 0x3D, else first found
 *   4. register the OLED as a device and run the init command sequence
 */
int display_init(oled_info_t *info)
{
    memset(info, 0, sizeof(*info));

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = OLED_I2C_PORT,
        .sda_io_num = OLED_I2C_SDA_PIN,
        .scl_io_num = OLED_I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Bus scan: probe all 7-bit addresses */
    ESP_LOGI(TAG, "Scanning I2C bus (SDA=%d SCL=%d @ %d kHz)...",
             OLED_I2C_SDA_PIN, OLED_I2C_SCL_PIN, OLED_I2C_FREQ_HZ / 1000);
    for (uint8_t a = 0x08; a < 0x78; a++) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = a,
            .scl_speed_hz = OLED_I2C_FREQ_HZ,
        };
        i2c_master_dev_handle_t probe;
        if (i2c_master_bus_add_device(bus, &dev_cfg, &probe) == ESP_OK) {
            if (i2c_master_probe(bus, a, 50) == ESP_OK) {
                ESP_LOGI(TAG, "I2C device found at 0x%02X", a);
                if (info->addr_count < 8) {
                    info->addr_list[info->addr_count++] = a;
                }
            }
            i2c_master_bus_rm_device(probe);
        }
    }

    if (info->addr_count == 0) {
        ESP_LOGE(TAG, "No I2C devices found. Check wiring / pull-ups (SDA=%d SCL=%d).",
                 OLED_I2C_SDA_PIN, OLED_I2C_SCL_PIN);
        i2c_del_master_bus(bus);
        return ESP_ERR_NOT_FOUND;
    }

    /* Prefer 0x3C, then 0x3D, then first found */
    uint8_t addr = info->addr_list[0];
    for (int i = 0; i < info->addr_count; i++) {
        if (info->addr_list[i] == 0x3C) { addr = 0x3C; break; }
        if (info->addr_list[i] == 0x3D) { addr = 0x3D; break; }
    }
    info->address = addr;
    info->found = true;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = OLED_I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add OLED device: %s", esp_err_to_name(err));
        return err;
    }

    /* Init with the configured controller. */
    oled_init_sequence(CONTROLLER_IS_SH1106);
    info->init_ok = true;
    info->is_sh1106 = CONTROLLER_IS_SH1106;
    ESP_LOGI(TAG, "OLED init OK at 0x%02X, controller=%s",
             addr, CONTROLLER_IS_SH1106 ? "SH1106" : "SSD1306");
    return ESP_OK;
}

/*
 * display_show_startup - boot banner (shown for 2 s by the display task).
 */
void display_show_startup(const oled_info_t *info)
{
    if (!info->init_ok) return;
    fb_clear();
    fb_draw_text(0, 0, "GNSS LOGGER TEST");
    fb_draw_text(2, 0, "ESP32-S3");
    fb_draw_text(3, 0, info->init_ok ? "OLED OK" : "OLED FAIL");
    char line[24];
    snprintf(line, sizeof(line), "I2C: 0x%02X", info->address);
    fb_draw_text(4, 0, line);
    fb_draw_text(5, 0, CONTROLLER_IS_SH1106 ? "CTRL: SH1106" : "CTRL: SSD1306");
    oled_flush();
}

/*
 * display_show_page - draw one of three rotating diagnostic pages.
 *
 * Unit conversions happen ONLY here (the GNSS state keeps native units):
 *   gSpeed / sAcc: mm/s -> km/h is *3.6/1000 = *0.0036
 *   hAcc / vAcc:   mm -> m by /1000
 *   pDOP:          0.01 units -> decimal (e.g. 250 = 2.50)
 * `solution` is passed as void* to avoid a display<-gnss header cycle;
 * it is really a gnss_solution_t snapshot (already copied under the mutex).
 */
void display_show_page(int page,
                       const void *solution,   /* gnss_solution_t* */
                       uint32_t pps_count, uint32_t pps_period_us,
                       uint32_t pps_missed, bool pps_ok,
                       const char *ver_str)
{
    if (!s_dev) return;
    const gnss_solution_t *sol = (const gnss_solution_t *)solution;
    char line[24];

    fb_clear();

    switch (page % 4) {
    case 0: {   /* fix / speed / rate / PPS */
        static const char *fixnames[] = {"NO FIX", "DR", "2D", "3D", "G+DR", "TIME"};
        const char *fix = (sol->pvt.fixType <= 5) ? fixnames[sol->pvt.fixType] : "?";
        fb_draw_text(0, 0, "GNSS M10");
        snprintf(line, sizeof(line), "FIX: %s SV: %u", fix, sol->pvt.numSV);
        fb_draw_text(1, 0, line);
        float hz = itow_stats_measured_hz(&sol->itow);
        snprintf(line, sizeof(line), "RATE: %.1f Hz", hz);
        fb_draw_text(2, 0, line);
        snprintf(line, sizeof(line), "SPD: %.2f km/h", sol->pvt.gSpeed * 0.0036f);
        fb_draw_text(3, 0, line);
        snprintf(line, sizeof(line), "sAcc: %.2f km/h", sol->pvt.sAcc * 0.0036f);
        fb_draw_text(4, 0, line);
        snprintf(line, sizeof(line), "PPS: %s miss:%lu",
                 pps_ok ? "OK" : (pps_count ? "INT" : "NO"),
                 (unsigned long)pps_missed);
        fb_draw_text(5, 0, line);
        snprintf(line, sizeof(line), "UBX: %lu crc:%lu", (unsigned long)sol->parser.ubx_packets_ok,
                 (unsigned long)sol->parser.ubx_checksum_errors);
        fb_draw_text(6, 0, line);
        break;
    }
    case 1: {   /* position + accuracy */
        fb_draw_text(0, 0, "POSITION");
        snprintf(line, sizeof(line), "LAT: %ld", (long)sol->pvt.lat);
        fb_draw_text(1, 0, line);
        snprintf(line, sizeof(line), "LON: %ld", (long)sol->pvt.lon);
        fb_draw_text(2, 0, line);
        snprintf(line, sizeof(line), "hAcc: %lu m", (unsigned long)(sol->pvt.hAcc / 1000));
        fb_draw_text(3, 0, line);
        snprintf(line, sizeof(line), "vAcc: %lu m", (unsigned long)(sol->pvt.vAcc / 1000));
        fb_draw_text(4, 0, line);
        snprintf(line, sizeof(line), "pDOP: %u.%02u", sol->pvt.pDOP / 100, sol->pvt.pDOP % 100);
        fb_draw_text(5, 0, line);
        snprintf(line, sizeof(line), "SAT vis:%u used:%u", sol->sat.visible, sol->sat.used);
        fb_draw_text(6, 0, line);
        break;
    }
    case 2: {   /* SIGNAL: satellite visibility and quality */
        fb_draw_text(0, 0, "SIGNAL");
        snprintf(line, sizeof(line), "vis:%u used:%u", sol->sat.visible, sol->sat.used);
        fb_draw_text(1, 0, line);
        /* Per-constellation "used/visible", two per row (col 0 and col 11). */
        int row = 2;
        int slot = 0;                     /* 0 = left column, 1 = right column */
        for (int g = 0; g < GNSS_ID_COUNT && row < 6; g++) {
            if (sol->sat.vis_per_gnss[g] > 0) {
                snprintf(line, sizeof(line), "%s %u/%u",
                         gnss_id_name((uint8_t)g),
                         sol->sat.used_per_gnss[g], sol->sat.vis_per_gnss[g]);
                fb_draw_text(row, slot ? 11 : 0, line);
                if (slot) row++;          /* right column done -> next row */
                slot ^= 1;
            }
        }
        /* C/N0 quality of the used satellites (the ones in the solution). */
        snprintf(line, sizeof(line), "CNO avg:%u max:%d",
                 (unsigned)nav_sat_avg_cno_used(&sol->sat), sol->sat.max_cno);
        fb_draw_text(6, 0, line);
        break;
    }
    case 3: {   /* diagnostics */
        fb_draw_text(0, 0, "DIAGNOSTICS");
        snprintf(line, sizeof(line), "UBX ok: %lu", (unsigned long)sol->parser.ubx_packets_ok);
        fb_draw_text(1, 0, line);
        snprintf(line, sizeof(line), "crc: %lu len: %lu",
                 (unsigned long)sol->parser.ubx_checksum_errors,
                 (unsigned long)sol->parser.ubx_length_errors);
        fb_draw_text(2, 0, line);
        snprintf(line, sizeof(line), "PPS per: %lu us", (unsigned long)pps_period_us);
        fb_draw_text(3, 0, line);
        snprintf(line, sizeof(line), "PPS miss: %lu", (unsigned long)pps_missed);
        fb_draw_text(4, 0, line);
        snprintf(line, sizeof(line), "dITOW: %lu ms", (unsigned long)sol->itow.delta_iTOW);
        fb_draw_text(5, 0, line);
        if (ver_str && ver_str[0]) {
            snprintf(line, sizeof(line), "%.20s", ver_str);
            fb_draw_text(6, 0, line);
        }
        break;
    }
    }

    oled_flush();
}

