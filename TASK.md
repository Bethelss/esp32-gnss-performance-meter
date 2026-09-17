I want you to create the first hardware bring-up / test firmware for my automotive GNSS logger project.

Please work carefully and treat this as the foundation of a larger ESP32-S3 project, not as a throwaway Arduino sketch.

HARDWARE
========

MCU board:
- ESP32-S3 development board
- Module marking: ESP32-S3-N16R8
- 16 MB Flash
- 8 MB PSRAM
- Windows laptop used for development
- Prefer ESP-IDF, NOT Arduino, unless there is a very strong technical reason otherwise.
- Use the current installed ESP-IDF version if one is already available.
- If ESP-IDF is not installed, tell me exactly what needs to be installed before proceeding.

Do not use GPIO19 or GPIO20 because I want to reserve the native ESP32-S3 USB D-/D+ pins for future USB functionality.

GNSS receiver:
- Quescan G10A-F30
- u-blox M10 family
- Connector pins are labelled:
  P = PPS
  V = VCC
  T = GNSS TX
  R = GNSS RX
  G = GND
  E = ENABLE
- The module is currently connected as follows:

  GNSS P (PPS) -> ESP32 GPIO4
  GNSS V       -> ESP32 3V3
  GNSS T (TX)  -> ESP32 GPIO18 (ESP RX)
  GNSS R (RX)  -> ESP32 GPIO17 (ESP TX)
  GNSS G       -> ESP32 GND
  GNSS E       -> not connected

- Factory UART settings are expected to be:
  38400 baud
  8 data bits
  no parity
  1 stop bit
- No hardware flow control.
- Do NOT reconfigure the GNSS receiver yet.
- For this first test I want to verify the factory configuration before sending configuration commands.
- The receiver may output NMEA, UBX binary data, or both.
- It may have no GNSS fix because I may be testing indoors.

OLED:
- Cheap generic 1.30 inch monochrome OLED
- 128x64 is very likely
- Only marking is "1.30 IIC"
- Pins:
  VDD
  GND
  SCK
  SDA
- Connected as:

  OLED VDD -> ESP32 3V3
  OLED GND -> ESP32 GND
  OLED SCK -> ESP32 GPIO9
  OLED SDA -> ESP32 GPIO8

- SCK on this module means I2C SCL.
- Likely I2C address is 0x3C, possibly 0x3D.
- Controller is unknown.
- A 1.3" module like this is likely SH1106, but do NOT blindly assume that.
- Perform an I2C bus scan first.
- If useful, support both SH1106 and SSD1306, or at minimum make the controller choice very easy to change.
- Prefer a reliable ESP-IDF compatible display library/component rather than writing a large display driver from scratch.
- If using an external component such as U8g2, use the normal ESP-IDF component mechanism and document exactly what you added.

GOAL
====

Create a small but well-structured ESP-IDF test project that verifies:

1. ESP32-S3 boots correctly.
2. I2C bus works.
3. OLED responds.
4. OLED can display text.
5. GNSS UART receives data at 38400 baud.
6. Raw GNSS traffic can be observed in the PC serial console.
7. NMEA traffic can be recognized if present.
8. UBX traffic can at least be recognized if present.
9. PPS pulses on GPIO4 can be detected.
10. The display remains responsive without interfering with GNSS reception.

Do not implement the full racing device yet.

ARCHITECTURE
============

Please keep the code structured enough to expand later.

I would like approximately this logical structure:

    GNSS UART
        |
        v
    GNSS receive task
        |
        +--> raw/debug output
        |
        +--> lightweight parser / current GNSS status
                         |
                         +--> OLED task

    PPS GPIO interrupt
        |
        v
    PPS counter/timestamp

For now two FreeRTOS tasks are enough:
- GNSS receive task
- display/status task

Do not create unnecessary tasks or abstractions.

The GNSS task must not wait for the OLED.

The OLED should update at approximately 5-10 Hz. There is no reason to redraw it hundreds of times per second.

UART reception must be buffered and robust enough that OLED updates cannot cause lost GNSS bytes.

GNSS TEST BEHAVIOR
==================

At startup, print clear diagnostics to the serial console:

- firmware name/version
- detected chip
- flash size if convenient
- PSRAM status if convenient
- configured UART pins
- configured GNSS baud rate
- I2C pins
- detected I2C addresses
- selected OLED driver/controller
- PPS pin

For GNSS reception:

1. Configure UART1:
   - TX GPIO17
   - RX GPIO18
   - 38400 baud
   - 8N1
   - no flow control

2. Continuously read UART data.

3. For the initial bring-up, print received NMEA lines to the serial console in a readable way.

4. Binary UBX bytes must not break the console output badly.
   If UBX sync bytes 0xB5 0x62 are detected, count UBX packets or at least report that UBX traffic was detected.

5. Do NOT send configuration commands to the GNSS receiver yet.

6. If convenient, implement a very small NMEA parser for only the information useful for testing:
   - GGA/GNGGA:
       fix quality
       satellites
   - RMC/GNRMC:
       valid/invalid status
       speed over ground

Do not pull in a huge GPS framework just for this test unless there is a good reason.

Convert RMC speed from knots to km/h if it is parsed.

It is completely valid for the screen to show "NO FIX" indoors.

PPS TEST
========

Configure GPIO4 as PPS input.

Use a rising-edge interrupt.

Do minimal work in the ISR.

Record/increment:
- PPS pulse count
- local ESP timer timestamp of the latest PPS edge

The display can show whether PPS has ever been observed.

Do not attempt full GNSS-to-ESP clock synchronization yet.

OLED TEST
=========

At startup:

1. Scan the I2C bus.
2. Print all detected I2C addresses to the serial console.
3. Try to initialize the OLED.
4. If display initialization succeeds, show a startup screen similar to:

    GNSS LOGGER TEST
    ESP32-S3
    OLED OK
    I2C: 0x3C

Then switch to a live status page.

A suggested live page:

    GNSS TEST
    RX: OK
    FIX: 3D / NO FIX
    SV: 12
    SPD: 0.0 km/h
    PPS: YES
    NMEA: 1234

If some information cannot yet be parsed, show useful diagnostics instead, such as:
- UART byte count
- NMEA sentence count
- UBX detected YES/NO
- PPS count

The important objective is hardware verification, not a pretty UI.

DISPLAY PERFORMANCE
===================

I am concerned about the cheap I2C OLED being slow.

Configure I2C at 400 kHz if the module works reliably at that speed.

Do not let full-screen rendering block GNSS reception.

A display refresh around 5-10 Hz is enough for this test.

Use a framebuffer/library architecture that is appropriate for a 128x64 monochrome display.

ERROR HANDLING
==============

Please handle these cases clearly:

- no I2C device found
- I2C device found but OLED init failed
- GNSS UART receives zero bytes
- GNSS UART receives bytes but no valid NMEA
- GNSS has no satellite fix
- PPS is not present
- display controller turns out to be SH1106 instead of SSD1306

Do not treat "no GNSS fix" as a hardware failure.

PROJECT REQUIREMENTS
====================

Please create a normal ESP-IDF project with clean file organization.

For example:

    gnss_logger_test/
        CMakeLists.txt
        sdkconfig.defaults if useful
        main/
            CMakeLists.txt
            main.c
            gnss.c
            gnss.h
            display.c
            display.h

You may simplify this if splitting files makes the first test unnecessarily complicated, but avoid one giant unstructured source file.

Use ESP-IDF APIs rather than Arduino APIs.

Use ESP_LOGI / ESP_LOGW / ESP_LOGE for diagnostic output.

Use esp_timer_get_time() where a microsecond local timestamp is needed.

Avoid dynamic memory allocation in tight recurring loops when it is unnecessary.

Do not implement:
- Bluetooth yet
- Wi-Fi yet
- microSD yet
- IMU yet
- race timing yet
- GNSS 20/25 Hz configuration yet
- power management yet
- USB MSC yet

We will add these later.

BUILD AND FLASH
===============

I am developing on Windows.

After creating the project, provide exact commands for:

1. selecting ESP32-S3 target
2. building
3. detecting/selecting the COM port
4. flashing
5. opening the serial monitor

For example, if appropriate:

    idf.py set-target esp32s3
    idf.py build
    idf.py -p COMx flash monitor

Do not hardcode a COM port without checking which one Windows assigned.

If my development board has two USB-C connectors, explain briefly which one I should normally try for flashing/serial first and how to identify the correct COM/device.

DELIVERABLE
===========

Please do the following:

1. Inspect the current development environment.
2. Create the ESP-IDF project.
3. Explain any external component/library you decide to use.
4. Implement the firmware.
5. Build it and fix all compile errors.
6. Do not claim that hardware works unless I actually flash/test it.
7. Give me exact flashing instructions.
8. Tell me what serial output I should expect from a healthy setup.
9. Tell me what to report back if:
   - OLED stays blank
   - no I2C address is found
   - no UART data appears
   - text is shifted/corrupted on the OLED
   - NMEA appears but there is no fix

IMPORTANT
=========

Do not guess silently.

If the exact OLED controller cannot be determined without testing, structure the project so I can easily switch between SH1106 and SSD1306 and tell me what symptom distinguishes them.

Do not send any persistent configuration to the Quescan/u-blox GNSS module during this first test.

The first milestone is simply:

ESP32-S3 boots
+ OLED displays diagnostics
+ GNSS UART data is received
+ PPS can be detected

Once that is proven, we will move on to UBX-NAV-PVT and 20/25 Hz GNSS configuration.



##Step 2
We have successfully completed the first hardware bring-up iteration of my ESP32-S3 + Quescan G10A-F30 GNSS project.

The first test firmware already works correctly:
- ESP32-S3 boots correctly
- 16 MB Flash detected
- 8 MB PSRAM detected
- SH1106 1.3" 128x64 OLED works over I2C
- OLED address is 0x3C
- I2C runs at 400 kHz
- Quescan G10A-F30 communicates correctly over UART
- Factory UART is working at 38400 baud, 8N1
- NMEA data is being received correctly
- PPS input on GPIO4 is working
- 2D fix has occasionally been observed indoors
- Typical indoor satellite conditions are weak, so lack of 3D fix is not considered a firmware failure

Now I want to make the SECOND ITERATION.

This iteration must focus ONLY on the GNSS subsystem.

Do NOT implement:
- race timing
- 0-100 / 100-200 calculations
- logging
- microSD
- IMU
- sensor fusion
- Bluetooth
- Android communication
- CAN
- power estimation
- vehicle logic
- USB mass storage

The goal is to understand and properly control the u-blox M10 GNSS receiver.

==================================================
CURRENT HARDWARE
==================================================

MCU:
- ESP32-S3 development board
- ESP32-S3 N16R8
- 16 MB Flash
- 8 MB PSRAM
- ESP-IDF project
- Windows development machine

GNSS:
- Quescan G10A-F30
- u-blox M10 family
- UART interface
- PPS output

Connections:

GNSS:
- P / PPS -> GPIO4
- V / VCC -> ESP32 3V3
- T / GNSS TX -> ESP32 GPIO18 (ESP RX)
- R / GNSS RX -> ESP32 GPIO17 (ESP TX)
- G / GND -> ESP32 GND
- E / ENABLE -> not connected

Initial UART:
- UART1
- ESP TX GPIO17
- ESP RX GPIO18
- 38400 baud
- 8N1
- no flow control

OLED:
- SH1106
- 128x64
- I2C address 0x3C
- SDA GPIO8
- SCL GPIO9
- 400 kHz

PPS:
- GPIO4
- rising-edge interrupt
- already tested successfully

Do not use GPIO19 or GPIO20 because they are reserved for native USB.

==================================================
VERY IMPORTANT: DOCUMENTATION
==================================================

Do not guess u-blox configuration keys, packet layouts, payload lengths,
bit fields, or protocol behavior from memory.

Use the official u-blox M10 Interface Description appropriate for the
receiver/protocol version.

The modern u-blox M10 configuration interface uses CFG-VALGET /
CFG-VALSET.

Use the official documentation to obtain the exact configuration key IDs
and permitted values.

Do not blindly copy old UBX-CFG-* examples intended for older u-blox
generations unless the official M10 documentation explicitly says they
are appropriate.

When uncertain, prefer querying the receiver rather than assuming a value.

==================================================
PRIMARY OBJECTIVES
==================================================

I want the second iteration to achieve the following, in this order:

1. Implement a reliable general UBX protocol parser.
2. Implement UBX packet transmission.
3. Implement UBX checksum generation and verification.
4. Implement ACK-ACK / ACK-NAK handling where applicable.
5. Query receiver identification/version information.
6. Query important current configuration values.
7. Receive and parse UBX-NAV-PVT.
8. Verify navigation update frequency using iTOW.
9. Move UART from 38400 to 115200 safely.
10. Test NAV-PVT at 10 Hz.
11. Test GPS + Galileo NAV-PVT at 20 Hz.
12. Optionally test GPS-only at 25 Hz after 20 Hz is working correctly.
13. Disable unnecessary NMEA output temporarily after UBX is proven.
14. Display useful GNSS diagnostics on the OLED.
15. Keep all experimental configuration changes volatile / RAM-only
    unless I explicitly request persistent storage later.

Do not make permanent configuration changes to the GNSS receiver yet.

==================================================
STEP 1 - ROBUST UBX STREAM PARSER
==================================================

Implement a proper byte-stream UBX parser.

UBX framing is:

    0xB5 0x62
    CLASS
    ID
    LENGTH_L
    LENGTH_H
    PAYLOAD
    CK_A
    CK_B

The parser must operate as a state machine and consume arbitrary UART
chunks.

It must correctly handle:
- packet split across multiple uart_read_bytes() calls
- multiple packets in one UART read
- NMEA mixed with UBX
- random bytes before a UBX sync sequence
- invalid checksum
- malformed / unreasonable payload length
- recovery after corrupted data
- back-to-back UBX packets

Do not assume one UART read equals one GNSS packet.

Maintain counters such as:

    rx_bytes
    ubx_packets_ok
    ubx_checksum_errors
    ubx_length_errors
    nmea_sentences
    parser_resyncs

Use a reasonable maximum UBX payload size and reject absurd length fields
rather than allocating arbitrary amounts of memory.

Avoid heap allocation for every packet.

==================================================
STEP 2 - UBX TX SUPPORT
==================================================

Implement a generic function conceptually similar to:

    ubx_send(class, id, payload, length)

It must:
- generate UBX framing
- encode length little-endian
- calculate CK_A / CK_B correctly
- send the complete packet over UART

Also implement infrastructure for receiving:
- UBX-ACK-ACK
- UBX-ACK-NAK

Do not assume every UBX poll produces ACK; distinguish:
- response messages
- ACK responses
- timeouts

Add useful diagnostics without flooding the console.

==================================================
STEP 3 - IDENTIFY THE RECEIVER
==================================================

Query receiver information using the official u-blox M10-supported
identification/version mechanism, for example UBX-MON-VER if supported
by the installed protocol version.

Print all useful identification strings to the serial console.

I want to know as much as possible about the actual module that arrived,
including where available:
- software version
- hardware version
- protocol version
- firmware information
- supported extensions

Do not invent missing information.

Store receiver identity in a small GNSS information structure if useful.

==================================================
STEP 4 - READ CURRENT CONFIGURATION
==================================================

Before changing anything, use CFG-VALGET to inspect the existing
configuration.

Determine and print at least:

- UART1 baud rate
- enabled UART input protocols
- enabled UART output protocols
- current measurement period
- current navigation rate
- enabled GNSS constellations where accessible
- current UBX-NAV-PVT output rate
- current NMEA output state where relevant

Use exact configuration keys from the official M10 Interface Description.

The firmware should print a readable summary, for example:

    GNSS configuration:
      UART1 baud:       38400
      Measurement:      1000 ms
      Navigation rate:  1
      Effective rate:   1.0 Hz
      GPS:              enabled
      Galileo:          ...
      GLONASS:          ...
      BeiDou:           ...
      NAV-PVT UART1:    ...
      NMEA GGA UART1:   ...

The exact list may differ depending on what the receiver exposes.

==================================================
STEP 5 - ENABLE UBX-NAV-PVT AT 1 HZ FIRST
==================================================

Do NOT immediately switch to 20 Hz.

First keep the existing navigation frequency at 1 Hz.

Enable UBX-NAV-PVT output on UART1 using CFG-VALSET.

For the first test:
- keep NMEA enabled
- enable NAV-PVT alongside it
- keep UART at 38400
- keep navigation at 1 Hz

This lets us verify that the binary parser works without changing several
variables at the same time.

The changes should be RAM-only / volatile.

Wait until valid NAV-PVT packets are being received before proceeding.

==================================================
STEP 6 - PARSE UBX-NAV-PVT
==================================================

Implement a proper NAV-PVT decoder using the exact official M10 message
layout.

Do not cast raw payload bytes directly onto an unsafe C struct unless
packing, alignment, endianness, and payload size have been deliberately
handled and justified.

Prefer explicit little-endian decode helpers such as conceptually:

    read_u16_le()
    read_u32_le()
    read_i32_le()

Verify the exact payload length before decoding fields.

Parse at least:

TIME:
- iTOW

FIX / QUALITY:
- fixType
- relevant validity flags
- numSV
- pDOP if available in NAV-PVT

POSITION:
- lon
- lat
- height
- hMSL

POSITION ACCURACY:
- hAcc
- vAcc

VELOCITY:
- velN
- velE
- velD
- gSpeed
- headMot

VELOCITY ACCURACY:
- sAcc
- headAcc

Convert values only for presentation.

Internally keep native integer units whenever practical.

For example:
- do not store speed internally as float km/h if the GNSS gives an
  integer physical unit
- convert to km/h only when displaying it

Most importantly:

USE gSpeed FROM NAV-PVT AS THE GNSS SPEED SOURCE.

Do NOT estimate speed by differentiating latitude/longitude positions.

==================================================
STEP 7 - LIVE GNSS STATE
==================================================

Create a clean structure representing the most recent GNSS solution,
for example conceptually:

    gnss_solution_t

It should contain at least:

    uint32_t iTOW
    fix type / validity
    uint8_t numSV

    int32_t latitude
    int32_t longitude

    int32_t gSpeed
    uint32_t sAcc

    uint32_t hAcc
    uint32_t vAcc

    int32_t headMot

    local UART reception timestamp if useful

    update counter

Protect access between GNSS and display tasks properly.

Do not let the OLED task parse UART data directly.

The flow should be:

    UART
      |
      v
    UBX parser
      |
      v
    GNSS solution state
      |
      +----> OLED

==================================================
STEP 8 - DISPLAY PAGE
==================================================

Keep the existing SH1106 display support.

Replace the old NMEA-oriented status screen with a UBX-oriented GNSS
diagnostics screen.

A useful layout could be similar to:

    GNSS M10
    FIX: 3D   SV: 14
    RATE: 10.0 Hz
    SPD: 0.00 km/h
    sAcc: 0.05 km/h
    hAcc: 1.2 m
    PPS: OK

Because the screen is small, use multiple diagnostic pages if necessary.

For example:

PAGE 1:
    FIX
    satellites
    speed
    speed accuracy
    actual NAV rate
    PPS status

PAGE 2:
    latitude
    longitude
    hAcc
    vAcc
    pDOP

PAGE 3:
    UART baud
    UBX good packets
    checksum errors
    PPS period
    receiver version

It is fine to rotate pages automatically every few seconds for now.

Do not spend excessive time making the UI beautiful.

==================================================
STEP 9 - MEASURE ACTUAL NAVIGATION RATE USING iTOW
==================================================

Do not trust the configured rate blindly.

Determine actual navigation rate from consecutive valid NAV-PVT iTOW
values.

For example:

1 Hz:
    delta iTOW = 1000 ms

10 Hz:
    delta iTOW = 100 ms

20 Hz:
    delta iTOW = 50 ms

25 Hz:
    delta iTOW = 40 ms

Keep statistics such as:

    last_iTOW
    delta_iTOW
    measured_rate_hz
    min_delta
    max_delta
    unexpected_delta_count

Display the measured rate on the OLED and periodically print statistics
to the console.

Do NOT calculate the navigation rate from UART packet arrival timestamps
unless only used as a secondary diagnostic.

iTOW is the primary source for navigation epoch spacing.

==================================================
STEP 10 - SAFE UART BAUD CHANGE
==================================================

After NAV-PVT works reliably at 1 Hz, change the GNSS UART from:

    38400
to
    115200

Use the correct M10 CFG-VALSET key.

Make the change RAM-only.

Implement the transition carefully:

1. send the UART configuration command at the old baud
2. account for the fact that the receiver may immediately begin using
   the new baud
3. reconfigure ESP32 UART1 to 115200
4. reacquire the UBX stream
5. verify NAV-PVT reception
6. query the receiver configuration again to confirm 115200

Do not permanently save 115200 yet.

If the transition fails, provide a recovery path:
- restart / power cycle GNSS
- reconnect at the factory 38400 setting

The code should not brick our communication because of a failed
experimental configuration.

==================================================
STEP 11 - 10 HZ TEST
==================================================

Once UART 115200 and NAV-PVT are stable:

Configure the measurement/navigation cycle for 10 Hz using the exact
official CFG-RATE keys.

The expected navigation epoch spacing is:

    100 ms

Keep NAV-PVT at one message per navigation solution.

Verify for a meaningful number of epochs, e.g. at least several hundred
NAV-PVT packets.

Report:
- expected interval
- average measured iTOW delta
- minimum delta
- maximum delta
- number of missed/unexpected epochs
- UBX checksum errors
- UART overflow/errors if any

The OLED must remain responsive during this test.

==================================================
STEP 12 - GPS + GALILEO AT 20 HZ
==================================================

After 10 Hz works reliably, configure a mode suitable for the final
performance meter:

    GPS + Galileo
    20 Hz navigation update rate

Use official u-blox M10 constellation configuration keys.

Do not assume the currently enabled constellations.

Query them first.

For the intended 20 Hz mode, configure only the constellation combination
that the official M10 documentation says supports 20 Hz.

The expected NAV-PVT iTOW increment is:

    50 ms

Verify it empirically.

Again collect statistics over several hundred navigation epochs.

The firmware should clearly report something such as:

    Requested rate: 20 Hz
    Measured rate:  20.00 Hz
    iTOW delta:     50 ms
    Unexpected:     0
    UBX CRC errors: 0

Do not claim successful 20 Hz operation just because CFG-VALSET returned
success.

The iTOW sequence must prove it.

==================================================
STEP 13 - OPTIONAL GPS-ONLY 25 HZ MODE
==================================================

Only after 20 Hz GPS + Galileo works correctly:

Add an optional test mode for:

    GPS only
    25 Hz

Expected iTOW increment:

    40 ms

This should be selectable in code/configuration rather than replacing
the 20 Hz mode permanently.

The purpose is to compare:

    GPS + Galileo @ 20 Hz

versus

    GPS only @ 25 Hz

Do not decide yet which one is better for the final device.

We will test real-world satellite availability and speed quality later.

==================================================
STEP 14 - REDUCE NMEA OUTPUT
==================================================

Only after UBX-NAV-PVT has been proven reliable:

Temporarily disable unnecessary NMEA output on UART1.

Do this with RAM-only CFG-VALSET changes.

Ideally the high-rate UART stream should mainly contain:

    UBX-NAV-PVT at every navigation epoch

Optionally retain or enable low-rate diagnostic UBX messages if genuinely
useful.

Do not leave GGA/GSA/GSV/RMC/VTG/GLL flooding the UART at 20 Hz.

Keep the parser capable of handling NMEA anyway, because it is useful
for recovery and diagnostics.

==================================================
STEP 15 - SATELLITE DIAGNOSTICS
==================================================

After NAV-PVT is stable, add a lower-rate satellite diagnostic message if
appropriate for M10, such as UBX-NAV-SAT or the current documented
equivalent.

This does NOT need to run at 20 Hz.

Approximately 1 Hz is enough.

I want to be able to inspect:
- satellites visible
- satellites used in navigation
- constellation
- C/N0
- elevation
- azimuth
- health/quality/use flags where available

Calculate useful diagnostics such as:
- visible satellite count
- used satellite count
- maximum C/N0
- average C/N0 of used satellites

This will later help compare antenna placement in the car.

Keep satellite diagnostics separate from the NAV-PVT high-rate path.

==================================================
PPS
==================================================

Keep the existing GPIO4 PPS implementation.

For this iteration:

- rising edge ISR
- record esp_timer_get_time()
- maintain pulse counter
- calculate period between consecutive PPS edges
- maintain recent min/max/average PPS period if convenient

The ISR must remain very small.

Do NOT implement full GNSS/ESP clock discipline or sensor synchronization
yet.

Do NOT change the GNSS TIMEPULSE configuration unless there is a specific
reason and it is documented.

For now we only want to confirm that PPS remains healthy while the
receiver runs at 10/20/25 Hz.

==================================================
CONFIGURATION SAFETY
==================================================

This is extremely important.

During this second iteration, use volatile / RAM configuration wherever
possible.

Do NOT write experimental configuration into persistent GNSS storage.

I want a power cycle to return the module to its known factory/recoverable
configuration unless we deliberately decide otherwise later.

Before every major reconfiguration, print the old values.

After every major reconfiguration, query the values back and print the
new values.

For example:

    Before:
      baud = 38400
      rate = 1 Hz

    Applying RAM configuration...

    After:
      baud = 115200
      rate = 10 Hz

If configuration verification fails, report it.

==================================================
ERROR HANDLING
==================================================

Handle at least:

- UBX checksum failure
- malformed UBX length
- packet timeout
- ACK-NAK
- no response to poll
- UART receive overflow
- GNSS disconnected
- no GNSS fix
- lost GNSS fix
- PPS missing
- iTOW jumps unexpectedly
- wrong actual navigation rate
- UART baud transition failure

"No GNSS fix" must NOT stop protocol/configuration testing.

Many of these tests will be performed indoors.

==================================================
CONSOLE OUTPUT
==================================================

Do not print every NAV-PVT packet at 20 Hz forever because that creates
unnecessary serial output and can disturb timing.

Instead:

- detailed packet output can be enabled with a DEBUG option
- normal mode should print a GNSS status summary approximately once per
  second
- print errors immediately
- print rate statistics periodically

Example:

    GNSS: UBX NAV-PVT OK
    FIX=3D SV=12
    speed=0.012 m/s
    sAcc=0.041 m/s
    hAcc=1.35 m
    iTOW=345678900
    rate=20.00 Hz
    UBX good=15432 bad_crc=0
    PPS=1000001 us

==================================================
CODE STRUCTURE
==================================================

Please inspect and reuse the existing first-iteration project.

Do NOT unnecessarily rewrite the working SH1106, I2C, UART, or PPS code.

Refactor where useful.

A reasonable structure might be:

    main/
        main.c

        gnss/
            gnss.c
            gnss.h

            ubx_parser.c
            ubx_parser.h

            ubx_protocol.c
            ubx_protocol.h

            ubx_config.c
            ubx_config.h

        display/
            display.c
            display.h

Do not over-engineer it.

The important separation is:

    UART transport
    UBX framing/parser
    UBX message decoding
    receiver configuration
    current GNSS state
    display

Avoid one giant source file.

==================================================
TEST / IMPLEMENTATION ORDER
==================================================

Please implement and validate in small steps.

Do NOT make all configuration changes at once.

Use this exact progression:

PHASE A:
    existing NMEA @ 38400 / 1 Hz
    +
    working generic UBX parser

PHASE B:
    query receiver identification
    query current CFG values

PHASE C:
    enable NAV-PVT @ 1 Hz
    keep NMEA
    keep 38400

PHASE D:
    decode NAV-PVT completely enough for diagnostics

PHASE E:
    switch UART to 115200
    verify communication

PHASE F:
    NAV-PVT @ 10 Hz
    verify using iTOW

PHASE G:
    GPS + Galileo @ 20 Hz
    verify using iTOW

PHASE H:
    disable unnecessary NMEA output in RAM

PHASE I:
    optional GPS-only @ 25 Hz test

PHASE J:
    add low-rate satellite/CN0 diagnostics

At every phase:
- build
- fix compiler warnings/errors
- tell me exactly what changed
- tell me exactly what I should observe on hardware

Do not move to the next phase automatically if actual hardware feedback
from me is required.

==================================================
WHAT I WANT FROM YOU NOW
==================================================

1. Inspect the existing first-iteration project.
2. Preserve the parts that already work.
3. Review the official u-blox M10 protocol documentation.
4. Design the second-iteration GNSS architecture.
5. Implement PHASE A through the next safe hardware-test checkpoint.
6. Build the firmware and fix all compile errors.
7. Tell me exactly what you implemented.
8. Give me exact flash/monitor commands.
9. Tell me what output I should expect.
10. Stop at a sensible checkpoint and wait for my real hardware results
    before making the next risky configuration change.

Do not claim a receiver configuration works until it has been verified on
my physical module.

The purpose of this iteration is not to finish the automotive performance
meter.

The purpose is to fully understand, control, validate, and characterize
the Quescan G10A-F30 / u-blox M10 GNSS subsystem first.