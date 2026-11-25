#pragma once
// Official device provisioning endpoint. OTA firmware URLs returned by this
// endpoint are deliberately ignored: they would replace the Factory firmware.
static constexpr const char *FactoryXiaozhiOta = "https://api.tenclass.net/xiaozhi/ota/";
// For a self-hosted XiaoZhi server, set a WebSocket URL here; empty uses the
// official activation/configuration flow. Never put Wi-Fi credentials here.
static constexpr const char *FactoryXiaozhiServer = "";
static constexpr const char *FactoryXiaozhiToken = "";
