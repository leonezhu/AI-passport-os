[简体中文](README.zh_CN.md) · English

# Passport OS

## Why this project exists

I started this project to turn the FoloToy AI Passport into a small device I would actually use every day and keep reaching for. After several rounds of selection and iteration, I collected ten of the functions and apps that fit my own daily use, while keeping enough room for experimentation and play.

Thank you to everyone who has enjoyed the project and shared feedback. Many people have also suggested personal improvements. Because each person uses the device differently, open-sourcing the project is the most useful next step: you can DIY your own version, add the features you need, or remove the ones you do not.

I hope this code and firmware give you a practical starting point for your own build, and that you have fun using and modifying it.

Community firmware for the FoloToy AI Passport based on ESP32-C3 with 8 MB
flash. It includes the multi-app UI, Wi-Fi setup, Xiaozhi AI, radio, games, and
the Windows hardware-microphone/hotkey bridge integration.

## Install

Download `FoloToy-AI-Passport-full.bin` from the latest GitHub Release and
write the merged image at address `0x0`. Confirm that the target is the same
ESP32-C3 / 8 MB AI Passport hardware revision before flashing. A full-image
flash may replace the device's local Wi-Fi and application settings; back up
anything you need first.

An `-app.bin` image is written at `0x10000` only after verifying an identical
partition layout for an existing Passport OS installation. Do not erase the
whole flash first. A `-full.bin` image at `0x0` includes blank NVS/PHY bytes and
overwrites Wi-Fi, settings and image indexes in that region; it is not a
data-preserving upgrade.

The release workflow builds with ESP-IDF 5.5.3, verifies the merged image and
protected partition layout, then publishes that exact binary. Build success is
not a substitute for testing audio, buttons, battery reporting and networking
on the physical device.

For the Windows hardware microphone and right-Shift/Enter bridge, see
[folo-ai-passport-voice](https://github.com/manchunx7-bit/folo-ai-passport-voice).

## Privacy

Release firmware is built from tracked source and does not contain the
maintainer's Wi-Fi password, API keys or local device settings. User settings
are created on the target device after installation. Never commit local
credentials or configuration exports.

## Build and verify

```bash
source <ESP-IDF-5.5.3>/export.sh
./tools/validate.sh --static
./tools/validate.sh --firmware
```

The firmware target is `esp32c3`. The merged image is written to
`build/FoloToy-AI-Passport-full.bin`.

For a local candidate, `bash build.sh release` builds in `build-public`. Both
this path and CI verify that public mode is enabled and proxy defaults are
empty. After validating that version, create a local candidate package:

```bash
python3 tools/package_firmware.py --date YYYY-MM-DD --verification /path/to/verification-public.json
```

Use public-safe verification JSON matching the firmware version, never device
logs or configuration exports. Packaging does not publish a GitHub Release.
The source ZIP and per-file hashes identify the exact working-tree snapshot;
a dirty checkout's old HEAD alone does not identify the candidate. A release
tag should point to a corresponding finalized commit.

## Compatibility and risk

- Supported target: matching FoloToy AI Passport, ESP32-C3, 8 MB flash.
- Do not flash onto merely similar-looking boards with a different flash size,
  display, pin map, codec, battery circuit or partition table.
- Keep a known-good recovery image. Power loss, a wrong port, or an incorrect
  target during flashing can require recovery.
- Community users must configure their own Wi-Fi and service credentials.

