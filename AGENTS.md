# GNSS Performance Meter

## Project shape and current scope

This repository contains two separate tracks:

- `main/`, `CMakeLists.txt`, `sdkconfig*`, `README.md`, `TASK.md`, `ПРОЕКТ.md`, and `AUDIT_GNSS.md` are the ESP-IDF firmware and its hardware documentation.
- `site-gnss-dashboard/` is the active interface and UX prototype. It is a static Sites web application: `dist/index.html`, `dist/app.css`, and ES modules in `dist/*.mjs`. It does not use a frontend framework or a build step.
- `android-app/` is an earlier Kotlin/Compose experiment. Do not develop or port Android in ordinary UI tasks. The web prototype is the source of truth for approved UX until the owner explicitly starts a separate Java/Android Studio port.

The present workstream is the web prototype. Do not change ESP32 firmware, GNSS configuration, hardware pin allocation, or Android code unless the user explicitly asks for that track.

## Useful commands

From `site-gnss-dashboard/`:

```powershell
python -m http.server 8765 --bind 127.0.0.1 --directory dist
node --test tests/engine.test.mjs
```

The first command starts a local static preview at `http://127.0.0.1:8765/`. The second runs the small deterministic simulator test suite. Sites hosting is configured in `site-gnss-dashboard/.openai/hosting.json`; keep its `dist/` directory as the publishable site root.

For a specifically requested firmware change, use the existing documented Windows check:

```powershell
build_idf.bat build
```

## Web prototype architecture

- `engine.mjs` contains the device-domain simulator, persistent records, measurement and trip state, and the `SimulatorAdapter` transport seam for a future device connection.
- `app.mjs` renders the UI and routes user actions through the adapter. Do not place device-domain decisions only in DOM handlers.
- `chart.mjs` holds chart segmentation, decimation, and cursor helpers. Preserve telemetry gaps as gaps; do not turn them into zero values.
- The demo panel is part of the verification surface. Prefer its deterministic scenarios to ad-hoc manual manipulation when reproducing UI states.
- Browser data is deliberately stored locally for this prototype. Do not add Android, BLE, or device protocol dependencies as part of UI work.

Future transports must remain behind an adapter. The user interface should consume published device state, not a browser-specific transport implementation.

## Hardware context for future firmware tasks

- MCU: ESP32-S3-N16R8, 16 MB flash and 8 MB PSRAM. GPIO19 and GPIO20 are reserved for native USB.
- GNSS: Quescan G10A-F30 / u-blox M10; UART1 is 38400 8N1, ESP TX GPIO17 to GNSS RX, GNSS TX to ESP RX GPIO18, PPS GPIO4.
- Engineering display: 1.3-inch 128x64 SH1106 OLED at I2C 0x3C, SDA GPIO8 and SCL GPIO9.
- A final 2-inch IPS display is planned, but its controller, resolution, pinout, voltage, and bus are not confirmed. Never assume them or allocate pins without documentation.

The firmware remains in a GNSS-diagnostics milestone. `AUDIT_GNSS.md` records the accepted technical constraints. Any firmware task must preserve its one-UART-consumer rule, coherent GNSS snapshots, explicit freshness/validity states, and RAM-only receiver configuration unless the owner explicitly authorizes a persistent change.

## Multi-agent workflow

The primary agent owns user intent, scope, architecture decisions, implementation, fixes, and the final explanation.

Use subagents only for bounded, independent work that saves primary-model time:

- repository exploration and targeted regression checks;
- routine browser verification, screenshots, and repetitive interactions;
- focused code review of an implementation diff.

For a meaningful UI change:

1. Read only the relevant code and implement the requested behavior.
2. Run the smallest applicable static or deterministic check.
3. Spawn `ui_verifier` for one targeted browser pass.
4. In parallel, spawn `code_reviewer` when the change is nontrivial.
5. Fix only real findings, then recheck only the affected behavior.

Do not automatically run this cycle for a small text, color, padding, typo, or documentation edit. Do not spawn agents merely to demonstrate multi-agent usage. Use at most two or three subagents at once.

The primary agent must not spend a long turn on repeated browser/computer-use clicks when `ui_verifier` can perform the work. If that verifier is unavailable or cannot access browser tools, do not silently substitute a long expensive browser pass; report the limitation to the user.

## Verification and reporting

- Do not rerun checks that already passed unless relevant code changed.
- Keep browser verification to the screen and states changed by the task; use one pass and one to three screenshots in ordinary work.
- Do not perform unrelated visual QA, full-site walkthroughs, or publication work in a configuration/bootstrap task unless explicitly asked.
- State what was actually checked. Never claim hardware behavior from a compile-only result.
