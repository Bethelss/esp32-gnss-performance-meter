/*
 * ESP32-S3 GNSS logger - iteration 2: UBX-NAV-PVT + configuration.
 * PHASES A-D implemented; hardware checkpoint before baud change (PHASE E).
 *
 * ===================== NEW TO ESP-IDF? READ THIS =====================
 *
 * HOW AN ESP32 PROGRAM WORKS (vs. Arduino):
 * - There is no loop() and no setup(). Instead, the system boots, then
 *   calls app_main() ONCE on one CPU core. app_main() must NOT run forever:
 *   it sets things up and returns. Everything else runs in "tasks".
 * - A "task" is like a thread: an independent function running forever
 *   (usually a while(1) loop), scheduled by the FreeRTOS real-time
 *   operating system. The ESP32-S3 has 2 cores; FreeRTOS decides which
 *   task runs on which core and for how long.
 * - Tasks are created with xTaskCreate(function, name, stack_size, ...).
 *   Stack size is in BYTES (unlike some RTOSes that use words).
 * - "Priority" decides who wins the CPU when several tasks want it.
 *   Higher number = higher priority. Our GNSS reception task has priority
 *   10 so it always beats the display task (priority 5).
 *
 * LOGGING:
 * - ESP_LOGI(tag, fmt, ...) prints an Info line over the USB serial link
 *   to your PC terminal. There are also ESP_LOGW (warning) and ESP_LOGE
 *   (error). The "tag" is a short label (e.g. "main") shown before each
 *   line so you can see which module printed it.
 *
 * TIMERS:
 * - esp_timer_get_time() returns microseconds since boot (64-bit).
 *   It is safe to call from interrupt handlers (ISRs) and is monotonic.
 * - vTaskDelay(pdMS_TO_TICKS(150)) makes the CURRENT task sleep for 150 ms
 *   so other tasks (and the RTOS idle housekeeping) can run. Never busy-
 *   wait with a loop - always sleep.
 *
 * INTERRUPTS (the PPS input):
 * - GPIO4 is wired to the GNSS module's "P" pin, which pulses once per
 *   second when the receiver has a time fix. We want to react within
 *   microseconds, so instead of polling we use a hardware interrupt:
 *   the CPU jumps to our special function (the ISR) on every rising edge.
 * - ISR rules: an interrupt handler must be as SHORT as possible (it
 *   preempts everything), must live in fast internal RAM (IRAM_ATTR),
 *   and must not call normal (blocking) FreeRTOS or printf functions.
 *   We only record a timestamp and bump counters here.
 *
 * FILE OVERVIEW:
 * - main.c          (this file): boot, diagnostics, PPS ISR, display task
 * - gnss/gnss.c     UART reception task + UBX frame dispatcher
 * - gnss/ubx_parser.c   byte-stream state machine for UBX/NMEA
 * - gnss/ubx_protocol.c UBX packet TX + ACK/response synchronization
 * - gnss/ubx_config.c   MON-VER / CFG-VALGET / NAV-PVT decode, key IDs
 * - display.c       SH1106/SSD1306 OLED over I2C, 3 rotating pages
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"   /* base RTOS types, TickType_t, pdMS_TO_TICKS */
#include "freertos/task.h"       /* xTaskCreate, vTaskDelay, TaskHandle_t */
#include "esp_log.h"             /* ESP_LOGI / ESP_LOGW / ESP_LOGE */
#include "esp_system.h"          /* basic system info */
#include "esp_chip_info.h"       /* chip model/revision/cores */
#include "esp_flash.h"           /* query external flash size */
#include "esp_timer.h"           /* esp_timer_get_time() - us since boot */
#include "esp_psram.h"           /* PSRAM (external RAM on the module) status */
#include "driver/gpio.h"         /* GPIO configuration + interrupts */
#include "sdkconfig.h"           /* generated from sdkconfig / menuconfig */

#include "gnss/gnss.h"           /* GNSS subsystem: UART task, solution state */
#include "display.h"             /* OLED display: init + 3 status pages */

static const char *TAG = "main"; /* every log line from this file is prefixed "main" */

#define PPS_GPIO        GPIO_NUM_4
#define FIRMWARE_VER    "v0.2-ubx"

/*
 * PPS bookkeeping. The ISR writes these; the display task reads them.
 * "volatile" tells the compiler that these variables can change at any
 * moment (from the interrupt), so it must not cache them in registers.
 *
 * GAP-AWARE PPS ACCOUNTING (iteration 2.3):
 * A healthy timepulse has a period of almost exactly 1,000,000 us.
 * If the receiver loses time (no fix yet, warm-up, restart) it may pause
 * the pulse or emit sparse pulses; naive min/max then records nonsense
 * like "period 9 s" as if that were a measurement. Instead, for every
 * interval we count how many WHOLE SECONDS it spans and charge that to
 * s_pps_missed. Example: pulses at t=0s, 1s, then nothing until t=4s ->
 * the 3-second gap means 2 missed pulses (the pulse at t=2s and t=3s
 * never arrived). "PPS: OK" on the display additionally requires that
 * the last period was close to one second - so a receiver that pulses
 * once every 9 seconds will show as intermittent, not healthy.
 */
static volatile uint32_t s_pps_count;        /* total rising edges seen */
static volatile int64_t  s_pps_last_us;      /* esp_timer time of last edge */
static volatile uint32_t s_pps_period_us;    /* period between last two edges */
static volatile uint32_t s_pps_period_min;   /* shortest gap-free period */
static volatile uint32_t s_pps_period_max;   /* longest gap-free period */
static volatile uint32_t s_pps_missed;       /* pulses missing inside gaps */
static volatile uint32_t s_pps_gap_count;    /* gaps (any interval != 1 pulse) */

/* Tolerance around 1 s for "healthy" and "gap-free" classification.
 * The timepulse has ~tens of ns of jitter; 50 ms is generous. */
#define PPS_NOMINAL_US   1000000u
#define PPS_TOL_US         50000u

/*
 * PPS INTERRUPT HANDLER ("ISR").
 *
 * Runs automatically every time GPIO4 goes from low to high (a rising
 * edge, i.e. once per second when the receiver has time). Rules observed:
 * - IRAM_ATTR: puts the code in fast internal RAM so it works even when
 *   flash is briefly unavailable (e.g. during flash writes).
 * - We only do integer math, no logging, no allocation, no delays.
 * - esp_timer_get_time() is one of the few APIs that are ISR-safe.
 */
static void IRAM_ATTR pps_isr(void *arg)
{
    uint32_t now = (uint32_t)esp_timer_get_time();
    if (s_pps_last_us) {
        uint32_t period = now - (uint32_t)s_pps_last_us;
        s_pps_period_us = period;

        /* Classify the interval:
         *   - within tolerance of 1 s  -> healthy epoch, update min/max
         *   - a whole number of seconds, N >= 2 -> N-1 pulses were missed
         *   - anything else (e.g. 1.4 s)       -> count as one missed pulse
         */
        if (period >= PPS_NOMINAL_US - PPS_TOL_US &&
            period <= PPS_NOMINAL_US + PPS_TOL_US) {
            if (s_pps_period_min == 0 || period < s_pps_period_min) s_pps_period_min = period;
            if (period > s_pps_period_max) s_pps_period_max = period;
        } else {
            s_pps_gap_count++;
            if (period < 2 * PPS_NOMINAL_US - PPS_TOL_US) {
                s_pps_missed++;                       /* irregular single gap */
            } else {
                /* round to nearest whole second: 2s+ = 1+ missing pulses */
                s_pps_missed += (period + PPS_NOMINAL_US / 2) / PPS_NOMINAL_US - 1;
            }
        }
    }
    s_pps_last_us = now;
    s_pps_count++;
}

/* Print firmware name/version and hardware facts to the serial console. */
static void print_diagnostics(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);                      /* fills model/revision/cores */
    uint32_t flash_sz = 0;
    esp_flash_get_size(NULL, &flash_sz);       /* bytes of external flash */

    ESP_LOGI(TAG, "=== GNSS LOGGER %s ===", FIRMWARE_VER);
    ESP_LOGI(TAG, "Chip: %s rev %d, cores %d",
             chip.model == CHIP_ESP32S3 ? "ESP32-S3" : "other", chip.revision, chip.cores);
    ESP_LOGI(TAG, "Flash: %lu MB", (unsigned long)(flash_sz / (1024 * 1024)));
    ESP_LOGI(TAG, "PSRAM: %s", esp_psram_is_initialized() ? "OK" : "NOT DETECTED");
    ESP_LOGI(TAG, "GNSS UART: UART%d, %d baud 8N1, TX=GPIO%d RX=GPIO%d",
             GNSS_UART_NUM, GNSS_UART_BAUD, GNSS_UART_TX_PIN, GNSS_UART_RX_PIN);
    ESP_LOGI(TAG, "I2C: SDA=GPIO%d SCL=GPIO%d @ %d kHz",
             OLED_I2C_SDA_PIN, OLED_I2C_SCL_PIN, OLED_I2C_FREQ_HZ / 1000);
    ESP_LOGI(TAG, "PPS: GPIO%d, rising edge", PPS_GPIO);
}

/* Configure GPIO4 as PPS input with a rising-edge interrupt. */
static void pps_init(void)
{
    /* gpio_config_t describes one or more pins in one call. */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PPS_GPIO,   /* bitmask of pins to configure */
        .mode = GPIO_MODE_INPUT,            /* we only listen, never drive */
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE, /* define a level when idle */
        .intr_type = GPIO_INTR_POSEDGE,     /* interrupt on rising edge */
    };
    gpio_config(&io);

    /* One global "ISR service" must be installed once, then each pin gets
     * a handler. The service routes the hardware interrupt to our C function. */
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PPS_GPIO, pps_isr, NULL);
    ESP_LOGI(TAG, "PPS input ready on GPIO%d", PPS_GPIO);
}

/*
 * DISPLAY TASK.
 * - Owns the OLED. Everything else only fills in data structures; this
 *   task is the only one that touches I2C, so no locking of the display
 *   is needed.
 * - Reads a snapshot of the GNSS solution (mutex-protected copy) and the
 *   PPS counters, then draws one of 4 pages. Rotates the page every 4 s.
 * - Refresh ~7 Hz: 150 ms sleep. Slow on purpose: the I2C bus is shared
 *   bandwidth-wise only in time, the GNSS UART is a separate peripheral,
 *   so slow drawing can never lose GNSS bytes.
 */
static void display_task(void *arg)
{
    oled_info_t info;      /* what display_init() discovered (addr, ctrl) */
    gnss_solution_t sol;   /* snapshot container, filled by gnss_get_solution() */

    int err = display_init(&info);
    if (err != ESP_OK) {
        /* Display failure is not fatal: keep logging to the console. */
        ESP_LOGE(TAG, "Display init failed (err=%d). Continuing without OLED.", err);
        vTaskDelete(NULL);   /* tasks delete themselves with vTaskDelete(NULL) */
        return;
    }

    display_show_startup(&info);
    vTaskDelay(pdMS_TO_TICKS(2000));   /* show the banner for 2 seconds */

    int page = 0;
    uint32_t page_since = 0;           /* ms timestamp of last page switch */
    uint32_t last_pps_logged = 0;
    uint32_t last_missed_logged = 0;

    for (;;) {
        /* Copy the shared GNSS state under its mutex (see gnss.c). */
        gnss_get_solution(&sol);
        uint32_t cnt = s_pps_count;
        uint32_t per = s_pps_period_us;
        uint32_t missed = s_pps_missed;
        /* PPS is "healthy" only if pulses are currently arriving AND the
         * last interval was ~1 s (a 9 s sparse pulse is not healthy). */
        bool pps_ok = (cnt > 0) &&
                      (per >= PPS_NOMINAL_US - PPS_TOL_US) &&
                      (per <= PPS_NOMINAL_US + PPS_TOL_US);

        /* Log each new PPS edge to the console with its period, and warn
         * whenever the missed-pulse counter grows (gap detected). */
        if (cnt != last_pps_logged) {
            ESP_LOGI(TAG, "PPS edge #%lu, period %lu us (min %lu us)",
                     (unsigned long)cnt, (unsigned long)per,
                     (unsigned long)s_pps_period_min);
            last_pps_logged = cnt;
        }
        if (missed != last_missed_logged) {
            ESP_LOGW(TAG, "PPS gaps: %lu pulses missed in %lu gaps (total edges %lu)",
                     (unsigned long)missed, (unsigned long)s_pps_gap_count,
                     (unsigned long)cnt);
            last_missed_logged = missed;
        }

        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (now - page_since >= 4000) {   /* rotate pages every 4 s */
            page++;
            page_since = now;
        }

        display_show_page(page, &sol, cnt, per, missed, pps_ok,
                          g_gnss_ver.sw_version);
        vTaskDelay(pdMS_TO_TICKS(150));   /* ~7 Hz refresh */
    }
}

/*
 * app_main: the entry point. Set everything up, then RETURN - do not loop
 * here. Returning is normal in ESP-IDF; FreeRTOS keeps running the tasks
 * we created.
 */
void app_main(void)
{
    print_diagnostics();
    pps_init();

    /* gnss_start(): sets up UART1 (38400 8N1) and creates:
     *   - "gnss_rx"  task (priority 10): the ONLY UART consumer; reads
     *     bytes, feeds the UBX/NMEA parser, dispatches frames, never blocks
     *   - "gnss_init" (priority 5): startup sequence (MON-VER, CFG-VALGET,
     *     enable NAV-PVT) that WAITS on task notifications while gnss_rx
     *     keeps servicing the UART. See gnss.c for the full explanation. */
    int err = gnss_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GNSS UART init failed: %d", err);
    }

    /* display task at lower priority: it may be starved briefly by GNSS
     * work, which is exactly what we want (GNSS never waits for OLED). */
    xTaskCreate(display_task, "display", 4096, NULL, 5, NULL);
}