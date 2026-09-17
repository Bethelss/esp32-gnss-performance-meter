#include "ubx_config.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"             /* ESP_LOGI/W/E */
#include "esp_timer.h"           /* esp_timer_get_time() */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"       /* vTaskDelay - sleeping the current task */

static const char *TAG = "ubxcfg";

ubx_resp_ctx_t g_resp;
ubx_mon_ver_t  g_gnss_ver;
ubx_cfg_snapshot_t g_gnss_cfg;

void ubx_config_init(void)
{
    ubx_resp_init(&g_resp);
    memset(&g_gnss_ver, 0, sizeof(g_gnss_ver));
    memset(&g_gnss_cfg, 0, sizeof(g_gnss_cfg));
}

/* ---- key type helpers ----
 * Official config-interface encoding: the value size derives from the TOP
 * nibble (bits 31..28) of the key ID:
 *   0x0/0x1/0x2 -> 1 byte (L, U1/E1/X1/I1)
 *   0x3         -> 2 bytes (U2/I2/E2/X2)
 *   0x4         -> 4 bytes (U4/I4/E4/X4/R4)
 *   0x5         -> 8 bytes (U8/I8/X8/R8)
 * Evidence from hardware: a 21-key VALGET response was exactly 114 bytes
 * (4 hdr + 4 baud(0x4..) + 2*2 meas/nav(0x3..) + 18*1 rest(0x1/0x2..)).
 */
static size_t key_val_size(uint32_t key)
{
    switch ((key >> 28) & 0xF) {
    case 3: return 2;
    case 4: return 4;
    case 5: return 8;
    default: return 1;
    }
}

/* ---- explicit little-endian decode helpers ----
 * The ESP32 is little-endian, so we COULD cast the payload onto structs -
 * but the project spec forbids that (padding/alignment bugs are real and
 * silent). These helpers read bytes one at a time and assemble values,
 * which works on any CPU regardless of endianness or struct packing. */
static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int16_t  rd_i16(const uint8_t *p) { return (int16_t)rd_u16(p); }
static uint32_t rd_u32(const uint8_t *p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }
static int32_t  rd_i32(const uint8_t *p) { return (int32_t)rd_u32(p); }

/* ---------------- VALGET / VALSET (low level, non-blocking send) ---------------- */

/*
 * ubx_valget - build and send a CFG-VALGET poll.
 * payload = [version=0][layer=RAM][position=0][key IDs, 4 bytes each].
 * The answer arrives asynchronously as a 0x06 0x8B frame; ubx_query_config()
 * handles the waiting, this function only transmits.
 */
int ubx_valget(const uint32_t *keys, size_t nkeys)
{
    if (nkeys > 60) return -1;   /* spec: max 64 keys per poll; we leave margin */
    uint8_t payload[4 + 4 * 60];
    payload[0] = 0x00;           /* version 0 */
    payload[1] = POLL_LAYER_RAM; /* read from the RAM layer */
    payload[2] = 0x00;           /* position lo (pagination: start at 0) */
    payload[3] = 0x00;           /* position hi */
    size_t n = 4;
    for (size_t i = 0; i < nkeys; i++) {
        uint32_t k = keys[i];
        payload[n++] = (uint8_t)(k & 0xFF);
        payload[n++] = (uint8_t)((k >> 8) & 0xFF);
        payload[n++] = (uint8_t)((k >> 16) & 0xFF);
        payload[n++] = (uint8_t)((k >> 24) & 0xFF);
    }
    return ubx_send(0x06, 0x8B, payload, (uint16_t)n);
}

/*
 * ubx_valset - build and send a CFG-VALSET with one or more key/value pairs.
 * Values are taken from u32 but only the low `size` bytes are written,
 * where size comes from the key's top nibble. RAM layer ONLY -> volatile.
 */
int ubx_valset(const uint32_t *keys, const uint32_t *values, size_t n)
{
    if (n > 60) return -1;
    uint8_t payload[4 + 12 * 60];
    payload[0] = 0x00;            /* version 0 */
    payload[1] = VAL_LAYER_RAM;   /* RAM only - volatile by design */
    payload[2] = 0x00;            /* transaction: none (apply immediately) */
    payload[3] = 0x00;
    size_t n_ = 4;
    for (size_t i = 0; i < n; i++) {
        uint32_t k = keys[i];
        size_t vs = key_val_size(k);
        payload[n_++] = (uint8_t)(k & 0xFF);
        payload[n_++] = (uint8_t)((k >> 8) & 0xFF);
        payload[n_++] = (uint8_t)((k >> 16) & 0xFF);
        payload[n_++] = (uint8_t)((k >> 24) & 0xFF);
        uint32_t v = values[i];
        for (size_t b = 0; b < vs; b++) {
            payload[n_++] = (uint8_t)((v >> (8 * b)) & 0xFF);
        }
    }
    return ubx_send(0x06, 0x8A, payload, (uint16_t)n_);
}

/* ---------------- STEP 3: MON-VER (receiver identity) ---------------- */

/*
 * ubx_query_mon_ver - ask "who are you?".
 * MON-VER has an EMPTY payload when polled; the answer (0x0A 0x04) carries
 * swVersion[30], hwVersion[10] and zero or more 30-byte extension strings
 * (FWVER=..., PROTVER=..., GPS;GLO;GAL;BDS, ...).
 * Sequence: arm -> send -> sleep in ubx_resp_wait (gnss_rx keeps running)
 * -> dispatcher stores the answer and wakes us.
 */
int ubx_query_mon_ver(ubx_mon_ver_t *info)
{
    memset(info, 0, sizeof(*info));
    ubx_resp_arm(&g_resp, 0x0A, 0x04);   /* wait for the response FRAME */
    int err = ubx_send(0x0A, 0x04, NULL, 0);
    if (err) {
        g_resp.armed = false;            /* send failed: cancel the wait */
        return err;
    }
    ubx_resp_t r = ubx_resp_wait(&g_resp, 2000);
    return (r == UBX_RESP_MONVER && info->received) ? 0 : -1;
}

/* Dispatcher callback (gnss task): parse the MON-VER payload into globals. */
void ubx_mon_ver_store(const ubx_frame_t *f)
{
    ubx_mon_ver_t *info = &g_gnss_ver;
    if (f->len < 40) {
        ESP_LOGW(TAG, "MON-VER too short: %u", (unsigned)f->len);
        return;
    }
    memcpy(info->sw_version, f->payload, 30);       /* 30-byte NUL-padded string */
    info->sw_version[30] = '\0';                     /* force termination */
    memcpy(info->hw_version, f->payload + 30, 10);
    info->hw_version[10] = '\0';
    info->ext_count = 0;
    size_t off = 40;                                 /* extensions start here */
    while (off + 30 <= f->len && info->ext_count < 6) {
        memcpy(info->ext[info->ext_count], f->payload + off, 30);
        info->ext[info->ext_count][30] = '\0';
        info->ext_count++;
        off += 30;
    }
    info->received = true;
}

/* ---------------- STEP 4: read current configuration ---------------- */

/* The keys whose values we want to see in the startup summary. */
static const uint32_t s_query_keys[] = {
    KEY_UART1_BAUDRATE,
    KEY_UART1INPROT_UBX, KEY_UART1INPROT_NMEA,
    KEY_UART1OUTPROT_UBX, KEY_UART1OUTPROT_NMEA,
    KEY_RATE_MEAS, KEY_RATE_NAV,
    KEY_SIGNAL_GPS_ENA, KEY_SIGNAL_GAL_ENA, KEY_SIGNAL_GLO_ENA,
    KEY_SIGNAL_BDS_ENA, KEY_SIGNAL_SBAS_ENA, KEY_SIGNAL_QZSS_ENA,
    KEY_MSGOUT_NAV_PVT_UART1,
    KEY_MSGOUT_NMEA_GGA_UART1, KEY_MSGOUT_NMEA_RMC_UART1,
    KEY_MSGOUT_NMEA_GSV_UART1, KEY_MSGOUT_NMEA_GSA_UART1,
    KEY_MSGOUT_NMEA_GLL_UART1, KEY_MSGOUT_NMEA_VTG_UART1,
    KEY_MSGOUT_NAV_SAT_UART1,
};
#define N_QUERY (sizeof(s_query_keys) / sizeof(s_query_keys[0]))

/*
 * ubx_valget_store - parse a CFG-VALGET RESPONSE.
 * Payload: [version][layer][position:2] then key/value pairs back to back
 * with NO padding; each value's size comes from the key's top nibble.
 * A wrong size here misaligns everything after it (the bug we fixed).
 */
void ubx_valget_store(ubx_cfg_snapshot_t *snap, const ubx_frame_t *f)
{
    if (f->len < 4) return;
    size_t off = 4;   /* skip version, layer, position(2) */
    while (off + 5 <= f->len) {
        uint32_t key = rd_u32(f->payload + off);
        off += 4;
        size_t vs = key_val_size(key);
        if (off + vs > f->len) break;         /* truncated; stop safely */
        uint32_t v = 0;
        for (size_t b = 0; b < vs; b++) v |= (uint32_t)f->payload[off + b] << (8 * b);
        off += vs;
        switch (key) {
        case KEY_UART1_BAUDRATE:        snap->baudrate = v; break;
        case KEY_UART1INPROT_UBX:       snap->in_ubx = v != 0; break;
        case KEY_UART1INPROT_NMEA:      snap->in_nmea = v != 0; break;
        case KEY_UART1OUTPROT_UBX:      snap->out_ubx = v != 0; break;
        case KEY_UART1OUTPROT_NMEA:     snap->out_nmea = v != 0; break;
        case KEY_RATE_MEAS:             snap->meas_ms = (uint16_t)v; break;
        case KEY_RATE_NAV:              snap->nav_rate = (uint16_t)v; break;
        case KEY_SIGNAL_GPS_ENA:        snap->gps_ena = v != 0; break;
        case KEY_SIGNAL_GAL_ENA:        snap->gal_ena = v != 0; break;
        case KEY_SIGNAL_GLO_ENA:        snap->glo_ena = v != 0; break;
        case KEY_SIGNAL_BDS_ENA:        snap->bds_ena = v != 0; break;
        case KEY_SIGNAL_SBAS_ENA:       snap->sbas_ena = v != 0; break;
        case KEY_SIGNAL_QZSS_ENA:       snap->qzss_ena = v != 0; break;
        case KEY_MSGOUT_NAV_PVT_UART1:  snap->pvt_rate = (uint8_t)v; break;
        case KEY_MSGOUT_NAV_SAT_UART1:  snap->sat_rate = (uint8_t)v; break;
        case KEY_MSGOUT_NMEA_GGA_UART1: snap->gga_rate = (uint8_t)v; break;
        case KEY_MSGOUT_NMEA_RMC_UART1: snap->rmc_rate = (uint8_t)v; break;
        case KEY_MSGOUT_NMEA_GSV_UART1: snap->gsv_rate = (uint8_t)v; break;
        case KEY_MSGOUT_NMEA_GSA_UART1: snap->gsa_rate = (uint8_t)v; break;
        case KEY_MSGOUT_NMEA_GLL_UART1: snap->gll_rate = (uint8_t)v; break;
        case KEY_MSGOUT_NMEA_VTG_UART1: snap->vtg_rate = (uint8_t)v; break;
        default: break;                    /* unknown key: skip, keep parsing */
        }
    }
    snap->valid = true;
}

int ubx_query_config(ubx_cfg_snapshot_t *snap)
{
    if (!snap) return -1;
    memset(snap, 0, sizeof(*snap));
    ubx_resp_arm(&g_resp, 0x06, 0x8B);   /* wait for the VALGET response frame */
    int err = ubx_valget(s_query_keys, N_QUERY);
    if (err) {
        g_resp.armed = false;
        return err;
    }
    ubx_resp_t r = ubx_resp_wait(&g_resp, 2000);
    return (r == UBX_RESP_VALGET && snap->valid) ? 0 : -1;
}

/* ---------------- config setters (arm -> send -> wait for own ACK) ---------------- */

/* Every setter follows the identical pattern:
 *   1. ubx_resp_arm(0x06, 0x8A) - VALSET ACKs carry class 06 id 8A
 *   2. ubx_valset(...)          - transmit
 *   3. ubx_resp_wait(2000)      - sleep; gnss_rx wakes us on ACK-ACK/NAK
 *   4. success only if ACK-ACK was received (NAK/timeout = failure).     */

int ubx_enable_nav_pvt(void)
{
    const uint32_t keys[] = { KEY_MSGOUT_NAV_PVT_UART1 };
    const uint32_t vals[] = { 1 };        /* rate 1 = once per solution */
    ubx_resp_arm(&g_resp, 0x06, 0x8A);
    int err = ubx_valset(keys, vals, 1);
    if (err) { g_resp.armed = false; return err; }
    return (ubx_resp_wait(&g_resp, 2000) == UBX_RESP_ACK) ? 0 : -1;
}

int ubx_set_uart_baud(uint32_t baud)
{
    const uint32_t keys[] = { KEY_UART1_BAUDRATE };
    const uint32_t vals[] = { baud };
    ubx_resp_arm(&g_resp, 0x06, 0x8A);
    int err = ubx_valset(keys, vals, 1);
    if (err) { g_resp.armed = false; return err; }
    return (ubx_resp_wait(&g_resp, 2000) == UBX_RESP_ACK) ? 0 : -1;
}

int ubx_set_rate(uint16_t meas_ms)
{
    const uint32_t keys[] = { KEY_RATE_MEAS, KEY_RATE_NAV };
    const uint32_t vals[] = { meas_ms, 1 };   /* 1 measurement per solution */
    ubx_resp_arm(&g_resp, 0x06, 0x8A);
    int err = ubx_valset(keys, vals, 2);
    if (err) { g_resp.armed = false; return err; }
    return (ubx_resp_wait(&g_resp, 2000) == UBX_RESP_ACK) ? 0 : -1;
}

/* pass_mask: bit0 GPS, bit1 Galileo, bit2 GLONASS, bit3 BeiDou. */
int ubx_set_constellations(uint8_t pass_mask)
{
    uint32_t keys[4], vals[4];
    size_t n = 0;
    keys[n] = KEY_SIGNAL_GPS_ENA; vals[n] = (pass_mask & 1) ? 1 : 0; n++;
    keys[n] = KEY_SIGNAL_GAL_ENA; vals[n] = (pass_mask & 2) ? 1 : 0; n++;
    keys[n] = KEY_SIGNAL_GLO_ENA; vals[n] = (pass_mask & 4) ? 1 : 0; n++;
    keys[n] = KEY_SIGNAL_BDS_ENA; vals[n] = (pass_mask & 8) ? 1 : 0; n++;
    ubx_resp_arm(&g_resp, 0x06, 0x8A);
    int err = ubx_valset(keys, vals, n);
    if (err) { g_resp.armed = false; return err; }
    return (ubx_resp_wait(&g_resp, 2000) == UBX_RESP_ACK) ? 0 : -1;
}

int ubx_disable_nmea_output(void)
{
    /* Rate 0 = never send this sentence. Kept: GGA/RMC used for recovery. */
    const uint32_t keys[] = {
        KEY_MSGOUT_NMEA_GGA_UART1, KEY_MSGOUT_NMEA_RMC_UART1,
        KEY_MSGOUT_NMEA_GSV_UART1, KEY_MSGOUT_NMEA_GSA_UART1,
        KEY_MSGOUT_NMEA_GLL_UART1, KEY_MSGOUT_NMEA_VTG_UART1,
    };
    const uint32_t vals[6] = {0, 0, 0, 0, 0, 0};
    ubx_resp_arm(&g_resp, 0x06, 0x8A);
    int err = ubx_valset(keys, vals, 6);
    if (err) { g_resp.armed = false; return err; }
    return (ubx_resp_wait(&g_resp, 2000) == UBX_RESP_ACK) ? 0 : -1;
}

int ubx_enable_nav_sat(void)
{
    const uint32_t keys[] = { KEY_MSGOUT_NAV_SAT_UART1 };
    const uint32_t vals[] = { 1 };
    ubx_resp_arm(&g_resp, 0x06, 0x8A);
    int err = ubx_valset(keys, vals, 1);
    if (err) { g_resp.armed = false; return err; }
    return (ubx_resp_wait(&g_resp, 2000) == UBX_RESP_ACK) ? 0 : -1;
}

/* ---------------- STEP 6: NAV-PVT decode (official 92-byte layout) ---------------- */
/*
 * Verified offsets (u-blox M10 SPG Interface Description, NAV-PVT 0x01 0x07):
 *   0  U4 iTOW        4  U2 year      6  U1 month    7 U1 day
 *   8  U1 hour        9  U1 min      10 U1 sec     11 X1 valid
 *  12  U4 tAcc       16  I4 nano     20 U1 fixType 21 X1 flags
 *  22  X1 flags2     23 U1 numSV    24 I4 lon     28 I4 lat
 *  32  I4 height     36 I4 hMSL     40 U4 hAcc    44 U4 vAcc
 *  48  I4 velN       52 I4 velE     56 I4 velD    60 I4 gSpeed
 *  64  I4 headMot    68 U4 sAcc     72 U4 headAcc 76 U2 pDOP
 *  78  X2 flags3     80 U4 reserved 84 I4 headVeh 88 I2 magDec 90 U2 magAcc
 *
 * NOTE on "strange" values when there is NO FIX (fixType=0):
 * u-blox fills position/velocity/accuracy with sentinels that grow over
 * time (pDOP=9999, headAcc=180deg, sAcc=999000, hAcc climbing). That is
 * documented behavior, not garbage - the console layer hides these fields
 * until fixType >= 2.
 */
bool ubx_decode_nav_pvt(const ubx_frame_t *f, ubx_nav_pvt_t *out)
{
    if (f->cls != 0x01 || f->id != 0x07) return false;
    if (f->len != 92) {   /* exact length required by the M10 layout */
        ESP_LOGW(TAG, "NAV-PVT unexpected length %u (want 92)", (unsigned)f->len);
        return false;
    }
    memset(out, 0, sizeof(*out));

    const uint8_t *p = f->payload;
    out->iTOW     = rd_u32(p + 0);
    out->fixType  = p[20];
    out->flags    = p[21];
    out->flags2   = p[22];
    out->numSV    = p[23];
    out->lon      = rd_i32(p + 24);
    out->lat      = rd_i32(p + 28);
    out->height   = rd_i32(p + 32);
    out->hMSL     = rd_i32(p + 36);
    out->hAcc     = rd_u32(p + 40);
    out->vAcc     = rd_u32(p + 44);
    out->velN     = rd_i32(p + 48);
    out->velE     = rd_i32(p + 52);
    out->velD     = rd_i32(p + 56);
    out->gSpeed   = rd_i32(p + 60);
    out->headMot  = rd_i32(p + 64);
    out->sAcc     = rd_u32(p + 68);
    out->headAcc  = rd_u32(p + 72);
    out->pDOP     = rd_u16(p + 76);
    out->rx_ts_ms = (uint32_t)(esp_timer_get_time() / 1000);
    return true;
}

/*
 * Temporary DEBUG: dump one NAV-PVT completely - raw payload hex by rows
 * of 16 bytes with their offsets, then every decoded field with the raw
 * bytes at the two most interesting offsets (hAcc, sAcc). Used once after
 * boot to compare raw bytes against the official offsets on real hardware.
 */
void ubx_debug_dump_nav_pvt(const ubx_frame_t *f)
{
    ESP_LOGI(TAG, "NAV-PVT DEBUG: payload len=%u", (unsigned)f->len);
    for (size_t off = 0; off < f->len; off += 16) {
        char hex[64];
        size_t n = 0;
        for (size_t i = off; i < f->len && i < off + 16; i++) {
            n += snprintf(hex + n, sizeof(hex) - n, "%02X", f->payload[i]);
            if ((i - off) % 4 == 3) n += snprintf(hex + n, sizeof(hex) - n, " ");
        }
        ESP_LOGI(TAG, "  +%02u: %s", (unsigned)off, hex);
    }

    ubx_nav_pvt_t d;
    if (!ubx_decode_nav_pvt(f, &d)) return;
    ESP_LOGI(TAG,
             "  iTOW=%lu  fixType=%u flags=%02X flags2=%02X numSV=%u",
             (unsigned long)d.iTOW, d.fixType, d.flags, d.flags2, d.numSV);
    ESP_LOGI(TAG,
             "  lon=%ld lat=%ld height=%ld hMSL=%ld",
             (long)d.lon, (long)d.lat, (long)d.height, (long)d.hMSL);
    ESP_LOGI(TAG,
             "  hAcc=%lu vAcc=%lu  [raw bytes 40..43: %02X %02X %02X %02X]",
             (unsigned long)d.hAcc, (unsigned long)d.vAcc,
             f->payload[40], f->payload[41], f->payload[42], f->payload[43]);
    ESP_LOGI(TAG,
             "  velN=%ld velE=%ld velD=%ld gSpeed=%ld headMot=%ld",
             (long)d.velN, (long)d.velE, (long)d.velD, (long)d.gSpeed, (long)d.headMot);
    ESP_LOGI(TAG,
             "  sAcc=%lu headAcc=%lu pDOP=%u  [raw bytes 68..71: %02X %02X %02X %02X]",
             (unsigned long)d.sAcc, (unsigned long)d.headAcc, d.pDOP,
             f->payload[68], f->payload[69], f->payload[70], f->payload[71]);
}

/* ---------------- NAV-SAT decode (satellite diagnostics) ----------------
 * Payload: 8 header bytes (iTOW, version, numSvs, reserved) then numSvs
 * records of 12 bytes: gnssId, svId, cno, elev(I1), azim(I2), prRes(I2),
 * flags(X4, bit3 = svUsed). We aggregate counts, per-constellation
 * breakdown and C/N0 statistics.                                       */

/* Short constellation names, indexed by gnssId (M10 Satellite Numbering).
 * 0 GPS, 1 SBAS, 2 Galileo, 3 BeiDou, 4 IMES, 5 QZSS, 6 GLONASS, 7 NavIC. */
const char *gnss_id_name(uint8_t gnssId)
{
    static const char *names[GNSS_ID_COUNT] = {
        "GPS", "SBAS", "GAL", "BDS", "IME", "QZS", "GLO", "NIC"
    };
    return (gnssId < GNSS_ID_COUNT) ? names[gnssId] : "???";
}

bool ubx_decode_nav_sat(const ubx_frame_t *f, ubx_nav_sat_t *out)
{
    if (f->cls != 0x01 || f->id != 0x35) return false;
    if (f->len < 8) return false;
    memset(out, 0, sizeof(*out));
    const uint8_t *p = f->payload;
    uint8_t numSvs = p[5];
    if (numSvs > NAV_SAT_MAX) numSvs = NAV_SAT_MAX;
    if (f->len < (size_t)(8 + numSvs * 12)) return false;

    out->numSvs = numSvs;
    for (int i = 0; i < numSvs; i++) {
        const uint8_t *sv = p + 8 + i * 12;
        out->sv[i].gnssId = sv[0];
        out->sv[i].svId   = sv[1];
        out->sv[i].cno    = sv[2];                  /* dB-Hz */
        out->sv[i].elev   = (int8_t)sv[3];
        out->sv[i].azim   = rd_i16(sv + 4);
        uint32_t flags = rd_u32(sv + 8);
        out->sv[i].svUsed = (flags & (1u << 3)) != 0;

        /* Per-constellation breakdown (guard: unknown gnssId -> ignore). */
        if (out->sv[i].gnssId < GNSS_ID_COUNT) {
            if (out->sv[i].cno > 0) out->vis_per_gnss[out->sv[i].gnssId]++;
            if (out->sv[i].svUsed)  out->used_per_gnss[out->sv[i].gnssId]++;
        }

        if (out->sv[i].cno > 0) out->visible++;
        if (out->sv[i].svUsed) {
            out->used++;
            out->sum_cno_used += out->sv[i].cno;
        }
        if (out->sv[i].cno > out->max_cno) out->max_cno = out->sv[i].cno;
    }
    return true;
}

/* Average C/N0 of USED satellites, dB-Hz (0 if none used). */
float nav_sat_avg_cno_used(const ubx_nav_sat_t *s)
{
    return s->used ? (float)s->sum_cno_used / s->used : 0.0f;
}

/* ---------------- iTOW rate statistics ---------------- */

/*
 * itow_stats_update - called for every NAV-PVT epoch.
 * Measures the REAL navigation rate from the receiver's own clock:
 *   delta 1000 ms = 1 Hz, 100 ms = 10 Hz, 50 ms = 20 Hz, 40 ms = 25 Hz.
 * The subtraction handles GPS week rollover naturally (unsigned wrap).
 * A delta of 0 ms (duplicate epoch) or > 5 s (gap/restart) counts as
 * "unexpected" instead of poisoning the statistics.
 */
void itow_stats_update(itow_stats_t *s, uint32_t iTOW)
{
    if (s->valid) {   /* an epoch already seen */
        uint32_t d = iTOW - s->last_iTOW;   /* handles week rollover */
        if (d == 0 || d > 5000) {
            s->unexpected_count++;
        } else {
            s->delta_iTOW = d;
            s->sum_delta += d;
            s->epochs++;
            if (s->min_delta == 0 || d < s->min_delta) s->min_delta = d;
            if (d > s->max_delta) s->max_delta = d;
        }
    }
    s->last_iTOW = iTOW;
    s->valid = true;
}

/* Average measured rate in Hz: epochs / total seconds (delta is in ms). */
float itow_stats_measured_hz(const itow_stats_t *s)
{
    if (s->epochs == 0 || s->sum_delta == 0) return 0.0f;
    return 1000.0f * s->epochs / (float)s->sum_delta;
}