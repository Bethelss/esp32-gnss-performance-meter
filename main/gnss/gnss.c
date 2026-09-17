#include "gnss.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"       /* xTaskCreate, vTaskDelay, notifications */
#include "freertos/semphr.h"     /* mutexes (semaphore.h API) */
#include "driver/uart.h"         /* uart_read_bytes etc. */
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "gnss";

/* ---- shared state ----
 * s_sol: written by gnss_rx (parser callbacks), read by display + console.
 * Guarded by s_lock so a reader never sees a half-updated struct.
 * A FreeRTOS mutex (xSemaphoreCreateMutex) is a lock: whoever takes it
 * with xSemaphoreTake owns it until xSemaphoreGive; another taker blocks. */
static gnss_solution_t s_sol;
static SemaphoreHandle_t s_lock;

/* The byte-stream parser instance (state machine from ubx_parser.c). */
static ubx_parser_t s_parser;

/* Counter for the 1 Hz log loop: a satellite summary is printed every
 * 10th second (NAV-SAT itself still arrives at 1 Hz and updates state). */
static uint32_t s_sat_log_count;

/* Debug helper state: dump exactly one NAV-PVT after boot. */
static bool s_debug_dumped;

/* ---------------- UBX dispatcher (runs in gnss_rx context) ---------------- */

/* NMEA sentences are counted by the parser; we deliberately do NOT log
 * them here (at 20 Hz they would flood the console and disturb timing). */
static void on_nmea(const char *line, void *user)
{
    (void)line;
    (void)user;
}

/*
 * on_ubx - called by the parser for every checksum-verified UBX frame.
 * This is the DISPATCHER: its job is to route each message type to the
 * right consumer. Order matters: ACK handling first (cheap, and ACK frames
 * must not be mistaken for data), then the message types we understand.
 */
static void on_ubx(const ubx_frame_t *f, void *user)
{
    (void)user;

    /* 1. ACK-ACK / ACK-NAK matching (fires the notification if it matches
     *    the armed expectation; returns true = frame fully consumed). */
    if (ubx_ack_consume(&g_resp, f)) return;

    /* 2. MON-VER response: store it, and if gnss_init is waiting for
     * exactly this frame, wake it. */
    if (f->cls == 0x0A && f->id == 0x04) {
        ubx_mon_ver_store(f);
        if (g_resp.armed && g_resp.exp_cls == 0x0A && g_resp.exp_id == 0x04) {
            ubx_resp_dispatch(&g_resp, UBX_RESP_MONVER);
        }
        return;
    }

    /* 3. CFG-VALGET response: same pattern with its own class/id. */
    if (f->cls == 0x06 && f->id == 0x8B) {
        ubx_valget_store(&g_gnss_cfg, f);
        ESP_LOGI(TAG, "CFG-VALGET: %u bytes of key/value pairs", (unsigned)f->len);
        if (g_resp.armed && g_resp.exp_cls == 0x06 && g_resp.exp_id == 0x8B) {
            ubx_resp_dispatch(&g_resp, UBX_RESP_VALGET);
        }
        return;
    }

    /* 4. NAV-PVT: the main data product, once per navigation epoch. */
    if (f->cls == 0x01 && f->id == 0x07) {
        ubx_nav_pvt_t pvt;
        if (ubx_decode_nav_pvt(f, &pvt)) {
            if (!s_debug_dumped) {
                /* One-shot raw dump for offset verification on hardware. */
                s_debug_dumped = true;
                ubx_debug_dump_nav_pvt(f);
            }
            itow_stats_update(&s_sol.itow, pvt.iTOW);
            if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) == pdTRUE) {
                s_sol.pvt = pvt;
                s_sol.pvt_seen = true;
                s_sol.pvt_count++;
                xSemaphoreGive(s_lock);
            }
            /* If the mutex is busy we simply skip this sample: the next
             * epoch (up to 25/s) overwrites it. Never wait long here. */
        }
        return;
    }

    /* 5. NAV-SAT: satellite visibility/quality summary. */
    if (f->cls == 0x01 && f->id == 0x35) {
        ubx_nav_sat_t sat;
        if (ubx_decode_nav_sat(f, &sat)) {
            if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) == pdTRUE) {
                s_sol.sat = sat;
                s_sol.sat_seen = true;
                xSemaphoreGive(s_lock);
            }
        }
        return;
    }
}

/*
 * gnss_get_solution - thread-safe snapshot for other tasks (display etc.).
 * Copies the whole struct under the mutex; the caller works on its own
 * private copy, so the lock is held for microseconds, never while drawing.
 */
void gnss_get_solution(gnss_solution_t *out)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        *out = s_sol;
        xSemaphoreGive(s_lock);
    } else {
        memset(out, 0, sizeof(*out));   /* lock contention: report empty */
    }
}

/* ---------------- startup sequence (SEPARATE task) ---------------- */

/* Pretty-print the CFG-VALGET snapshot. Called once after boot. */
static void print_config_summary(const ubx_cfg_snapshot_t *c)
{
    if (!c->valid) {
        ESP_LOGW(TAG, "Config snapshot not received (VALGET failed/timeout)");
        return;
    }
    ESP_LOGI(TAG, "GNSS configuration:");
    ESP_LOGI(TAG, "  UART1 baud:       %lu", (unsigned long)c->baudrate);
    ESP_LOGI(TAG, "  UART1 in:  UBX=%d NMEA=%d   out: UBX=%d NMEA=%d",
             c->in_ubx, c->in_nmea, c->out_ubx, c->out_nmea);
    ESP_LOGI(TAG, "  Measurement:      %u ms   Nav rate: %u", c->meas_ms, c->nav_rate);
    /* effective rate = 1000 / (meas_ms * nav_rate); nav_rate 0 = meaning 1 */
    ESP_LOGI(TAG, "  Effective rate:   %.2f Hz",
             c->meas_ms ? 1000.0f / ((float)c->meas_ms * (c->nav_rate ? c->nav_rate : 1)) : 0.0f);
    ESP_LOGI(TAG, "  GPS=%d GAL=%d GLO=%d BDS=%d SBAS=%d QZSS=%d",
             c->gps_ena, c->gal_ena, c->glo_ena, c->bds_ena, c->sbas_ena, c->qzss_ena);
    ESP_LOGI(TAG, "  NAV-PVT UART1:    %u   NAV-SAT: %u", c->pvt_rate, c->sat_rate);
    ESP_LOGI(TAG, "  NMEA UART1: GGA=%u RMC=%u GSV=%u GSA=%u GLL=%u VTG=%u",
             c->gga_rate, c->rmc_rate, c->gsv_rate, c->gsa_rate, c->gll_rate, c->vtg_rate);
}

/*
 * startup_sequence - PHASES B..D from TASK.md.
 * IMPORTANT: this runs in the gnss_init task, NOT in gnss_rx. Every wait
 * here sleeps on a task notification while gnss_rx continues reading the
 * UART - that is the whole point of the two-task split.
 */
static void startup_sequence(void)
{
    /* PHASE B1: identify the receiver (software/hardware/protocol version). */
    ESP_LOGI(TAG, "=== PHASE B: receiver identification (MON-VER) ===");
    if (ubx_query_mon_ver(&g_gnss_ver) == 0) {
        ESP_LOGI(TAG, "MON-VER OK:");
        ESP_LOGI(TAG, "  swVersion : %s", g_gnss_ver.sw_version);
        ESP_LOGI(TAG, "  hwVersion : %s", g_gnss_ver.hw_version);
        for (int i = 0; i < g_gnss_ver.ext_count; i++) {
            ESP_LOGI(TAG, "  extension : %s", g_gnss_ver.ext[i]);
        }
    } else {
        ESP_LOGW(TAG, "MON-VER: no response");
    }

    /* PHASE B2: read current config BEFORE changing anything. */
    ESP_LOGI(TAG, "=== PHASE B: current configuration (CFG-VALGET) ===");
    if (ubx_query_config(&g_gnss_cfg) == 0) {
        print_config_summary(&g_gnss_cfg);
    } else {
        ESP_LOGW(TAG, "CFG-VALGET failed/timeout");
    }

    /* PHASE C: enable NAV-PVT output, but only if not already enabled
     * (after our earlier test it may already be on in the RAM layer). */
    if (g_gnss_cfg.valid && g_gnss_cfg.pvt_rate > 0) {
        ESP_LOGI(TAG, "NAV-PVT already enabled (rate=%u), skipping VALSET",
                 g_gnss_cfg.pvt_rate);
    } else {
        ESP_LOGI(TAG, "=== PHASE C: enabling NAV-PVT @ 1 Hz (RAM, NMEA kept) ===");
        if (ubx_enable_nav_pvt() == 0) {
            ESP_LOGI(TAG, "NAV-PVT output enabled on UART1 (RAM layer, ACK-ACK received)");
        } else {
            ESP_LOGE(TAG, "Failed to enable NAV-PVT (NAK/timeout)");
        }
    }

    /* PHASE D: wait up to 30 s for the first decoded NAV-PVT. */
    ESP_LOGI(TAG, "Waiting for first NAV-PVT frames...");
    for (int i = 0; i < 300; i++) {
        if (s_sol.pvt_seen) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (s_sol.pvt_seen) {
        ESP_LOGI(TAG, "=== PHASE D: NAV-PVT decoding active ===");
    } else {
        ESP_LOGW(TAG, "No NAV-PVT received yet (no fix yet, or UBX output blocked)");
    }

    /* PHASE J groundwork: enable NAV-SAT for antenna-placement diagnostics.
     * The receiver emits it once per navigation epoch (1 Hz today); our
     * console code below only logs it ~every 10 s, so it stays a quiet
     * background diagnostic. No other configuration is touched. */
    ESP_LOGI(TAG, "=== PHASE J: enabling NAV-SAT (RAM, 1 Hz equivalent) ===");
    if (g_gnss_cfg.valid && g_gnss_cfg.sat_rate > 0) {
        ESP_LOGI(TAG, "NAV-SAT already enabled (rate=%u), skipping VALSET",
                 g_gnss_cfg.sat_rate);
    } else if (ubx_enable_nav_sat() == 0) {
        ESP_LOGI(TAG, "NAV-SAT output enabled on UART1 (RAM layer, ACK-ACK received)");
    } else {
        ESP_LOGW(TAG, "Failed to enable NAV-SAT (NAK/timeout) - continuing without it");
    }
}

/* The low-priority companion of gnss_rx; dies after the sequence finishes. */
static void init_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500));   /* let gnss_rx see the first bytes */
    startup_sequence();
    vTaskDelete(NULL);                /* self-delete: sequence runs once */
}

/* ---------------- gnss_rx task: the ONLY UART consumer ---------------- */

static void gnss_task(void *arg)
{
    uint8_t buf[512];                 /* one UART read chunk */
    bool logged_empty = false;
    uint32_t last_stats_ms = 0;

    ESP_LOGI(TAG, "GNSS RX task running: UART%d %d baud 8N1 TX=%d RX=%d",
             GNSS_UART_NUM, GNSS_UART_BAUD, GNSS_UART_TX_PIN, GNSS_UART_RX_PIN);

    for (;;) {
        /*
         * The read. Blocks up to 200 ms waiting for data, then returns:
         *   >0  number of bytes read  -> feed the parser
         *    0  timeout, no bytes     -> check the "dead link" warning
         *   <0  driver error          -> log and pause
         * CRITICAL: nothing else in this loop may wait on responses - all
         * command/response synchronization happens in other tasks via
         * notifications, so UART reception never stalls.
         */
        int len = uart_read_bytes(GNSS_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(200));
        if (len > 0) {
            ubx_parser_feed(&s_parser, buf, (size_t)len);
        } else if (len == 0) {
            if (!logged_empty && s_parser.rx_bytes == 0) {
                ESP_LOGW(TAG, "No GNSS data yet. Check module T(TX) -> ESP GPIO%d.", GNSS_UART_RX_PIN);
                logged_empty = true;
            }
        } else {
            ESP_LOGE(TAG, "uart_read_bytes error: %d", len);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        /* Once per second: refresh parser stats and print a status summary
         * (never per-packet: at 20 Hz that would flood the console).
         * Every 10th tick: one line of satellite/signal diagnostics from
         * the last NAV-SAT (used for antenna placement experiments). */
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (now - last_stats_ms >= 1000) {
            last_stats_ms = now;
            ubx_parser_get_stats(&s_parser, &s_sol.parser);
            gnss_solution_t snap;
            gnss_get_solution(&snap);
            if (snap.pvt_seen) {
                float hz = itow_stats_measured_hz(&snap.itow);
                bool has_fix = (snap.pvt.fixType >= 2);   /* 2D or better */
                if (has_fix) {
                    ESP_LOGI(TAG,
                             "GNSS: fixType=%u fixOK=%d SV=%u gSpeed=%ld mm/s "
                             "hAcc=%lu vAcc=%lu sAcc=%lu mm/s pDOP=%u.%02u "
                             "iTOW=%lu rate=%.2f Hz dITOW=%lu | "
                             "UBX ok=%lu crc=%lu len=%lu resync=%lu NMEA=%lu",
                             snap.pvt.fixType, (snap.pvt.flags & 0x01) ? 1 : 0,
                             snap.pvt.numSV,
                             (long)snap.pvt.gSpeed,
                             (unsigned long)snap.pvt.hAcc,
                             (unsigned long)snap.pvt.vAcc,
                             (unsigned long)snap.pvt.sAcc,
                             snap.pvt.pDOP / 100, snap.pvt.pDOP % 100,
                             (unsigned long)snap.pvt.iTOW, hz,
                             (unsigned long)snap.itow.delta_iTOW,
                             (unsigned long)snap.parser.ubx_packets_ok,
                             (unsigned long)snap.parser.ubx_checksum_errors,
                             (unsigned long)snap.parser.ubx_length_errors,
                             (unsigned long)snap.parser.parser_resyncs,
                             (unsigned long)snap.parser.nmea_sentences);
                } else {
                    /* No fix: u-blox fills accuracy fields with growing
                     * "unknown" sentinels - don't present them as data. */
                    ESP_LOGI(TAG,
                             "GNSS: NO FIX SV=%u iTOW=%lu rate=%.2f Hz dITOW=%lu | "
                             "UBX ok=%lu crc=%lu len=%lu resync=%lu NMEA=%lu",
                             snap.pvt.numSV,
                             (unsigned long)snap.pvt.iTOW, hz,
                             (unsigned long)snap.itow.delta_iTOW,
                             (unsigned long)snap.parser.ubx_packets_ok,
                             (unsigned long)snap.parser.ubx_checksum_errors,
                             (unsigned long)snap.parser.ubx_length_errors,
                             (unsigned long)snap.parser.parser_resyncs,
                             (unsigned long)snap.parser.nmea_sentences);
                }

                /* Satellite diagnostics: 1 summary line every 10 s.
                 * Format: per-constellation "used/visible", then C/N0 stats.
                 * This is the data that later tells us where to put the
                 * antenna in the car (which sky quadrant is productive). */
                s_sat_log_count++;
                if (s_sat_log_count >= 10 && s_sol.sat_seen) {
                    s_sat_log_count = 0;
                    const ubx_nav_sat_t *sat = &snap.sat;
                    char per[64];
                    size_t n = 0;
                    per[0] = '\0';
                    for (int g = 0; g < GNSS_ID_COUNT; g++) {
                        if (sat->vis_per_gnss[g] > 0) {
                            n += snprintf(per + n, sizeof(per) - n, "%s%s%u/%u",
                                          n ? " " : "", gnss_id_name((uint8_t)g),
                                          sat->used_per_gnss[g],
                                          sat->vis_per_gnss[g]);
                        }
                    }
                    ESP_LOGI(TAG, "SAT: vis=%u used=%u avgCNO=%u maxCNO=%d [%s]",
                             sat->visible, sat->used,
                             (unsigned)nav_sat_avg_cno_used(sat),
                             sat->max_cno, per);
                }
            }
        }
    }
}

/*
 * gnss_start - UART hardware setup + task creation. Called from app_main.
 *
 * UART setup, in order:
 *   1. uart_param_config : baud/parity/stop bits/clock source
 *   2. uart_set_pin      : which GPIOs are TX/RX (CTS/RTS unused)
 *   3. uart_driver_install : allocates the driver's RX ring buffer
 *      (4 KB here). From this point the UART hardware + driver receive
 *      bytes INTO THE RING BUFFER using interrupts, even when no task
 *      is calling uart_read_bytes - an extra safety layer for GNSS data.
 */
int gnss_start(void)
{
    uart_config_t cfg = {
        .baud_rate = GNSS_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(GNSS_UART_NUM, &cfg);
    if (err != ESP_OK) return err;
    err = uart_set_pin(GNSS_UART_NUM, GNSS_UART_TX_PIN, GNSS_UART_RX_PIN,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) return err;
    err = uart_driver_install(GNSS_UART_NUM, GNSS_UART_BUF_SIZE, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    memset(&s_sol, 0, sizeof(s_sol));
    ubx_config_init();                                   /* resp ctx + globals */
    ubx_parser_init(&s_parser, on_ubx, on_nmea, NULL);   /* register dispatcher */

    BaseType_t ok = xTaskCreate(gnss_task, "gnss_rx", 6144, NULL, 10, NULL);
    if (ok != pdTRUE) return ESP_FAIL;

    /* Separate init task: waits on notifications while gnss_rx services UART. */
    ok = xTaskCreate(init_task, "gnss_init", 6144, NULL, 5, NULL);
    return (ok == pdTRUE) ? ESP_OK : ESP_FAIL;
}