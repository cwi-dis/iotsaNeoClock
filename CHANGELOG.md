# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Automatic rain forecast (buienradar.nl) driving the outer status ring, on clocks that have it enabled (#7)

### Changed

- Migrated to iotsa v3 API (`IotsaModule` base class, `lateSetup()`, `webHandler()`, bare `api.setup()` paths) (cwi-dis/iotsa#199)
- `platformio.ini` now uses vendored `iotsa-board-*.ini` for board definitions instead of hand-copied board sections (cwi-dis/iotsa#224); esp8266 env renamed `nodemcuv2` → `iotsa_v4`
- esp8266: dropped the `4m3m` flash layout for the nodemcuv2 default (`4m1m`) — restores OTA headroom (the clock only needs ~15KB of LittleFS)
- esp32c3 (crowpanel): now built with `-DESP32C3`, which was previously missing
