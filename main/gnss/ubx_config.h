#pragma once
/*
 * Receiver identification, configuration, NAV-PVT decode, iTOW stats.
 *
 * ===================== UBX CONFIGURATION INTERFACE =====================
 *
 * Modern u-blox receivers (M10) are configured through "configuration
 * keys": every setting has a 32-bit KEY ID. You read them with
 * CFG-VALGET and write them with CFG-VALSET.
 *
 * CFG-VALGET request payload: [version=0][layer][position:2][key IDs...]
 * CFG-VALGET response:        [version][layer][position:2][key,value pairs...]
 * CFG-VALSET payload:         [version=0][layers][transaction:2][key,value pairs...]
 *
 * KEY ENCODING (important - we got this wrong once):
 *   the VALUE SIZE is encoded in the TOP nibble of the key ID:
 *     0x0/0x1/0x2 prefix -> 1-byte value   (L, U1, I1, E1, X1)
 *     0x3                -> 2-byte value   (U2, I2, E2, X2)
 *     0x4                -> 4-byte value   (U4, I4, X4)
 *     0x5                -> 8-byte value   (U8, I8, X8, R8)
 *   e.g. 0x40520001 = U4 baud, 0x30210001 = U2 meas period, 0x20910007 = U1 msg rate.
 *
 * MEMORY LAYERS: settings live in RAM (current), BBR (battery-backed) and
 * Flash (permanent). This project writes ONLY to the RAM layer
 * (VAL_LAYER_RAM=0x01), so every experiment is undone by a power cycle.
 *
 * Key IDs below are verified against the u-blox M10 SPG Interface
 * Description (UBX-21035062, PROTVER 34.10) - the exact firmware the
 * connected module runs (checked via MON-VER on hardware).
 */

#include <stdint.h>
#include <stdbool.h>
#include "ubx_parser.h"
#include "ubx_protocol.h"

/* ---- configuration keys (M10, little-endian on wire) ---- */
#define KEY_UART1_BAUDRATE        0x40520001u  /* U4 */
#define KEY_RATE_MEAS             0x30210001u  /* U2, ms between measurements */
#define KEY_RATE_NAV              0x30210002u  /* U2, measurements per solution */
#define KEY_UART1INPROT_UBX       0x10730001u  /* L: accept UBX on input */
#define KEY_UART1INPROT_NMEA      0x10730002u  /* L: accept NMEA input */
#define KEY_UART1OUTPROT_UBX      0x10740001u  /* L: send UBX output */
#define KEY_UART1OUTPROT_NMEA     0x10740002u  /* L: send NMEA output */
#define KEY_SIGNAL_GPS_ENA        0x1031001Fu  /* L: GPS constellation */
#define KEY_SIGNAL_GAL_ENA        0x10310021u  /* L: Galileo */
#define KEY_SIGNAL_GLO_ENA        0x10310025u  /* L: GLONASS */
#define KEY_SIGNAL_BDS_ENA        0x10310022u  /* L: BeiDou */
#define KEY_SIGNAL_SBAS_ENA       0x10310020u  /* L: SBAS */
#define KEY_SIGNAL_QZSS_ENA       0x10310024u  /* L: QZSS */
#define KEY_MSGOUT_NAV_PVT_UART1  0x20910007u  /* U1: NAV-PVT rate on UART1 */
#define KEY_MSGOUT_NAV_SAT_UART1  0x20910016u  /* U1: NAV-SAT rate on UART1 */
#define KEY_MSGOUT_NMEA_GGA_UART1 0x209100BBu  /* U1: NMEA GGA rate */
#define KEY_MSGOUT_NMEA_RMC_UART1 0x209100ACu  /* U1: NMEA RMC rate */
#define KEY_MSGOUT_NMEA_GSV_UART1 0x209100C5u  /* U1: NMEA GSV rate */
#define KEY_MSGOUT_NMEA_GSA_UART1 0x209100C0u  /* U1: NMEA GSA rate */
#define KEY_MSGOUT_NMEA_GLL_UART1 0x209100CAu  /* U1: NMEA GLL rate */
#define KEY_MSGOUT_NMEA_VTG_UART1 0x209100B1u  /* U1: NMEA VTG rate */

#define VAL_LAYER_RAM   0x01
#define POLL_LAYER_RAM  0x00

/* ---- receiver identity, filled by the MON-VER dispatcher ---- */
typedef struct {
    char sw_version[31];   /* e.g. "ROM SPG 5.10 (7b202e)" */
    char hw_version[11];   /* e.g. "000A0000" */
    char ext[6][31];       /* extension strings: FWVER=, PROTVER=, MOD=... */
    int  ext_count;
    bool received;
} ubx_mon_ver_t;

/*
 * Decoded NAV-PVT (the main navigation solution message).
 * Field names/offsets are the official 92-byte M10 layout; values are kept
 * in NATIVE units (mm, mm/s, 1e-7 deg) and converted only for display.
 * See the offset table above ubx_decode_nav_pvt() in the .c file.
 */
typedef struct {
    uint32_t iTOW;        /* ms GPS time of week, offset 0 */
    uint8_t  fixType;     /* 0 none 1 dead-reckoning 2 2D 3 3D 4 gnss+dr 5 time */
    uint8_t  flags;       /* bit0 gnssFixOK, bit1 diffSoln... */
    uint8_t  flags2;      /* confirmedDate/Time bits */
    uint8_t  numSV;       /* satellites used in the solution */
    int32_t  lon;         /* 1e-7 deg */
    int32_t  lat;         /* 1e-7 deg */
    int32_t  height;      /* mm above WGS84 ellipsoid */
    int32_t  hMSL;        /* mm above mean sea level */
    uint32_t hAcc;        /* mm, horizontal accuracy estimate */
    uint32_t vAcc;        /* mm, vertical accuracy estimate */
    int32_t  velN, velE, velD; /* mm/s, NED velocity components */
    int32_t  gSpeed;      /* mm/s, ground speed (2-D) - THE speed source */
    int32_t  headMot;     /* 1e-5 deg, heading of motion */
    uint32_t sAcc;        /* mm/s, speed accuracy estimate */
    uint32_t headAcc;     /* 1e-5 deg, heading accuracy */
    uint16_t pDOP;        /* 0.01, position dilution of precision */
    uint32_t rx_ts_ms;    /* local ESP time (ms since boot) when received */
} ubx_nav_pvt_t;

/*
 * Navigation-rate statistics derived purely from iTOW deltas.
 * iTOW is the receiver's GPS time of week in ms; consecutive solutions
 * are spaced by the navigation period (1000 ms at 1 Hz, 100 ms at 10 Hz,
 * 50 ms at 20 Hz, 40 ms at 25 Hz). Measuring here means we PROVE the
 * configured rate instead of trusting it.
 */
typedef struct {
    uint32_t last_iTOW;         /* iTOW of previous epoch */
    uint32_t delta_iTOW;        /* last observed delta in ms */
    uint32_t epochs;            /* deltas counted */
    uint32_t min_delta;         /* shortest epoch ever */
    uint32_t max_delta;         /* longest epoch ever */
    uint64_t sum_delta;         /* for the average */
    uint32_t unexpected_count;  /* delta==0 or >5s (jumps, missed epochs) */
    bool     valid;             /* at least one epoch seen */
} itow_stats_t;

/* ---- satellite diagnostics (NAV-SAT) ----
 *
 * NAV-SAT lists every satellite the receiver knows about, tracked or not.
 * Per satellite we keep: constellation (gnssId), satellite number (svId),
 * signal strength (C/N0, dB-Hz), elevation/azimuth, and the svUsed flag
 * (1 = this satellite is in the current navigation solution).
 *
 * Aggregates computed over the list:
 *   - visible  : entries with C/N0 > 0 (receiver hears something)
 *   - used     : svUsed flag set
 *   - per-constellation counts (gnssId mapping below)
 *   - avg/max C/N0 of USED satellites (signal quality of the solution)
 *
 * gnssId mapping per M10 Interface Description (Satellite Numbering):
 *   0 GPS, 1 SBAS, 2 Galileo, 3 BeiDou, 5 QZSS, 6 GLONASS, 7 NavIC
 */
#define NAV_SAT_MAX 40
#define GNSS_ID_COUNT 8
typedef struct {
    uint8_t  numSvs;        /* entries in the packet */
    uint8_t  visible;       /* C/N0 > 0 (receiver sees signal) */
    uint8_t  used;          /* svUsed flag set (in the navigation solution) */
    int8_t   max_cno;       /* strongest signal of the whole list, dB-Hz */
    int32_t  sum_cno_used;  /* sum of C/N0 of used satellites -> average */
    /* per-constellation breakdown (indexed by gnssId 0..7) */
    uint8_t  vis_per_gnss[GNSS_ID_COUNT];
    uint8_t  used_per_gnss[GNSS_ID_COUNT];
    struct {
        uint8_t gnssId, svId, cno;
        int8_t  elev;       /* degrees, -90..90 */
        int16_t azim;       /* degrees, 0..359 */
        bool    svUsed;
    } sv[NAV_SAT_MAX];
} ubx_nav_sat_t;

/* Short constellation name by gnssId ("GPS", "GAL", ...). Never NULL. */
const char *gnss_id_name(uint8_t gnssId);

/* ---- configuration snapshot, filled by VALGET responses ---- */
typedef struct {
    uint32_t baudrate;
    bool     in_ubx, in_nmea;
    bool     out_ubx, out_nmea;
    uint16_t meas_ms;
    uint16_t nav_rate;
    bool     gps_ena, gal_ena, glo_ena, bds_ena, sbas_ena, qzss_ena;
    uint8_t  pvt_rate, sat_rate;
    uint8_t  gga_rate, rmc_rate, gsv_rate, gsa_rate, gll_rate, vtg_rate;
    bool     valid;      /* at least one VALGET response was parsed */
} ubx_cfg_snapshot_t;

/* ---- shared objects (defined in ubx_config.c) ----
 * g_resp:   the command/response slot used by all ubx_query_* / setters.
 * g_gnss_ver / g_gnss_cfg: filled in by the dispatcher, read by display. */
extern ubx_resp_ctx_t g_resp;
extern ubx_mon_ver_t  g_gnss_ver;
extern ubx_cfg_snapshot_t g_gnss_cfg;

void ubx_config_init(void);

/* STEP 3: poll MON-VER and block on a task notification until the
 * response frame arrives (UART RX keeps running - see ubx_protocol.h). */
int ubx_query_mon_ver(ubx_mon_ver_t *info);

/* STEP 4: read the interesting keys from RAM; block on the response. */
int ubx_query_config(ubx_cfg_snapshot_t *snap);

/* Non-blocking VALGET request with explicit key list. */
int ubx_valget(const uint32_t *keys, size_t nkeys);

/* VALSET to the RAM layer only (volatile); waits for ACK-ACK. */
int ubx_valset(const uint32_t *keys, const uint32_t *values, size_t n);

/* ---- high-level actions (each arms, sends, waits, reports) ---- */
int ubx_enable_nav_pvt(void);          /* STEP 5 */
int ubx_set_uart_baud(uint32_t baud);  /* STEP 10 (PHASE E, not called yet) */
int ubx_set_rate(uint16_t meas_ms);    /* STEP 11 (10 Hz = 100 ms, later) */
int ubx_set_constellations(uint8_t pass_mask); /* STEP 12/13 (later) */
int ubx_disable_nmea_output(void);     /* STEP 14 (later) */
int ubx_enable_nav_sat(void);          /* STEP 15: NAV-SAT @ every epoch (we use 1 Hz) */

/* NAV-SAT frame -> satellite list + aggregates. */
bool ubx_decode_nav_sat(const ubx_frame_t *f, ubx_nav_sat_t *out);

/* Frame store callbacks, invoked by the dispatcher in gnss.c. */
void ubx_mon_ver_store(const ubx_frame_t *f);
void ubx_valget_store(ubx_cfg_snapshot_t *snap, const ubx_frame_t *f);

/* NAV-PVT frame -> struct. Requires exactly 92 bytes of payload. */
bool ubx_decode_nav_pvt(const ubx_frame_t *f, ubx_nav_pvt_t *out);

/* One-shot diagnostic: hex dump + all decoded fields of one NAV-PVT. */
void ubx_debug_dump_nav_pvt(const ubx_frame_t *f);

/* Feed a new epoch into the rate statistics. */
void itow_stats_update(itow_stats_t *s, uint32_t iTOW);
float itow_stats_measured_hz(const itow_stats_t *s);

/* Average C/N0 of USED satellites in the last NAV-SAT, dB-Hz. */
float nav_sat_avg_cno_used(const ubx_nav_sat_t *s);