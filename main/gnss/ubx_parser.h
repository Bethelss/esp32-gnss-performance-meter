#pragma once
/*
 * Generic UBX byte-stream parser.
 *
 * ===================== WHAT IT DOES =====================
 *
 * The UART hands us arbitrary chunks of bytes: a packet may be split
 * across two uart_read_bytes() calls, three packets may arrive in one
 * chunk, NMEA text may be interleaved, and noise/garbage can appear at
 * any moment. This module is a STATE MACHINE that consumes one byte at
 * a time and produces:
 *   - complete, checksum-verified UBX frames (via on_ubx callback)
 *   - complete NMEA sentences              (via on_nmea callback)
 *
 * UBX framing on the wire:
 *   0xB5 0x62 | CLASS | ID | LEN_L LEN_H | PAYLOAD | CK_A CK_B
 *
 * The parser never allocates memory (all buffers live inside the struct)
 * and never blocks: feed it bytes, callbacks fire synchronously.
 *
 * STATES of the machine (field `st`):
 *   0  idle            - scanning for 0xB5 (UBX) or '$' (NMEA)
 *   1  saw 0xB5        - waiting for 0x62 to confirm UBX sync
 *   2  reading class   3  reading id   4/5 reading length (2 bytes LE)
 *   6  collecting payload bytes
 *   7  expect CK_A     8  expect CK_B -> verify -> emit -> back to 0
 *   10 inside an NMEA line (started with '$')
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Largest payload we accept. NAV-SAT with 40 SVs is ~488 bytes, so 512
 * covers everything we use. A length field beyond this is treated as
 * corrupted data and rejected (see ubx_length_errors counter). */
#define UBX_MAX_PAYLOAD   512

/* One complete, verified UBX frame handed to the callback. */
typedef struct {
    uint8_t  cls;                     /* message class  (e.g. 0x01 NAV) */
    uint8_t  id;                      /* message id     (e.g. 0x07 PVT) */
    uint16_t len;                     /* payload length */
    uint8_t  payload[UBX_MAX_PAYLOAD];
} ubx_frame_t;

/* Health counters required by the project spec (TASK.md STEP 1). */
typedef struct {
    uint32_t rx_bytes;             /* every byte that entered the parser */
    uint32_t ubx_packets_ok;       /* frames with valid checksum */
    uint32_t ubx_checksum_errors;  /* framing ok but checksum mismatch */
    uint32_t ubx_length_errors;    /* absurd length field rejected */
    uint32_t nmea_sentences;       /* complete NMEA lines */
    uint32_t parser_resyncs;       /* recoveries after garbage */
} ubx_parser_stats_t;

/* Callbacks (run in gnss_rx task context, right after a frame completes). */
typedef void (*ubx_frame_cb_t)(const ubx_frame_t *frame, void *user);
typedef void (*nmea_line_cb_t)(const char *line, void *user);

/*
 * The parser object. Kept as a struct (not hidden globals) so it could be
 * instantiated per-UART later if we ever add a second GNSS receiver.
 */
typedef struct {
    /* UBX state machine */
    int      st;              /* current state, see state list above */
    uint8_t  cls, id;         /* header being collected */
    uint16_t len;             /* payload length from the header */
    uint16_t got;             /* payload bytes collected so far */
    uint8_t  ck_a, ck_b;      /* running Fletcher checksum over cls..payload */
    uint16_t rem;             /* payload bytes still expected */

    uint8_t  payload[UBX_MAX_PAYLOAD];

    /* NMEA side: "$..." lines are assembled here until CR/LF */
    char     nmea_buf[180];
    uint16_t nmea_len;

    ubx_frame_cb_t   on_ubx;
    nmea_line_cb_t   on_nmea;
    void            *user;

    /* statistics (mirrored into ubx_parser_stats_t on request) */
    uint32_t rx_bytes;
    uint32_t ubx_packets_ok;
    uint32_t ubx_checksum_errors;
    uint32_t ubx_length_errors;
    uint32_t nmea_sentences;
    uint32_t parser_resyncs;
} ubx_parser_t;

/* Reset the parser and register the callbacks. */
void ubx_parser_init(ubx_parser_t *p,
                     ubx_frame_cb_t on_ubx, nmea_line_cb_t on_nmea, void *user);

/* Push any number of raw bytes through the machine (handles chunking). */
void ubx_parser_feed(ubx_parser_t *p, const uint8_t *data, size_t len);

/* Copy the current counters out (for logging / display). */
void ubx_parser_get_stats(const ubx_parser_t *p, ubx_parser_stats_t *out);

/* UBX checksum (Fletcher-16) helper, shared with the TX path. */
void ubx_checksum_update(uint8_t *ck_a, uint8_t *ck_b, uint8_t byte);