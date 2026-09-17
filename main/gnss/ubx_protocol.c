#include "ubx_protocol.h"

#include <string.h>
#include "driver/uart.h"     /* uart_write_bytes, uart_wait_tx_done */
#include "esp_log.h"
#include "gnss.h"            /* GNSS_UART_NUM: which UART the GNSS module is on */

static const char *TAG = "ubx";

/* ---------------------------------------------------------------
 * Response context (the "who is waiting for what" slot)
 * --------------------------------------------------------------- */

void ubx_resp_init(ubx_resp_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
}

/*
 * ubx_resp_arm - announce "I will wait for a reply concerning (cls,id)".
 *
 * MUST be called BEFORE the command is sent. Why: the receiver can reply
 * in under a millisecond. If we sent first and armed afterwards, the
 * reply could already be parsed and lost before anyone starts listening.
 * Arming first makes the window airtight.
 *
 * exp_cls/exp_id: what frame would satisfy this wait:
 *   - for VALSET waits:        0x06 0x8A (so ACK/NAK payload must match)
 *   - for MON-VER waits:       0x0A 0x04 (the response frame itself)
 *   - for CFG-VALGET waits:    0x06 0x8B (the response frame itself)
 */
void ubx_resp_arm(ubx_resp_ctx_t *ctx, uint8_t exp_cls, uint8_t exp_id)
{
    ctx->exp_cls = exp_cls;
    ctx->exp_id = exp_id;
    ctx->result = UBX_RESP_NONE;
    ctx->waiter = xTaskGetCurrentTaskHandle();  /* remember who waits */
    ctx->armed = true;
}

/*
 * ubx_resp_dispatch - called from the gnss_rx dispatcher when the frame
 * the waiter was hoping for has arrived. Wakes the waiting task.
 */
void ubx_resp_dispatch(ubx_resp_ctx_t *ctx, ubx_resp_t r)
{
    if (!ctx->armed) return;        /* nobody is listening; drop */
    ctx->result = r;
    ctx->armed = false;
    if (ctx->waiter) {
        /* eSetValueWithOverwrite: store r as the notification value and
         * unblock the waiter even if it already had a pending value. */
        xTaskNotify(ctx->waiter, (uint32_t)r, eSetValueWithOverwrite);
    }
}

/*
 * ubx_resp_wait - the sleeping half of the handshake.
 *
 * Loops on xTaskNotifyWait until either:
 *  - the dispatcher fires (armed becomes false) -> return its result, or
 *  - timeout_ms elapses -> declare UBX_RESP_TIMEOUT and disarm.
 *
 * While we sleep here, the gnss_rx task keeps reading the UART: this is
 * exactly why the "blocking wait inside the RX task" bug is gone.
 */
ubx_resp_t ubx_resp_wait(ubx_resp_ctx_t *ctx, uint32_t timeout_ms)
{
    TickType_t t0 = xTaskGetTickCount();      /* RTOS ticks since boot */
    TickType_t limit = pdMS_TO_TICKS(timeout_ms);

    while (ctx->armed) {
        uint32_t val = 0;
        TickType_t elapsed = xTaskGetTickCount() - t0;
        TickType_t rem = (elapsed < limit) ? (limit - elapsed) : 0;
        if (rem == 0 ||
            xTaskNotifyWait(0, 0xFFFFFFFFu, &val, rem) != pdTRUE) {
            if (ctx->armed) {
                ctx->armed = false;
                ESP_LOGW(TAG, "response timeout (armed cls=%02X id=%02X)",
                         ctx->exp_cls, ctx->exp_id);
                return UBX_RESP_TIMEOUT;
            }
            break;   /* dispatched between checks */
        }
        if (!ctx->armed) return (ubx_resp_t)val;
    }
    return ctx->result;
}

/*
 * ubx_ack_consume - dispatcher helper, inspects an incoming UBX frame:
 *   - if it is an ACK-ACK / ACK-NAK for the ARMED class/id: wake the
 *     waiter with ACK or NAK and report "consumed" (true), so the frame
 *     is not processed anywhere else.
 *   - ACKs for other commands (or while nothing is armed) are logged at
 *     debug level and also swallowed - they belong to nobody else.
 *   - anything that is not an ACK frame (cls != 0x05, len != 2) returns
 *     false so the dispatcher continues to the real handlers.
 */
bool ubx_ack_consume(ubx_resp_ctx_t *ctx, const ubx_frame_t *f)
{
    if (f->cls != 0x05 || f->len != 2) return false;

    uint8_t acked_cls = f->payload[0];
    uint8_t acked_id  = f->payload[1];

    if (!ctx->armed) {
        ESP_LOGD(TAG, "unsolicited ACK %02X %02X", acked_cls, acked_id);
        return true;
    }
    if (acked_cls != ctx->exp_cls || acked_id != ctx->exp_id) {
        /* e.g. ACK for a previous command arriving late - not ours. */
        ESP_LOGD(TAG, "ACK for %02X %02X while waiting for %02X %02X",
                 acked_cls, acked_id, ctx->exp_cls, ctx->exp_id);
        return true;
    }

    ubx_resp_dispatch(ctx, (f->id == 0x01) ? UBX_RESP_ACK : UBX_RESP_NAK);
    ESP_LOGI(TAG, "%s for %02X %02X",
             (f->id == 0x01) ? "ACK-ACK" : "ACK-NAK", acked_cls, acked_id);
    return true;
}

/*
 * ubx_send - transmit one UBX frame over the GNSS UART.
 *
 * UBX wire format:
 *   0xB5 0x62 | CLASS | ID | LEN_L LEN_H | payload... | CK_A CK_B
 * - length is 16-bit LITTLE-ENDIAN (low byte first)
 * - checksum is Fletcher-16 over class,id,length,payload (not sync bytes)
 *
 * uart_write_bytes() copies into the UART driver's TX buffer; it does not
 * block for long. uart_wait_tx_done() then sleeps until the hardware has
 * actually shifted the bits out, so we never overwrite the TX buffer with
 * a second frame too quickly.
 */
int ubx_send(uint8_t cls, uint8_t id, const void *payload, uint16_t len)
{
    uint8_t buf[8 + UBX_MAX_PAYLOAD];
    if (len > UBX_MAX_PAYLOAD) return -1;

    uint8_t ck_a = 0, ck_b = 0;
    size_t n = 0;
    buf[n++] = 0xB5;
    buf[n++] = 0x62;
    buf[n++] = cls; ubx_checksum_update(&ck_a, &ck_b, cls);
    buf[n++] = id;  ubx_checksum_update(&ck_a, &ck_b, id);
    buf[n++] = (uint8_t)(len & 0xFF);        ubx_checksum_update(&ck_a, &ck_b, buf[n-1]);
    buf[n++] = (uint8_t)((len >> 8) & 0xFF); ubx_checksum_update(&ck_a, &ck_b, buf[n-1]);
    const uint8_t *p = (const uint8_t *)payload;
    for (uint16_t i = 0; i < len; i++) {
        buf[n++] = p[i];
        ubx_checksum_update(&ck_a, &ck_b, p[i]);
    }
    buf[n++] = ck_a;
    buf[n++] = ck_b;

    int written = uart_write_bytes(GNSS_UART_NUM, buf, n);
    if (written < 0) return written;
    uart_wait_tx_done(GNSS_UART_NUM, pdMS_TO_TICKS(200));
    ESP_LOGD(TAG, "TX %02X %02X len=%u", cls, id, (unsigned)len);
    return 0;
}