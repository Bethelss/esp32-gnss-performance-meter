#include "ubx_parser.h"

#include <string.h>

/*
 * Fletcher-16 checksum, the one u-blox uses.
 * Two running sums: CK_A accumulates raw bytes, CK_B accumulates CK_A.
 * Both start at 0 and cover class+id+length+payload (NOT the 0xB5 0x62).
 */
void ubx_checksum_update(uint8_t *ck_a, uint8_t *ck_b, uint8_t byte)
{
    *ck_a = (uint8_t)(*ck_a + byte);
    *ck_b = (uint8_t)(*ck_b + *ck_a);
}

void ubx_parser_init(ubx_parser_t *p,
                     ubx_frame_cb_t on_ubx, nmea_line_cb_t on_nmea, void *user)
{
    memset(p, 0, sizeof(*p));
    p->on_ubx = on_ubx;
    p->on_nmea = on_nmea;
    p->user = user;
}

/*
 * ubx_emit - the moment a full frame (payload + both checksum bytes) is in.
 * Compares the two received checksum bytes with our running calculation:
 *   match   -> count as good, deliver to the on_ubx callback
 *   mismatch-> count a checksum error and silently drop (recovery = the
 *              machine simply returns to idle and scans for the next 0xB5)
 * The two checksum bytes were stored just past the payload, which is why
 * payload[] is sized UBX_MAX_PAYLOAD and the parser rejects len > MAX-2.
 */
static void ubx_emit(ubx_parser_t *p)
{
    uint8_t rx_a = p->payload[p->len];
    uint8_t rx_b = p->payload[p->len + 1];

    if (rx_a == p->ck_a && rx_b == p->ck_b) {
        p->ubx_packets_ok++;
        p->st = 0;
        if (p->on_ubx) {
            /* Copy into a local frame so the callback receives a clean,
             * self-contained object (and parser state can keep running). */
            ubx_frame_t f;
            f.cls = p->cls;
            f.id = p->id;
            f.len = p->len;
            memcpy(f.payload, p->payload, p->len);
            p->on_ubx(&f, p->user);
        }
    } else {
        p->ubx_checksum_errors++;
        p->st = 0;
    }
}

/*
 * ubx_parser_feed - the heart of the module.
 *
 * Called with whatever uart_read_bytes() happened to return: could be
 * 2 bytes of one packet, 300 bytes of three packets, NMEA text, garbage.
 * The for-loop walks every byte through the same state machine, so
 * chunking is automatically handled.
 */
void ubx_parser_feed(ubx_parser_t *p, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        p->rx_bytes++;

        switch (p->st) {
        case 0: /* idle: look for the start of something */
            if (b == 0xB5) {
                p->st = 1;             /* possible UBX sync */
            } else if (b == '$') {
                p->nmea_len = 0;       /* definite NMEA sentence start */
                p->st = 10;
            }
            break;

        case 1: /* saw 0xB5, need 0x62 to confirm UBX */
            if (b == 0x62) {
                p->st = 2;             /* confirmed: class byte follows */
                p->ck_a = p->ck_b = 0; /* checksum starts over cls..payload */
            } else {
                /* Not UBX - it was noise (or the 0xB5 was data inside an
                 * interrupted NMEA line). Restart scanning. */
                p->parser_resyncs++;
                p->st = (b == 0xB5) ? 1 : 0;
            }
            break;

        case 2: /* class byte */
            p->cls = b;
            ubx_checksum_update(&p->ck_a, &p->ck_b, b);
            p->st = 3;
            break;

        case 3: /* id */
            p->id = b;
            ubx_checksum_update(&p->ck_a, &p->ck_b, b);
            p->st = 4;
            break;

        case 4: /* length low byte (little-endian) */
            p->len = b;
            ubx_checksum_update(&p->ck_a, &p->ck_b, b);
            p->st = 5;
            break;

        case 5: /* length high byte */
            p->len |= (uint16_t)(b << 8);
            ubx_checksum_update(&p->ck_a, &p->ck_b, b);
            if (p->len > UBX_MAX_PAYLOAD - 2) {
                /* A real M10 message never exceeds a few hundred bytes of
                 * payload; anything bigger means we misread noise as sync. */
                p->ubx_length_errors++;
                p->parser_resyncs++;
                p->st = 0;
            } else {
                p->got = 0;
                p->rem = p->len;
                p->st = 6;
            }
            break;

        case 6: /* payload bytes */
            if (p->got < p->len) {
                ubx_checksum_update(&p->ck_a, &p->ck_b, b);
                p->payload[p->got++] = b;
            }
            if (p->got == p->len) p->st = 7;   /* checksum bytes follow */
            break;

        case 7: /* CK_A received, store it for later comparison */
            p->payload[p->len] = b;
            p->st = 8;
            break;

        case 8: /* CK_B received -> frame complete, verify and emit */
            p->payload[p->len + 1] = b;
            ubx_emit(p);
            break;

        case 10: /* inside an NMEA sentence */
            if (b == '\r' || b == '\n') {
                if (p->nmea_len > 0) {
                    p->nmea_buf[p->nmea_len] = '\0';
                    p->nmea_sentences++;
                    if (p->on_nmea) p->on_nmea(p->nmea_buf, p->user);
                }
                p->st = 0;
            } else if (b == 0xB5) {
                /* A UBX burst can start in the middle of an NMEA gap;
                 * hand control to the UBX machine. */
                p->st = 1;
                p->parser_resyncs++;
            } else if (p->nmea_len < sizeof(p->nmea_buf) - 1) {
                p->nmea_buf[p->nmea_len++] = (char)b;
            } else {
                p->st = 0;   /* overlong line: drop it, rescan */
            }
            break;

        default:
            p->st = 0;   /* should never happen; fail safe */
            break;
        }
    }
}

void ubx_parser_get_stats(const ubx_parser_t *p, ubx_parser_stats_t *out)
{
    out->rx_bytes = p->rx_bytes;
    out->ubx_packets_ok = p->ubx_packets_ok;
    out->ubx_checksum_errors = p->ubx_checksum_errors;
    out->ubx_length_errors = p->ubx_length_errors;
    out->nmea_sentences = p->nmea_sentences;
    out->parser_resyncs = p->parser_resyncs;
}