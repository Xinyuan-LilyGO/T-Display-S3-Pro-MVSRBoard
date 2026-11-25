# Factory Opus configuration

Vendored from https://github.com/pschatzmann/arduino-libopus (`main` archive,
downloaded 2026-11-18), which packages Opus 1.3.1. The archive SHA-256 is
`b1bd5609bb509a1c9c6cebeb047c46a97bde8a6aecf077def12f3cec4696bbfe`.
The package manifest's upstream version is 1.3.2; this does not mean Opus 1.3.2.
Original licenses remain under `src/opus-1.3.1/COPYING` and source headers.

Factory changes:

- Compile only portable Opus/CELT/SILK sources, excluding demos, tests, float
  SILK and architecture-specific implementations. Ogg is not used by Xiaozhi.
- Fixed-point codec with float API disabled, shared 120,000-byte pseudostack.
- Factory allocates/checks that scratch arena in PSRAM before starting workers.
  **All** codec calls (encoder and decoder, including creation/control/destruction)
  run under one mutex because the pseudostack is not thread safe.
- Microphone input: 16 kHz mono, 60 ms packets, 24 kbps VOIP, complexity 0.
  Speaker decoding accepts the sample rate negotiated by the server.

`python3 tools/test_factory_voice.py` on Linux/WSL checks framing with ASan/UBSan
and exercises actual fixed-point encode/decode with ASan at 16/24/48 kHz.
The existing ESP32-audioI2S Opus decoder cannot replace this library because
Xiaozhi also requires a voice encoder and SILK decoding.
