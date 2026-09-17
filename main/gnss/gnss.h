#pragma once
/*
 * GNSS subsystem (iteration 2): UBX-first.
 *
 * ===================== ARCHITECTURE (READ THIS) =====================
 *
 * Exactly ONE task reads the UART. This is the rule that everything else
 * is built around (violating it caused a real bug: responses timed out
 * because the waiting code had stalled the only reader).
 *
 *   gnss_rx task (priority 10 - the ONLY UART consumer, never blocks on
 *   anything except uart_read_bytes itself):
 *
 *     uart_read_bytes() -> ubx_parser_feed() -> on_ubx dispatcher:
 *       UBX-ACK-ACK/NAK     -> ubx_resp_dispatch()  (wakes gnss_init)
 *       UBX-MON-VER         -> store + dispatch     (wakes gnss_init)
 *       UBX-CFG-VALGET      -> store + dispatch     (wakes gnss_init)
 *       UBX-NAV-PVT         -> decode -> gnss_solution (mutex-protected)
 *       UBX-NAV-SAT         -> satellite statistics
 *
 *   gnss_init task (priority 5): runs the startup sequence (MON-VER,
 *   CFG-VALGET, enable NAV-PVT). It WAITS on task notifications while
 *   gnss_rx keeps servicing the UART - the two never block each other.
 *
 *   display task (in main.c, priority 5): reads a snapshot of
 *   gnss_solution under the mutex and draws the OLED. It never touches
 *   the UART, so it can never lose GNSS bytes.
 *
 * SHARED STATE: gnss_solution is guarded by a mutex (s_lock). Writers
 * (gnss_rx) take it briefly to swap in new values; readers (display,
 * 1 Hz console log) take it briefly to copy the whole struct out. This
 * "copy under lock" pattern avoids holding the lock while drawing.
 */

#include <stdint.h>
#include <stdbool.h>
#include "driver/uart.h"
#include "ubx_parser.h"
#include "ubx_protocol.h"
#include "ubx_config.h"

#define GNSS_UART_NUM       UART_NUM_1   /* UART0 = console, UART1 = GNSS */
#define GNSS_UART_BAUD      38400
#define GNSS_UART_TX_PIN    17           /* ESP TX  -> module R (RX) */
#define GNSS_UART_RX_PIN    18           /* module T (TX) -> ESP RX */
#define GNSS_UART_BUF_SIZE  4096         /* driver RX ring buffer (bytes) */

/* The most recent GNSS knowledge, shared between tasks. */
typedef struct {
    ubx_nav_pvt_t   pvt;            /* last decoded NAV-PVT */
    bool            pvt_seen;       /* any NAV-PVT ever received */
    uint32_t        pvt_count;      /* how many */
    ubx_nav_sat_t   sat;            /* last NAV-SAT summary */
    bool            sat_seen;
    itow_stats_t    itow;           /* measured navigation rate */
    ubx_parser_stats_t parser;      /* refreshed once per second */
} gnss_solution_t;

int gnss_start(void);
void gnss_get_solution(gnss_solution_t *out);