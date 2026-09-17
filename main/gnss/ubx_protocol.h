#pragma once
/*
 * UBX transmission + response synchronization.
 *
 * ===================== WHY THIS EXISTS =====================
 *
 * The GNSS receiver speaks two languages on one wire:
 *  - NMEA: human-readable text lines ("$GNGGA,...")
 *  - UBX:  u-blox's binary protocol (compact, checksummed)
 * We SPEAK UBX to the receiver (send commands like "enable NAV-PVT"),
 * and the receiver answers with:
 *  - UBX-ACK-ACK  (class 0x05 id 0x01, payload = the class/id it accepted)
 *  - UBX-ACK-NAK  (class 0x05 id 0x00, payload = the class/id it REJECTED)
 *  - or a direct response message (MON-VER, CFG-VALGET).
 *
 * THE GOLDEN RULE (learned the hard way in testing):
 * exactly ONE consumer of the UART exists - the gnss_rx task feeding the
 * parser. Nothing here ever reads the UART itself. A task that wants a
 * response:
 *   1. ARMS the expectation BEFORE sending (so an early reply can't be missed)
 *   2. sends the command
 *   3. SLEEPS in xTaskNotifyWait() - this costs no CPU and, crucially,
 *      does NOT stop the gnss_rx task from reading the UART
 *   4. the dispatcher (running in gnss_rx) sees the matching frame and
 *      wakes the waiter with xTaskNotify()
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ubx_parser.h"

/* Possible outcomes of "send a command and wait". */
typedef enum {
    UBX_RESP_NONE = 0,
    UBX_RESP_ACK,        /* ACK-ACK for the armed class/id */
    UBX_RESP_NAK,        /* ACK-NAK for the armed class/id */
    UBX_RESP_MONVER,     /* MON-VER response frame arrived */
    UBX_RESP_VALGET,     /* CFG-VALGET response frame arrived */
    UBX_RESP_TIMEOUT,    /* nothing matched within the timeout */
    UBX_RESP_ERR,
} ubx_resp_t;

/*
 * One shared "response slot". Only one command-response exchange happens
 * at a time in this firmware, so a single context is enough.
 *
 * Task notification in one paragraph: every FreeRTOS task carries a 32-bit
 * "notification" - a tiny, fast mailbox. xTaskNotify(handle, value, ...)
 * delivers a value and unblocks the receiving task;
 * xTaskNotifyWait(...) sleeps until something arrives (or a timeout).
 * It is the lightest wakeup primitive in FreeRTOS - lighter than queues
 * or event groups - and is what we use for command/response sync.
 */
typedef struct {
    volatile TaskHandle_t waiter;    /* which task is waiting (set on arm) */
    volatile bool     armed;         /* true between arm and dispatch/timeout */
    volatile uint8_t  exp_cls;       /* expected class for ACK matching */
    volatile uint8_t  exp_id;        /* expected id for ACK matching */
    volatile ubx_resp_t result;      /* what the dispatcher decided */
} ubx_resp_ctx_t;

void ubx_resp_init(ubx_resp_ctx_t *ctx);

/* Arm BEFORE sending the command. Records the calling task as waiter. */
void ubx_resp_arm(ubx_resp_ctx_t *ctx, uint8_t exp_cls, uint8_t exp_id);

/* Block until dispatcher fires or timeout. Returns result. */
ubx_resp_t ubx_resp_wait(ubx_resp_ctx_t *ctx, uint32_t timeout_ms);

/* Called by dispatcher (gnss_rx context) to deliver a result. */
void ubx_resp_dispatch(ubx_resp_ctx_t *ctx, ubx_resp_t r);

/* Feed an incoming frame; returns true if consumed as an ACK/NAK.
 * Called from the gnss_rx dispatcher. */
bool ubx_ack_consume(ubx_resp_ctx_t *ctx, const ubx_frame_t *f);

/* Send a complete UBX frame. Returns 0 or error. */
int ubx_send(uint8_t cls, uint8_t id, const void *payload, uint16_t len);