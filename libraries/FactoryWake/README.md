# FactoryWake

Local, offline `你好小智` wake detection for the Factory application on ESP32-S3.
This library targets Arduino-ESP32 2.0.x / ESP-IDF 4.4 with PSRAM. It is not an
ESP-IDF 5/6 migration, and the Xtensa archives do not support other chips.

Sources:

- Runtime archives and `esp_wn_iface.h` / `esp_wn_models.h` from Espressif's
  Arduino-ESP32 **2.0.9**, `tools/sdk/esp32s3`: `libwakenet`, `libesp-sr`,
  `libdl_lib`, `libc_speech_features`, `libhufzip`.
  https://github.com/espressif/arduino-esp32/tree/2.0.9/tools/sdk/esp32s3
- Model from ESP-SR **v1.2.0**, `model/wakenet_model/wn9_nihaoxiaozhi`:
  https://github.com/espressif/esp-sr/tree/v1.2.0/model/wakenet_model/wn9_nihaoxiaozhi
  Model info: `wakenet9_v1h12_nihaoxiaozhi_3_0.585_0.595`.
- Espressif's license is retained in `LICENSE`. Its additional restriction
  limits use to Espressif hardware. Embedded model and archive SHA-256 hashes
  are recorded in `checksums.json`.

`FactoryWakeModel.h` embeds the three model files in flash. `FactoryWake.cpp`
provides a read-only VFS so the IDF 4.4 recognizer can load them without a
SPIFFS partition, filesystem formatting, or a separate data upload.
Only the microphone task calls this recognizer. Do not call it concurrently.

`build.py` attaches the archives using this library's PlatformIO extra script;
the project's `platformio.ini` does not need changes. This integration is tested
with PlatformIO/espressif32 6.5.0, Arduino-ESP32 2.0.14. Arduino IDE does not run
the extra script and is **not** a supported build route for Factory with Xiaozhi.

`tools/prepare_factory_voice.py` rebuilds the embedded arrays from the downloaded
inputs listed in its docstring and an installed 2.0.9 SDK. Existing Opus files
are preserved. Normal users should use the committed assets directly.
