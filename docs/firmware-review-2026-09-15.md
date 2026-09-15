[简体中文](firmware-review-2026-09-15.zh_CN.md) · English

# Passport OS firmware review, 2026-09-15

Base: `manchunx7-bit/passport-os`, `main`, `e5dd58223d62c2be5de6ffcb370b0fcb7b04b11d`.

This is a source patch awaiting a full ESP-IDF build and hardware acceptance. Initial publication was blocked by automatic approval review; the user subsequently authorized a feature branch, draft PR and firmware build. There is no verified flashable binary.

## Changes

- Centralize terminal colours across boot, home, launcher, settings, both voice applications, radio, Wi-Fi, games and the browser configuration pages. Use dark green surfaces, readable phosphor text, square frames and amber focus. Preserve meaningful artwork and game-piece colours.
- Remove shared animated landscape backgrounds and their retained object pointers. Keep the existing clock-theme API with a stable dark palette.
- Repair the boot title reveal, voice title/status-bar overlap and clock/battery overlap. Bound the shared title/footer widgets. Use the existing broader Chinese font for SSIDs.
- Consume an entire wake gesture; serialize input with app lifecycle changes using a recursive mutex; publish sleep state only after successful display operations.
- Bound Xiaozhi retained text to 1200 bytes at UTF-8 boundaries, for both assignment and streaming appends.
- Fix the missing profile-save token and avatar authorization check, return HTTP 401 correctly, reject oversized fields, bound structure-string reads, and invalidate stale city coordinates.
- Publish Wi-Fi task handles before allowing workers to execute, recover the UI after task-creation failure, and recheck the active page before returning from successful configuration.
- Match the next Tetris piece preview to its actual colour.
- Restore shell executable bits and the missing host-test entry points. Keep existing repository governance checks intact.

The upload token is delivered by the device landing page. This is an interface consistency repair, not a replacement for authentication or secure pairing.

## Validation

`bash tools/test-host.sh` passes: animation boundaries, wake-event sequences, UTF-8/emoji/oversized text, 200 Tetris seeds, execution of the actual profile-page save handler, and synthetic firmware protection fixtures. Embedded JavaScript syntax, shell syntax, and diff whitespace checks pass. Materialized base source hashes match the remote tree.

The repository audit still fails on pre-existing missing governance documents, dependency lock, broken links and language peers. These checks were not removed. The original firmware job `104394330260` failed with `./tools/validate.sh: Permission denied`, before compilation.

No complete ESP-IDF build, actual LVGL screenshot, or on-device test has been performed. Local tools lack ESP-IDF 5.5.3, and the publication block prevents CI for this revision. Host fixtures do not validate an actual firmware image.

## Remaining work

Review SPIFFS auto-format-on-mount-failure and avatar replacement recovery; add explicit Wi-Fi session lifetimes for workers that survive stop timeouts; check weather snapshot concurrency and numerical bounds; improve allocation/storage failure feedback; restore accurate upstream project documents and dependency locks.

Acceptance requires a complete ESP32-C3 build with protected partition verification, all-page inspection at 240x320, wake tests for all keys, at least 30 app-switch cycles, interrupted profile uploads, Wi-Fi failure/re-entry tests, long voice replies, ten-minute radio playback, and both games. Build success and hardware success must remain separate claims. The Chinese companion provides the detailed issue table and acceptance sequence.
