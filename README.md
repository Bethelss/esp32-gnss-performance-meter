# ESP32 GNSS Performance Meter

Early prototype of an automotive GNSS performance meter built around an ESP32-S3 and a u-blox M10-class receiver.

> **Prototype status:** this repository does not claim completed hardware, BLE, or production validation.

## Project tracks

- `main/` contains the ESP-IDF firmware, currently focused on trustworthy GNSS diagnostics.
- `site-gnss-dashboard/` is the active static web UX prototype and the current source of truth for the approved interface.
- `android-app/` is an earlier Kotlin/Compose experiment retained as source code; it is not the active UI workstream.

Supporting hardware constraints and engineering findings are documented in `AGENTS.md`, `AUDIT_GNSS.md`, `TASK.md`, and `ПРОЕКТ.md`.

## Run the dashboard

```powershell
python -m http.server 8765 --bind 127.0.0.1 --directory site-gnss-dashboard/dist
```

Open `http://127.0.0.1:8765/` in a browser. Run its deterministic simulator tests with:

```powershell
node --test site-gnss-dashboard/tests/engine.test.mjs
```
