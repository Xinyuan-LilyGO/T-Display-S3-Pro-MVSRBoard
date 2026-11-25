#include "FactoryXiaozhi.h"
#include "FactoryXiaozhiConfig.h"
#include "FactoryXiaozhiCertificates.h"
#include "FactoryXiaozhiProtocol.h"
#include "FactoryXiaozhiInput.h"
#include "FactoryNetwork.h"
#include "FactoryText.h"
#include "FactoryVolume.h"
#include "pin_config.h"
#include <FactoryWake.h>
#include <opus.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <cJSON.h>
#include <driver/i2s.h>
#include <esp_websocket_client.h>
#include <esp_system.h>
#include <freertos/event_groups.h>
#include <atomic>
#include <memory>
#include <math.h>
#include <esp_heap_caps.h>
#include <mqtt_client.h>
#include <WiFiUdp.h>
#include <mbedtls/aes.h>
#include <sys/time.h>
#include "FactoryXiaozhiState.h"

#if !defined(CONFIG_SPIRAM_MODE_OCT) || !CONFIG_SPIRAM_MODE_OCT
#error "Factory Xiaozhi requires OPI PSRAM: use boards/esp32s3_flash_16MB.json build.arduino.memory_type=qio_opi, then clean and rebuild."
#endif

// Opus' bounded shared scratch arena is protected by codecLock.
extern "C" { extern char *scratch_ptr; extern char *global_stack; }

namespace FactoryXiaozhi {
namespace {
enum class Phase : uint8_t { Starting, Offline, Binding, Idle, Connecting, Listening, Thinking, Speaking, Error };
struct ScreenState {
    Phase phase = Phase::Starting;
    char detail[160] = "正在启动本地语音唤醒";
    char activation[32] = {};
    char heard[240] = {}, reply[480] = {};
    char audioError[128] = {};
};
struct Packet {
    uint32_t epoch;
    uint16_t length;
    uint8_t bytes[FactoryXiaozhiProtocol::MaxPacket];
};
// Only the network worker accesses these buffers. Keep them off the TLS task
// stack, and allocate them in PSRAM only when audio is available.
struct NetworkAudioBuffers {
    Packet outgoing, incoming;
    uint8_t transmit[FactoryXiaozhiProtocol::MaxPacket+16];
    uint8_t receive[FactoryXiaozhiProtocol::MaxPacket+16];
};
NetworkAudioBuffers *networkAudio = nullptr;
constexpr int MicRate = 16000, FrameSamples = 960;
// ESP-IDF task stack sizes are bytes. The linked Xtensa SILK encoder alone
// reserves 9,504 bytes in silk_encode_frame_FIX, excluding its callers.
// Opus' PSRAM pseudostack does not cover these ordinary C local variables.
constexpr uint32_t MicStackBytes = 32768, SpeakerStackBytes = 16384;
std::atomic<bool> armed{false}, foreground{false}, wakeRequested{false}, talkRequested{false};
std::atomic<bool> dialog{false}, capture{false}, speaking{false}, abortRequested{false};
std::atomic<bool> connected{false}, hello{false}, ttsStopped{false}, streamFault{false}, captureDone{false};
std::atomic<bool> micReady{false}, wakeReady{false}, hardwareFailed{false};
std::atomic<bool> inputFault{false}, cueRequested{false}, cuePlaying{false};
std::atomic<uint32_t> uploadedFrames{0}, receivedFrames{0}, playedFrames{0};
std::atomic<bool> retryConfiguration{false}, audioStarted{false};
std::atomic<int> serverRate{24000};
std::atomic<uint32_t> epoch{0};
std::atomic<uint32_t> releaseRequest{0}, speakerReleased{0}, managerReleased{0};
portMUX_TYPE uiLock = portMUX_INITIALIZER_UNLOCKED;
ScreenState shared, painted;
QueueHandle_t outgoing = nullptr, incoming = nullptr;
StaticQueue_t outgoingControl, incomingControl;
uint8_t *outgoingStorage = nullptr, *incomingStorage = nullptr;
SemaphoreHandle_t codecLock = nullptr;
esp_websocket_client_handle_t websocket = nullptr; // Manager task owns lifecycle.
FactoryXiaozhiProtocol::Message message; // WebSocket callback owns assembly.
Arduino_GFX *screen = nullptr;
bool initialized = false, wifiPage = false, force = true;
uint32_t lastRender = 0;
int protocolVersion = 1;
char sessionId[128] = {};
String serverUrl, serverToken, clientId, deviceId;
Preferences settings;
bool provisioned = false;
std::atomic<bool> serviceReady{false};
bool useMqtt = false;
String mqttEndpoint, mqttId, mqttUsername, mqttPassword, mqttTopic;
int mqttKeepalive = 240;
esp_mqtt_client_handle_t mqtt = nullptr;
WiFiUDP udp;
IPAddress udpPeer;
char udpHost[128] = {};
uint16_t udpPort = 0;
uint8_t udpKey[16] = {}, udpNonce[16] = {};
uint32_t sendSequence = 0, receiveSequence = 0;
mbedtls_aes_context aes;
bool shownArmed = false, shownWake = false, shownBusy = false, shownInputFault = false;

void microphonePower(bool enabled) {
#ifdef T_Display_S3_Pro_MVSRBoard_V1_1
    digitalWrite(MP34DT05TR_EN, enabled ? LOW : HIGH);
#else
    digitalWrite(MSM261_EN, enabled ? HIGH : LOW);
#endif
}

template<size_t N> void copy(char (&target)[N], const char *value) {
    if (!value) value = "";
    size_t count = strnlen(value, N - 1);
    if (value[count]) while (count && (uint8_t(value[count]) & 0xc0) == 0x80) --count;
    memcpy(target, value, count); target[count] = 0;
}
void status(Phase phase, const char *detail) {
    portENTER_CRITICAL(&uiLock);
    shared.phase = phase; copy(shared.detail, detail);
    portEXIT_CRITICAL(&uiLock);
}
void audioError(const char *detail) {
    portENTER_CRITICAL(&uiLock); copy(shared.audioError,detail); portEXIT_CRITICAL(&uiLock);
    Serial.printf("[Xiaozhi] Audio: %s; internal=%u largest=%u PSRAM=%u\n",detail,
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),ESP.getFreePsram());
}
const char *field(const cJSON *object, const char *key) {
    const auto *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}
int number(const cJSON *object, const char *key, int fallback) {
    const auto *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}
void micTask(void *) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    i2s_config_t cfg = {};
    cfg.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX);
#ifdef T_Display_S3_Pro_MVSRBoard_V1_1
    cfg.mode = i2s_mode_t(cfg.mode | I2S_MODE_PDM);
#endif
    cfg.sample_rate = MicRate; cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 6; cfg.dma_buf_len = 512;
    cfg.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;
    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_PIN_NO_CHANGE; pins.data_out_num = I2S_PIN_NO_CHANGE;
#ifdef T_Display_S3_Pro_MVSRBoard_V1_1
    pins.bck_io_num = I2S_PIN_NO_CHANGE; pins.ws_io_num = MP34DT05TR_LRCLK; pins.data_in_num = MP34DT05TR_DATA;
    pinMode(MP34DT05TR_EN, OUTPUT); digitalWrite(MP34DT05TR_EN, LOW);
#else
    pins.bck_io_num = MSM261_BCLK; pins.ws_io_num = MSM261_WS; pins.data_in_num = MSM261_DATA;
    pinMode(MSM261_EN, OUTPUT); digitalWrite(MSM261_EN, HIGH);
#endif
    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) {
        microphonePower(false); hardwareFailed.store(true);
        audioError("麦克风初始化失败，请重启设备"); vTaskDelete(nullptr); return;
    }
    if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
        microphonePower(false); hardwareFailed.store(true);
        i2s_driver_uninstall(I2S_NUM_0); audioError("麦克风引脚配置失败，请重启"); vTaskDelete(nullptr); return;
    }
#ifdef T_Display_S3_Pro_MVSRBoard_V1_1
    // MP34DT05-A requires a 1.2--3.25 MHz clock (datasheet, tables 2/4).
    // IDF 4.4 defaults to DSR_8S: 16 kHz * 64 = 1.024 MHz, out of spec.
    // DSR_16S gives 2.048 MHz while retaining 16 kHz PCM for WakeNet/Opus.
    const esp_err_t pdmClock = i2s_set_pdm_rx_down_sample(I2S_NUM_0, I2S_PDM_DSR_16S);
    if (pdmClock != ESP_OK) {
        Serial.printf("[Xiaozhi] PDM clock setup failed: %s\n", esp_err_to_name(pdmClock));
        i2s_driver_uninstall(I2S_NUM_0); microphonePower(false); hardwareFailed.store(true);
        audioError("麦克风时钟配置失败，请重启"); vTaskDelete(nullptr); return;
    }
    Serial.printf("[Xiaozhi] MP34DT05-A: PCM=%d Hz, PDM=%d Hz, CLK=%d DATA=%d EN=%d (LOW=on)\n",
        MicRate, MicRate*128, MP34DT05TR_LRCLK, MP34DT05TR_DATA, MP34DT05TR_EN);
#endif
    bool wakeOk = FactoryWake::begin();
    int chunk = wakeOk ? FactoryWake::samples() : 512;
    if (chunk <= 0 || chunk > 2048) { FactoryWake::end(); wakeOk = false; chunk = 512; }
    wakeReady.store(wakeOk);
    Serial.printf("[Xiaozhi] WakeNet=%s, chunk=%d, heap=%u, PSRAM=%u\n",
        wakeOk ? "ready" : "unavailable", chunk, ESP.getFreeHeap(), ESP.getFreePsram());
    std::unique_ptr<int16_t[]> raw(new (std::nothrow) int16_t[chunk * 2]);
    std::unique_ptr<int16_t[]> mono(new (std::nothrow) int16_t[chunk]);
    std::unique_ptr<int16_t[]> frame(new (std::nothrow) int16_t[FrameSamples]);
    std::unique_ptr<Packet> packetStorage(new (std::nothrow) Packet{});
    int error = 0;
    xSemaphoreTake(codecLock, portMAX_DELAY);
    OpusEncoder *encoder = opus_encoder_create(MicRate, 1, OPUS_APPLICATION_VOIP, &error);
    if (encoder) {
        opus_encoder_ctl(encoder, OPUS_SET_BITRATE(24000));
        opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(0));
        opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    }
    xSemaphoreGive(codecLock);
    if (!raw || !mono || !frame || !packetStorage || !encoder) {
        audioError("语音内存不足，请重启"); i2s_driver_uninstall(I2S_NUM_0);
        microphonePower(false); wakeReady.store(false); hardwareFailed.store(true);
        if (encoder) { xSemaphoreTake(codecLock, portMAX_DELAY); opus_encoder_destroy(encoder); xSemaphoreGive(codecLock); }
        raw.reset(); mono.reset(); frame.reset(); packetStorage.reset();
        FactoryWake::end(); vTaskDelete(nullptr); return;
    }
    micReady.store(true);
    bool running = true;
    int frameUsed = 0;
    uint32_t recordingEpoch = 0, started = 0, lastSpeech = 0;
    bool heardSpeech = false;
    Packet &packet = *packetStorage;
    uint32_t lastDiagnostic = 0;
    uint32_t badSamples = 0, goodSamples = 0, discardSamples = MicRate / 5;
    unsigned readFailures = 0;
    int activeLane = 0;
    for (;;) {
        if (!armed.load()) {
            if (running) {
                i2s_stop(I2S_NUM_0); running = false;
                microphonePower(false);
            }
            vTaskDelay(pdMS_TO_TICKS(30)); continue;
        }
        if (!running) {
            microphonePower(true);
            i2s_start(I2S_NUM_0); running = true; FactoryWake::clear();
            discardSamples = MicRate / 5; badSamples = goodSamples = 0;
        }
        size_t bytes = 0;
        const size_t wanted = chunk * 2 * sizeof(int16_t);
        const esp_err_t readResult = i2s_read(I2S_NUM_0, raw.get(), wanted, &bytes, pdMS_TO_TICKS(150));
        if (readResult != ESP_OK || bytes != wanted) {
            frameUsed = 0; goodSamples = 0;
            if (++readFailures >= 6) {
                inputFault.store(true);
                if (capture.load()) streamFault.store(true);
            }
            if (millis()-lastDiagnostic >= 5000) {
                lastDiagnostic = millis();
                Serial.printf("[Xiaozhi] mic read error=%s bytes=%u/%u\n",esp_err_to_name(readResult),unsigned(bytes),unsigned(wanted));
            }
            vTaskDelay(1); continue;
        }
        readFailures = 0;
        // Drain power/clock startup data and the local speaker cue before wake
        // detection; sleeping alone would leave those samples in the DMA queue.
        if (cuePlaying.load()) discardSamples = MicRate / 5;
        if (discardSamples) {
            discardSamples = discardSamples > unsigned(chunk) ? discardSamples-chunk : 0;
            frameUsed = 0; continue;
        }
        const auto left = FactoryXiaozhiInput::measure(raw.get(),chunk,0);
        const auto right = FactoryXiaozhiInput::measure(raw.get(),chunk,1);
        const int lane = FactoryXiaozhiInput::select(left,right,activeLane);
        if (millis()-lastDiagnostic >= 5000) {
            lastDiagnostic = millis();
            Serial.printf("[Xiaozhi] mic=%s levelL=%u levelR=%u acL=%u acR=%u dcL=%ld dcR=%ld spanL=%u spanR=%u lane=%d stack_free=%u/%u\n",
                capture.load() ? "recording" : dialog.load() ? "dialog" : "wake",
                unsigned(left.absolute),unsigned(right.absolute),unsigned(left.ac),unsigned(right.ac),
                long(left.dc),long(right.dc),unsigned(left.span),unsigned(right.span),lane,
                unsigned(uxTaskGetStackHighWaterMark(nullptr)),unsigned(MicStackBytes));
        }
        if (lane < 0) {
            goodSamples = 0; frameUsed = 0;
            badSamples += unsigned(chunk);
            if (badSamples >= unsigned(MicRate)) {
                badSamples = MicRate;
                if (!inputFault.exchange(true)) Serial.println("[Xiaozhi] Microphone has no valid PCM signal; check PDM clock/power/connection");
                if (capture.load()) streamFault.store(true);
            }
            vTaskDelay(1); continue;
        }
        activeLane = lane; badSamples = 0;
        goodSamples += unsigned(chunk);
        if (goodSamples >= unsigned(MicRate / 5)) {
            goodSamples = MicRate / 5;
            if (inputFault.exchange(false)) {
                FactoryWake::clear(); Serial.println("[Xiaozhi] Microphone signal recovered");
                portENTER_CRITICAL(&uiLock);
                if (shared.phase == Phase::Error && !strcmp(shared.detail,"麦克风无有效信号，请检查扩展板连接")) {
                    shared.phase = Phase::Idle; copy(shared.detail,"麦克风收音已恢复，可重新唤醒");
                }
                portEXIT_CRITICAL(&uiLock);
            }
        }
        if (inputFault.load()) { frameUsed = 0; continue; }
        for (int i = 0; i < chunk; ++i) mono[i] = raw[2*i+lane];
        if (!dialog.load()) {
            frameUsed = 0;
            if (wakeOk && !wakeRequested.load() && FactoryWake::detect(mono.get())) {
                Serial.println("[Xiaozhi] Wake word detected"); wakeRequested.store(true);
            }
            vTaskDelay(1); // WakeNet must leave time for touch/UI even if DMA is full.
            continue;
        }
        if (!capture.load() || speaking.load() || !foreground.load()) { frameUsed = 0; continue; }
        const uint32_t currentEpoch = epoch.load();
        if (recordingEpoch != currentEpoch) {
            recordingEpoch = currentEpoch; frameUsed = 0; heardSpeech = false; started = lastSpeech = millis();
            xSemaphoreTake(codecLock, portMAX_DELAY); opus_encoder_ctl(encoder, OPUS_RESET_STATE); xSemaphoreGive(codecLock);
        }
        const uint32_t energy = lane ? right.ac : left.ac;
        if (energy > 300) { heardSpeech = true; lastSpeech = millis(); }
        for (int i = 0; i < chunk; ++i) {
            frame[frameUsed++] = mono[i];
            if (frameUsed != FrameSamples) continue;
            packet.epoch = currentEpoch;
            xSemaphoreTake(codecLock, portMAX_DELAY);
            const int n = opus_encode(encoder, frame.get(), FrameSamples, packet.bytes, sizeof(packet.bytes));
            xSemaphoreGive(codecLock);
            frameUsed = 0;
            if (n > 0) {
                packet.length = n;
                if (xQueueSend(outgoing, &packet, 0) != pdTRUE) streamFault.store(true);
            } else streamFault.store(true);
        }
        if ((heardSpeech && millis() - lastSpeech > 900) || millis() - started > (heardSpeech ? 20000u : 10000u)) {
            capture.store(false); captureDone.store(true); frameUsed = 0;
        }
    }
}

bool installSpeaker(int rate) {
    i2s_config_t cfg = {};
    cfg.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX); cfg.sample_rate = rate;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT; cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1; cfg.dma_buf_count = 6; cfg.dma_buf_len = 256; cfg.tx_desc_auto_clear = true;
    esp_err_t result = i2s_driver_install(I2S_NUM_1, &cfg, 0, nullptr);
    if (result != ESP_OK) {
        Serial.printf("[Xiaozhi] Speaker install failed: %s\n",esp_err_to_name(result));
        audioError("扬声器初始化失败，请重启"); return false;
    }
    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_PIN_NO_CHANGE; pins.bck_io_num = MAX98357A_BCLK; pins.ws_io_num = MAX98357A_LRCLK;
    pins.data_out_num = MAX98357A_DATA; pins.data_in_num = I2S_PIN_NO_CHANGE;
    result = i2s_set_pin(I2S_NUM_1, &pins);
    if (result != ESP_OK) {
        Serial.printf("[Xiaozhi] Speaker pins failed: %s\n",esp_err_to_name(result));
        i2s_driver_uninstall(I2S_NUM_1); audioError("扬声器引脚配置失败"); return false;
    }
    i2s_zero_dma_buffer(I2S_NUM_1);
    return true;
}
bool writeSpeaker(const int16_t *stereo, size_t samples) {
    size_t offset = 0;
    while (offset < samples*4 && foreground.load() && !abortRequested.load()) {
        size_t written = 0;
        const esp_err_t result = i2s_write(I2S_NUM_1, reinterpret_cast<const uint8_t *>(stereo) + offset,
            samples*4 - offset, &written, pdMS_TO_TICKS(40));
        offset += written;
        if (result != ESP_OK || !written) {
            Serial.printf("[Xiaozhi] Speaker write failed: %s bytes=%u\n",esp_err_to_name(result),unsigned(written));
            streamFault.store(true); return false;
        }
    }
    return offset == samples*4;
}

void speakerTask(void *) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    bool installed = false;
    int rate = 0, decoderRate = 0;
    uint32_t decoderEpoch = 0;
    OpusDecoder *decoder = nullptr;
    std::unique_ptr<int16_t[]> pcm(new (std::nothrow) int16_t[5760]);
    std::unique_ptr<int16_t[]> stereo(new (std::nothrow) int16_t[11520]);
    std::unique_ptr<Packet> packetStorage(new (std::nothrow) Packet{});
    for (;;) {
        const uint32_t release = releaseRequest.load();
        if (!dialog.load() || !foreground.load() || abortRequested.load()) {
            if (installed) {
                digitalWrite(MAX98357A_SD_MODE, LOW); i2s_driver_uninstall(I2S_NUM_1); installed = false;
            }
            xQueueReset(incoming);
            // The same task/driver plays both the local cue and server audio.
            // This checks the speaker without Wi-Fi, STT or a server TTS reply.
            if (cueRequested.exchange(false) && foreground.load() && !abortRequested.load() &&
                !dialog.load() && !talkRequested.load() && stereo) {
                const int volume = FactoryVolume::get();
                cuePlaying.store(true);
                if (installSpeaker(24000)) {
                    constexpr int cueSamples = 4800, noteSamples = 1920;
                    for (int i = 0; i < cueSamples; ++i) {
                        const int pos = i % 2400;
                        float envelope = pos < noteSamples ? fminf(1.0f,fminf(pos/240.0f,(noteSamples-pos)/240.0f)) : 0;
                        const float frequency = i < 2400 ? 660.0f : 880.0f;
                        const int16_t value = int16_t(sinf(6.2831853f*frequency*pos/24000.0f)*envelope*4000.0f*volume/100.0f);
                        stereo[2*i] = stereo[2*i+1] = value;
                    }
                    digitalWrite(MAX98357A_SD_MODE,volume ? HIGH : LOW);
                    const bool sent = writeSpeaker(stereo.get(),cueSamples);
                    // Finish the remaining DMA audio before shutting down the amplifier.
                    for (int i = 0; i < 8 && foreground.load() && !abortRequested.load(); ++i) vTaskDelay(pdMS_TO_TICKS(10));
                    digitalWrite(MAX98357A_SD_MODE,LOW); i2s_driver_uninstall(I2S_NUM_1);
                    Serial.printf("[Xiaozhi] Speaker cue: %s, volume=%d\n",sent ? "written" : "interrupted",volume);
                }
                cuePlaying.store(false);
            }
            speakerReleased.store(release);
            vTaskDelay(pdMS_TO_TICKS(10)); continue;
        }
        if (!pcm || !stereo || !packetStorage) { streamFault.store(true); vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        Packet &packet = *packetStorage;
        if (xQueueReceive(incoming, &packet, pdMS_TO_TICKS(10)) != pdTRUE) continue;
        if (packet.epoch != epoch.load() || !foreground.load() || abortRequested.load()) continue;
        const int nextRate = serverRate.load();
        if (!decoder || decoderRate != nextRate || decoderEpoch != packet.epoch) {
            xSemaphoreTake(codecLock, portMAX_DELAY);
            if (decoder) opus_decoder_destroy(decoder);
            int error = 0; decoder = opus_decoder_create(nextRate, 1, &error);
            xSemaphoreGive(codecLock);
            decoderRate = nextRate; decoderEpoch = packet.epoch;
        }
        if (!decoder) { streamFault.store(true); continue; }
        if (!installed) {
            if (!installSpeaker(nextRate)) { streamFault.store(true); continue; }
            installed = true; rate = nextRate;
        } else if (rate != nextRate) {
            if (i2s_set_clk(I2S_NUM_1, nextRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO) != ESP_OK) {
                streamFault.store(true); continue;
            }
            rate = nextRate;
        }
        xSemaphoreTake(codecLock, portMAX_DELAY);
        const int samples = opus_decode(decoder, packet.bytes, packet.length, pcm.get(), 5760, 0);
        xSemaphoreGive(codecLock);
        if (samples < 0) { Serial.printf("[Xiaozhi] Opus decode failed: %d\n",samples); streamFault.store(true); continue; }
        const int volume = FactoryVolume::get();
        for (int i = 0; i < samples; ++i) stereo[2*i] = stereo[2*i+1] = int32_t(pcm[i]) * volume / 100;
        if (!foreground.load() || abortRequested.load()) continue;
        digitalWrite(MAX98357A_SD_MODE, volume ? HIGH : LOW);
        if (writeSpeaker(stereo.get(),samples)) {
            if (++playedFrames == 1) Serial.printf("[Xiaozhi] First reply audio written: rate=%d samples=%d volume=%d\n",nextRate,samples,volume);
        }
    }
}

void handleJson(const char *text,size_t length);
void mqttEvent(void *,esp_event_base_t,int32_t event,void *arg) {
    auto *data = static_cast<esp_mqtt_event_handle_t>(arg);
    if (event == MQTT_EVENT_CONNECTED) { connected.store(true); return; }
    if (event == MQTT_EVENT_DISCONNECTED || event == MQTT_EVENT_ERROR) {
        connected.store(false);
        if (event == MQTT_EVENT_ERROR) {
            streamFault.store(true);
            if (data->error_handle) Serial.printf("[Xiaozhi] MQTT error: TLS=%d broker=%d\n",
                data->error_handle->esp_tls_last_esp_err,data->error_handle->connect_return_code);
        }
        return;
    }
    if (event != MQTT_EVENT_DATA || !dialog.load() || data->total_data_len<0 || data->data_len<0 || data->current_data_offset<0) return;
    if (message.append(1,data->total_data_len,data->current_data_offset,data->data,data->data_len))
        handleJson(reinterpret_cast<char *>(message.bytes),message.used);
}
void websocketEvent(void *, esp_event_base_t, int32_t event, void *arg) {
    if (event == WEBSOCKET_EVENT_CONNECTED) { connected.store(true); return; }
    if (event == WEBSOCKET_EVENT_DISCONNECTED || event == WEBSOCKET_EVENT_CLOSED || event == WEBSOCKET_EVENT_ERROR) {
        connected.store(false); hello.store(false); return;
    }
    if (event != WEBSOCKET_EVENT_DATA || !dialog.load()) return;
    const auto *data = static_cast<esp_websocket_event_data_t *>(arg);
    if (data->op_code != 1 && data->op_code != 2) return;
    if (data->payload_len < 0 || data->payload_offset < 0 || data->data_len < 0) return;
    if (!message.append(data->op_code, data->payload_len, data->payload_offset, data->data_ptr, data->data_len)) return;
    if (message.opcode == 2) {
        const uint8_t *payload = nullptr; size_t length = 0;
        if (!hello.load() || !FactoryXiaozhiProtocol::audioPayload(message.bytes, message.used, protocolVersion, payload, length)) {
            streamFault.store(true); return;
        }
        Packet packet; packet.epoch = epoch.load(); packet.length = length; memcpy(packet.bytes, payload, length);
        speaking.store(true); capture.store(false);
        while (dialog.load() && foreground.load() && !abortRequested.load())
            if (xQueueSend(incoming, &packet, pdMS_TO_TICKS(20)) == pdTRUE) { ++receivedFrames; break; }
        return;
    }
    handleJson(reinterpret_cast<char *>(message.bytes),message.used);
}
void handleJson(const char *text,size_t length) {
    cJSON *root = cJSON_ParseWithLength(text,length+1);
    const char *type = field(root, "type");
    if (!strcmp(type, "hello") && !hello.load()) {
        const auto *params = cJSON_GetObjectItemCaseSensitive(root, "audio_params");
        const int rate = number(params, "sample_rate", 24000);
        const char *format = field(params, "format");
        if (strcmp(field(root,"transport"),useMqtt ? "udp" : "websocket") || (*format && strcmp(format,"opus")) ||
            (rate != 8000 && rate != 12000 && rate != 16000 && rate != 24000 && rate != 48000) ||
            number(params,"channels",1) != 1 || !*field(root,"session_id")) streamFault.store(true);
        else {
            bool valid = true;
            if (useMqtt) {
                const auto *channel = cJSON_GetObjectItemCaseSensitive(root,"udp");
                const int port = number(channel,"port",0);
                const char *host = field(channel,"server");
                valid = *host && strlen(host)<sizeof(udpHost) && port>0 && port<65536 &&
                    FactoryXiaozhiProtocol::hex16(field(channel,"key"),udpKey) &&
                    FactoryXiaozhiProtocol::hex16(field(channel,"nonce"),udpNonce);
                if (valid) { copy(udpHost,host); udpPort = port; }
            }
            if (valid) { copy(sessionId, field(root,"session_id")); serverRate.store(rate); hello.store(true); }
            else streamFault.store(true);
        }
    } else if (!strcmp(type, "stt")) {
        Serial.printf("[Xiaozhi] STT received: %u bytes\n",unsigned(strlen(field(root,"text"))));
        portENTER_CRITICAL(&uiLock); copy(shared.heard, field(root,"text")); portEXIT_CRITICAL(&uiLock);
    } else if (!strcmp(type, "tts")) {
        const char *state = field(root,"state");
        if (!strcmp(state,"start") || !strcmp(state,"stop")) Serial.printf("[Xiaozhi] TTS %s\n",state);
        if (!strcmp(state,"start")) { capture.store(false); speaking.store(true); ttsStopped.store(false); status(Phase::Speaking,"小智正在回答"); }
        else if (!strcmp(state,"stop")) ttsStopped.store(true);
        else if (!strcmp(state,"sentence_start")) {
            portENTER_CRITICAL(&uiLock); copy(shared.reply, field(root,"text")); portEXIT_CRITICAL(&uiLock);
        }
    } else if (!strcmp(type,"goodbye")) abortRequested.store(true);
    else if (!strcmp(type,"error")) { status(Phase::Error,"服务返回错误，请稍后重试"); streamFault.store(true); }
    cJSON_Delete(root);
}

class JsonBody : public Stream {
public:
    String value;
    size_t write(const uint8_t *bytes, size_t count) override {
        if (count > 16384 - value.length()) return 0;
        return value.concat(reinterpret_cast<const char *>(bytes), count) ? count : 0;
    }
    size_t write(uint8_t byte) override { return write(&byte, 1); }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};
int post(const String &url, const String &body, String &reply) {
    WiFiClientSecure tls; tls.setCACert(FactoryXiaozhiRootCa); tls.setHandshakeTimeout(6);
    HTTPClient http; http.setConnectTimeout(4000); http.setTimeout(6000);
    if (!http.begin(tls, url)) return -1;
    http.addHeader("Activation-Version", "1"); http.addHeader("Device-Id", deviceId);
    http.addHeader("Client-Id", clientId); http.addHeader("Accept-Language", "zh-CN");
    http.addHeader("User-Agent", "lilygo-t-display-s3-pro-mvsrlora/1.0.1"); http.addHeader("Content-Type", "application/json");
    const int code = http.POST(body);
    JsonBody response;
    if (code > 0 && http.getSize() <= 16384 && http.writeToStream(&response) >= 0) reply = response.value;
    http.end(); return code;
}
String encoded(cJSON *root) {
    char *json = cJSON_PrintUnformatted(root);
    String result(json ? json : "{}"); cJSON_free(json); cJSON_Delete(root); return result;
}
bool validServer(const String &value) {
    return value.length() < 512 && (value.startsWith("wss://") || value.startsWith("ws://")) &&
        value.indexOf('\r') < 0 && value.indexOf('\n') < 0;
}
bool configureServer() {
    if (*FactoryXiaozhiServer) {
        useMqtt = false;
        serverUrl = FactoryXiaozhiServer; serverToken = FactoryXiaozhiToken; protocolVersion = 1;
        provisioned = validServer(serverUrl); serviceReady.store(provisioned); return provisioned;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root,"version",2); cJSON_AddStringToObject(root,"mac_address",deviceId.c_str());
    cJSON_AddStringToObject(root,"uuid",clientId.c_str()); cJSON_AddStringToObject(root,"language","zh-CN");
    cJSON_AddStringToObject(root,"chip_model_name","esp32s3");
    cJSON_AddNumberToObject(root,"flash_size",ESP.getFlashChipSize());
    cJSON_AddNumberToObject(root,"minimum_free_heap_size",ESP.getMinFreeHeap());
    cJSON *app = cJSON_AddObjectToObject(root,"application");
    cJSON_AddStringToObject(app,"name","Factory"); cJSON_AddStringToObject(app,"version","1.0.1");
    cJSON_AddStringToObject(app,"idf_version",esp_get_idf_version());
    cJSON *board = cJSON_AddObjectToObject(root,"board");
    cJSON_AddStringToObject(board,"type","lilygo-t-display-s3-pro-mvsrlora");
    cJSON_AddStringToObject(board,"name","lilygo-t-display-s3-pro-mvsrlora");
    cJSON_AddStringToObject(board,"manufacturer","lilygo");
    cJSON *protocol = cJSON_AddObjectToObject(root,"protocol");
    cJSON *features = cJSON_AddObjectToObject(protocol,"features");
    cJSON_AddBoolToObject(features,"mcp",false); cJSON_AddBoolToObject(features,"aec",false);
    cJSON_AddBoolToObject(features,"udp_probe",false);
    String response;
    const int codeStatus = post(FactoryXiaozhiOta, encoded(root), response);
    if (codeStatus != 200) {
        Serial.printf("[Xiaozhi] Provisioning HTTP status: %d\n", codeStatus);
        char detail[128]; snprintf(detail,sizeof(detail),"官方配置请求失败（HTTP %d），点击重试",codeStatus);
        status(Phase::Error,detail); return false;
    }
    root = cJSON_Parse(response.c_str());
    if (!root) { status(Phase::Error,"服务配置解析失败"); return false; }
    const auto *serverTime = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root,"server_time"),"timestamp");
    if (cJSON_IsNumber(serverTime) && serverTime->valuedouble > 1700000000000.0) {
        timeval tv = {}; tv.tv_sec = time_t(serverTime->valuedouble/1000.0); settimeofday(&tv,nullptr);
    }
    const auto *activation = cJSON_GetObjectItemCaseSensitive(root,"activation");
    const char *code = field(activation,"code");
    if (*code) {
        portENTER_CRITICAL(&uiLock); copy(shared.activation, code); portEXIT_CRITICAL(&uiLock);
        provisioned = false; serviceReady.store(false);
        Serial.printf("[Xiaozhi] Official activation code received (%u digits)\n",unsigned(strlen(code)));
        status(Phase::Binding,"请在 xiaozhi.me 控制台添加设备"); cJSON_Delete(root); return false;
    }
    const auto *mq = cJSON_GetObjectItemCaseSensitive(root,"mqtt");
    if (cJSON_IsObject(mq) && *field(mq,"endpoint")) {
        mqttEndpoint = field(mq,"endpoint"); mqttId = field(mq,"client_id");
        mqttUsername = field(mq,"username"); mqttPassword = field(mq,"password"); mqttTopic = field(mq,"publish_topic");
        mqttKeepalive = constrain(number(mq,"keepalive",240),30,1200);
        if (mqttEndpoint.length()>255 || mqttEndpoint.indexOf('/')>=0 || mqttEndpoint.indexOf('\n')>=0 ||
            !mqttId.length() || mqttId.length()>512 || mqttUsername.length()>2048 || mqttPassword.length()>2048 ||
            !mqttTopic.length() || mqttTopic.length()>512) {
            status(Phase::Error,"官方 MQTT 配置格式错误"); cJSON_Delete(root); return false;
        }
        useMqtt = true; provisioned = true; serviceReady.store(true);
        portENTER_CRITICAL(&uiLock); shared.activation[0] = 0; portEXIT_CRITICAL(&uiLock);
        Serial.println("[Xiaozhi] Official MQTT/UDP configuration ready");
        cJSON_Delete(root); return true;
    }
    const auto *ws = cJSON_GetObjectItemCaseSensitive(root,"websocket");
    const String url(field(ws,"url")), token(field(ws,"token"));
    const int version = number(ws,"version",1);
    if (!validServer(url) || version < 1 || version > 3 || token.length() > 2048 || token.indexOf('\r') >= 0 || token.indexOf('\n') >= 0) {
        status(Phase::Error,"服务未提供可用的 WebSocket 配置"); cJSON_Delete(root); return false;
    }
    serverUrl = url; serverToken = token; protocolVersion = version; provisioned = true;
    useMqtt = false; serviceReady.store(true);
    settings.putString("url", serverUrl); settings.putString("token", serverToken); settings.putInt("proto", version);
    settings.putBool("bound",true);
    portENTER_CRITICAL(&uiLock); shared.activation[0] = 0; portEXIT_CRITICAL(&uiLock);
    cJSON_Delete(root); return true;
}
bool sendText(const String &text) {
    if (useMqtt) return mqtt && connected.load() &&
        esp_mqtt_client_publish(mqtt,mqttTopic.c_str(),text.c_str(),text.length(),0,0)>=0;
    return websocket && connected.load() &&
        esp_websocket_client_send_text(websocket,text.c_str(),text.length(),pdMS_TO_TICKS(1000)) == int(text.length());
}
bool listenMessage(const char *state) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root,"session_id",sessionId); cJSON_AddStringToObject(root,"type","listen");
    cJSON_AddStringToObject(root,"state",state);
    if (!strcmp(state,"start")) cJSON_AddStringToObject(root,"mode","manual");
    const bool sent = sendText(encoded(root));
    Serial.printf("[Xiaozhi] listen %s: %s\n",state,sent ? "sent" : "failed");
    return sent;
}
void finishConversation() {
    if (useMqtt && mqtt && connected.load() && hello.load()) {
        cJSON *bye = cJSON_CreateObject();
        cJSON_AddStringToObject(bye,"type","goodbye"); cJSON_AddStringToObject(bye,"session_id",sessionId);
        sendText(encoded(bye));
    }
    capture.store(false); dialog.store(false); speaking.store(false);
    Serial.printf("[Xiaozhi] Audio frames: uploaded=%u received=%u played=%u input_fault=%d\n",
        unsigned(uploadedFrames.load()),unsigned(receivedFrames.load()),unsigned(playedFrames.load()),inputFault.load());
    // Let the speaker release I2S1 before another application can use it.
    if (websocket) {
        esp_websocket_client_stop(websocket); esp_websocket_client_destroy(websocket); websocket = nullptr;
    }
    if (mqtt) { esp_mqtt_client_stop(mqtt); esp_mqtt_client_destroy(mqtt); mqtt = nullptr; }
    udp.stop(); mbedtls_aes_free(&aes);
    connected.store(false); hello.store(false);
    xQueueReset(outgoing);
}
bool sendAudio(const Packet &packet,uint8_t *buffer,size_t capacity) {
    if (!useMqtt) {
        const size_t n = FactoryXiaozhiProtocol::wrap(buffer,capacity,packet.bytes,packet.length,protocolVersion);
        return n && esp_websocket_client_send_bin(websocket,reinterpret_cast<char *>(buffer),n,pdMS_TO_TICKS(1000)) == int(n);
    }
    if (packet.length+16u>capacity || sendSequence == UINT32_MAX) return false;
    memcpy(buffer,udpNonce,16); buffer[2]=packet.length>>8; buffer[3]=packet.length;
    FactoryXiaozhiProtocol::write32(buffer+8,0);
    FactoryXiaozhiProtocol::write32(buffer+12,++sendSequence);
    uint8_t nonce[16], block[16] = {}; memcpy(nonce,buffer,16); size_t offset=0;
    if (mbedtls_aes_crypt_ctr(&aes,packet.length,&offset,nonce,block,packet.bytes,buffer+16)) return false;
    return udp.beginPacket(udpPeer,udpPort) && udp.write(buffer,packet.length+16)==packet.length+16u && udp.endPacket();
}
void receiveAudio() {
    if (!useMqtt) return;
    auto &bytes = networkAudio->receive;
    // Bound the work so touch/stop and microphone uplink keep making progress.
    for (int i=0;i<4;++i) {
        const int size=udp.parsePacket(); if (!size) break;
        if (size>int(sizeof(bytes)) || udp.remoteIP()!=udpPeer || udp.remotePort()!=udpPort) { udp.flush(); continue; }
        const int n=udp.read(bytes,sizeof(bytes)); size_t length=0; uint32_t sequence=0;
        if (n!=size || !FactoryXiaozhiProtocol::udpPayload(bytes,n,receiveSequence,length,sequence)) continue;
        auto &packet = networkAudio->incoming; packet.epoch=epoch.load(); packet.length=length;
        uint8_t nonce[16], block[16] = {}; memcpy(nonce,bytes,16); size_t offset=0;
        if (mbedtls_aes_crypt_ctr(&aes,length,&offset,nonce,block,bytes+16,packet.bytes)) { streamFault.store(true); break; }
        receiveSequence=sequence;
        if (xQueueSend(incoming,&packet,pdMS_TO_TICKS(10))!=pdTRUE) { streamFault.store(true); break; }
        ++receivedFrames;
        speaking.store(true); capture.store(false);
    }
}
bool connectConversation() {
    dialog.store(true); capture.store(false); speaking.store(false); streamFault.store(false); ttsStopped.store(false);
    connected.store(false); hello.store(false); message.used = message.expected = 0;
    xQueueReset(outgoing); xQueueReset(incoming);
    uploadedFrames.store(0); receivedFrames.store(0); playedFrames.store(0);
    ++epoch;
    String headers = "Device-Id: " + deviceId + "\r\nClient-Id: " + clientId + "\r\nProtocol-Version: " + String(protocolVersion) + "\r\n";
    if (serverToken.length()) headers += "Authorization: " + (serverToken.indexOf(' ') < 0 ? "Bearer " + serverToken : serverToken) + "\r\n";
    if (useMqtt) {
        const String uri = "mqtts://"+mqttEndpoint;
        esp_mqtt_client_config_t cfg = {};
        cfg.uri=uri.c_str(); cfg.client_id=mqttId.c_str(); cfg.username=mqttUsername.c_str(); cfg.password=mqttPassword.c_str();
        cfg.cert_pem=FactoryXiaozhiRootCa; cfg.keepalive=mqttKeepalive; cfg.disable_auto_reconnect=true;
        cfg.task_stack=6144; cfg.buffer_size=2048;
        mqtt=esp_mqtt_client_init(&cfg);
        if (!mqtt) return false;
        esp_mqtt_client_register_event(mqtt,MQTT_EVENT_ANY,mqttEvent,nullptr);
        if (esp_mqtt_client_start(mqtt)!=ESP_OK) return false;
    } else {
    esp_websocket_client_config_t cfg = {};
    cfg.uri = serverUrl.c_str(); cfg.headers = headers.c_str(); cfg.disable_auto_reconnect = true;
    cfg.cert_pem = FactoryXiaozhiRootCa;
    cfg.task_stack = 8192; cfg.buffer_size = 4096; cfg.ping_interval_sec = 15; cfg.pingpong_timeout_sec = 15;
    websocket = esp_websocket_client_init(&cfg);
    if (!websocket) return false;
    esp_websocket_register_events(websocket, WEBSOCKET_EVENT_ANY, websocketEvent, nullptr);
    if (esp_websocket_client_start(websocket) != ESP_OK) return false;
    }
    const uint32_t started = millis();
    while (!connected.load() && !streamFault.load() && !abortRequested.load() && foreground.load() && millis() - started < 12000) vTaskDelay(pdMS_TO_TICKS(20));
    if (!connected.load() || abortRequested.load() || !foreground.load()) return false;
    const String greeting = "{\"type\":\"hello\",\"version\":" + String(useMqtt ? 3 : protocolVersion) +
        ",\"transport\":\"" + (useMqtt ? "udp" : "websocket") + "\",\"features\":{\"mcp\":false,\"aec\":false,\"udp_probe\":false},"
        "\"audio_params\":{\"format\":\"opus\",\"sample_rate\":16000,\"channels\":1,\"frame_duration\":60}}";
    if (!sendText(greeting)) return false;
    const uint32_t helloStarted = millis();
    while (!hello.load() && connected.load() && !abortRequested.load() && !streamFault.load() && foreground.load() && millis() - helloStarted < 10000)
        vTaskDelay(pdMS_TO_TICKS(20));
    if (!hello.load() || abortRequested.load() || streamFault.load() || !foreground.load()) return false;
    if (useMqtt) {
        if (!WiFi.hostByName(udpHost,udpPeer) || !udp.begin(uint16_t(10000+esp_random()%50000))) return false;
        mbedtls_aes_init(&aes);
        if (mbedtls_aes_setkey_enc(&aes,udpKey,128)) return false;
        sendSequence=receiveSequence=0;
    }
    return true;
}
void managerTask(void *) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    settings.begin("factory-xz",false);
    deviceId = WiFi.macAddress(); deviceId.toLowerCase();
    clientId = settings.getString("uuid","");
    if (clientId.length() != 36) {
        uint8_t bytes[16]; esp_fill_random(bytes,sizeof(bytes)); bytes[6] = (bytes[6]&15)|64; bytes[8] = (bytes[8]&63)|128;
        char uuid[37]; snprintf(uuid,sizeof(uuid),"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7],bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
        clientId = uuid; settings.putString("uuid",clientId);
    }
    // Connection credentials are refreshed from the official configuration
    // below. MQTT devices need not have the old WebSocket url/token keys.
    provisioned = false; // Recheck official binding/token state on every boot.
    uint32_t nextCheck = 0;
    for (;;) {
        managerReleased.store(releaseRequest.load());
        if (retryConfiguration.exchange(false)) { provisioned = false; serviceReady.store(false); nextCheck = 0; }
        if (WiFi.status() != WL_CONNECTED) {
            status(Phase::Offline,"请连接 Wi-Fi，离线仍可本地唤醒");
            talkRequested.store(false); vTaskDelay(pdMS_TO_TICKS(300)); continue;
        }
        if (time(nullptr) < 1700000000) {
            // Bootstrap certificate date validation using the build time, then
            // replace it with NTP/verified official server time. Never disable TLS.
            tm built = {}; strptime(__DATE__ " " __TIME__,"%b %d %Y %H:%M:%S",&built);
            timeval tv = {}; tv.tv_sec = mktime(&built); settimeofday(&tv,nullptr);
            configTime(0,0,"ntp.aliyun.com","ntp.tencent.com","pool.ntp.org");
        }
        if (!provisioned && int32_t(millis() - nextCheck) >= 0) {
            bool binding;
            portENTER_CRITICAL(&uiLock); binding = shared.activation[0] != 0; portEXIT_CRITICAL(&uiLock);
            if (binding) {
                String response;
                const int result = post(String(FactoryXiaozhiOta) + "activate", "{}", response);
                if (result == 202) {
                    status(Phase::Binding,"等待绑定：在 xiaozhi.me 添加设备"); nextCheck = millis()+3000;
                    vTaskDelay(pdMS_TO_TICKS(30)); continue;
                }
                if (result != 200) {
                    char detail[128]; snprintf(detail,sizeof(detail),"检查绑定失败（HTTP %d），稍后重试",result);
                    status(Phase::Error,detail); nextCheck = millis()+10000;
                    vTaskDelay(pdMS_TO_TICKS(30)); continue;
                }
            }
            configureServer(); nextCheck = millis() + 15000;
            if (provisioned) status(Phase::Idle,"官方服务已就绪，说“你好小智”开始");
        }
        if (provisioned && micReady.load()) {
            portENTER_CRITICAL(&uiLock);
            if (shared.phase == Phase::Starting || shared.phase == Phase::Offline)
                { shared.phase = Phase::Idle; copy(shared.detail,"说“你好小智”或点击开始对话"); }
            portEXIT_CRITICAL(&uiLock);
        }
        if (!talkRequested.exchange(false)) { vTaskDelay(pdMS_TO_TICKS(30)); continue; }
        if (abortRequested.load()) continue;
        if (!foreground.load() || !micReady.load()) { status(Phase::Error,"麦克风尚未就绪，请稍后重试"); continue; }
        if (!provisioned) { nextCheck = 0; continue; }
        if (!networkAudio) { status(Phase::Error,"语音缓冲区未就绪，请重试"); continue; }
        auto &packet = networkAudio->outgoing;
        auto &wrapped = networkAudio->transmit;
        status(Phase::Connecting,"正在连接小智，请稍候");
        if (!connectConversation()) {
            finishConversation();
            if (!abortRequested.load()) { status(Phase::Error,"对话连接失败，可点击重试"); provisioned = false; serviceReady.store(false); nextCheck = millis()+5000; }
            continue;
        }
        if (!listenMessage("start")) { finishConversation(); status(Phase::Error,"无法开始对话"); continue; }
        captureDone.store(false); capture.store(true); status(Phase::Listening,"我在听，请说话…");
        uint32_t activity = millis(), drainStarted = 0, lastAudioDiagnostic = millis();
        const uint32_t dialogStarted = millis();
        bool stopSent = false;
        while (foreground.load() && armed.load() && !abortRequested.load() && connected.load() && !streamFault.load() && WiFi.status()==WL_CONNECTED) {
            receiveAudio();
            if (xQueueReceive(outgoing,&packet,pdMS_TO_TICKS(10)) == pdTRUE && packet.epoch == epoch.load()) {
                if (!sendAudio(packet,wrapped,sizeof(wrapped))) { streamFault.store(true); break; }
                if (++uploadedFrames == 1) Serial.printf("[Xiaozhi] First microphone packet sent: %u bytes via %s\n",
                    unsigned(packet.length),useMqtt ? "UDP" : "WebSocket");
                activity = millis();
            }
            if (millis()-lastAudioDiagnostic >= 5000) {
                lastAudioDiagnostic = millis();
                Serial.printf("[Xiaozhi] Audio frames: uploaded=%u received=%u played=%u\n",
                    unsigned(uploadedFrames.load()),unsigned(receivedFrames.load()),unsigned(playedFrames.load()));
            }
            if (captureDone.load() && !stopSent && uxQueueMessagesWaiting(outgoing)==0) {
                if (!listenMessage("stop")) break;
                stopSent = true; status(Phase::Thinking,"小智正在思考…"); activity = millis();
            }
            if (speaking.load()) activity = millis();
            if (ttsStopped.load() && uxQueueMessagesWaiting(incoming)==0) {
                if (!drainStarted) drainStarted = millis();
                // Wait beyond the longest supported packet and DMA queue.
                if (millis()-drainStarted > 500) break;
            } else drainStarted = 0;
            if (millis()-activity > 45000 || millis()-dialogStarted > 120000) { streamFault.store(true); break; }
        }
        const bool failed = streamFault.load() || (!connected.load() && !ttsStopped.load() && !abortRequested.load());
        finishConversation();
        if (inputFault.load()) status(Phase::Error,"麦克风无有效信号，请检查扩展板连接");
        else if (failed) status(Phase::Error,"语音连接中断，请重新唤醒");
        else status(Phase::Idle,"说“你好小智”开始下一次对话");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

bool startService() {
    // No TLS/model/task allocations if the board's external RAM did not start.
    // Microphone-only failures can still leave activation available below.
    if (!psramFound() || heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) {
        audioError("PSRAM 不可用，请 Clean 后重新编译");
        status(Phase::Error,"启动已停止：请检查 OPI PSRAM 配置");
        return false;
    }
    if (!initialized) {
        TaskHandle_t manager = nullptr;
        if (xTaskCreatePinnedToCore(managerTask,"xz-network",16384,nullptr,1,&manager,0) != pdPASS) {
            status(Phase::Error,"联网任务内存不足，点击重试连接"); return false;
        }
        initialized = true;
        status(Phase::Starting,"正在连接 xiaozhi.me 获取设备配置");
        xTaskNotifyGive(manager);
    }
    if (audioStarted.load()) return true;
    audioError("");
    outgoingStorage = static_cast<uint8_t *>(heap_caps_malloc(12 * sizeof(Packet), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    incomingStorage = static_cast<uint8_t *>(heap_caps_malloc(16 * sizeof(Packet), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (outgoingStorage) outgoing = xQueueCreateStatic(12, sizeof(Packet), outgoingStorage, &outgoingControl);
    if (incomingStorage) incoming = xQueueCreateStatic(16, sizeof(Packet), incomingStorage, &incomingControl);
    codecLock = xSemaphoreCreateMutex();
    scratch_ptr = static_cast<char *>(heap_caps_malloc(120000, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    global_stack = scratch_ptr;
    networkAudio = static_cast<NetworkAudioBuffers *>(heap_caps_malloc(sizeof(NetworkAudioBuffers),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    TaskHandle_t mic = nullptr, speaker = nullptr;
    bool ok = outgoing && incoming && codecLock && scratch_ptr && networkAudio;
    if (ok) ok = xTaskCreatePinnedToCore(micTask,"xz-mic",MicStackBytes,nullptr,3,&mic,1) == pdPASS;
    if (ok) ok = xTaskCreatePinnedToCore(speakerTask,"xz-speaker",SpeakerStackBytes,nullptr,2,&speaker,0) == pdPASS;
    if (!ok) {
        // All workers are still waiting at their startup barrier.
        if (mic) vTaskDelete(mic);
        if (speaker) vTaskDelete(speaker);
        if (outgoing) vQueueDelete(outgoing);
        if (incoming) vQueueDelete(incoming);
        if (codecLock) vSemaphoreDelete(codecLock);
        outgoing = incoming = nullptr; codecLock = nullptr;
        free(outgoingStorage); free(incomingStorage); outgoingStorage = incomingStorage = nullptr;
        free(scratch_ptr); scratch_ptr = global_stack = nullptr;
        free(networkAudio); networkAudio = nullptr;
        audioError("语音任务内存不足，点击麦克风重试"); return true;
    }
    audioStarted.store(true);
    armed.store(true);
    xTaskNotifyGive(mic); xTaskNotifyGive(speaker);
    return true;
}

void releaseConversation() {
    if (!initialized) return;
    cueRequested.store(false);
    foreground.store(false); talkRequested.store(false); capture.store(false); abortRequested.store(true);
    const uint32_t request = ++releaseRequest;
    // An acknowledgement issued *after* the request prevents a stale idle flag
    // from allowing a different app to claim I2S during speaker startup.
    while ((audioStarted.load() && speakerReleased.load() != request) || managerReleased.load() != request) delay(5);
}

constexpr uint16_t Ink = 0x1928, Muted = 0x7C11, Blue = 0x349F, Pale = 0xF7BF;
void ascii(Arduino_GFX &c, const char *value, int x, int y, int size, uint16_t color) {
    c.setFont(nullptr); c.setTextSize(size); c.setTextColor(color); c.setCursor(x,y); c.print(value);
}
void wrappedReply(FactorySurface &c, const char *text) {
    char line[160];
    const char *at = text;
    for (int row = 0; row < 2 && *at; ++row) {
        size_t used = 0; int width = 0;
        while (*at && *at != '\n' && used < sizeof(line)-5) {
            const uint8_t b = uint8_t(*at);
            const int bytes = b < 128 ? 1 : b < 224 ? 2 : b < 240 ? 3 : 4;
            const int advance = b < 128 ? 9 : 16;
            if (width + advance > 290) break;
            int n = 0; while (n < bytes && at[n]) ++n;
            memcpy(line+used,at,n); used += n; at += n; width += advance;
        }
        if (*at == '\n') ++at;
        if (row == 1 && *at && used < sizeof(line)-4) { memcpy(line+used,"...",3); used += 3; }
        line[used] = 0;
        factoryDrawText(c,line,142,117+row*19,316,Ink,Pale);
    }
}
void paint() {
    auto &c = factorySurface();
    c.fillScreen(WHITE); c.setTextWrap(false);
    ascii(c,"XIAOZHI",16,14,2,Ink);
    c.fillCircle(120,21,3,shownArmed && shownWake ? 0x36B3 : Muted);
    const char *heading = painted.audioError[0] ? "音频异常" : shownInputFault ? "收音异常" : !initialized ? "正在启动" :
        audioStarted.load() && !micReady.load() ? "麦克风启动中" : shownArmed ? "语音助手" : "麦克风已关闭";
    factoryDrawText(c,heading,134,13,230,Muted,WHITE);
    c.fillRoundRect(384,6,82,32,12,Pale);
    ascii(c,"Wi-Fi",405,17,1,WiFi.status() == WL_CONNECTED ? Blue : Muted);
    c.drawFastHLine(16,45,448,0xEF7D);
    c.fillCircle(64,99,39,0xE73F); drawIcon(c,64,99);
    factoryDrawText(c,"你好小智",28,147,94,Ink,WHITE);
    if (painted.activation[0]) {
        factoryDrawText(c,"在 xiaozhi.me 添加设备",136,62,326,Ink,WHITE);
        ascii(c,painted.activation,142,90,3,Blue);
        factoryDrawText(c,"输入激活码，绑定后自动连接",136,127,330,Muted,WHITE);
    } else {
        c.fillRoundRect(128,57,338,45,12,Pale);
        factoryDrawText(c,painted.audioError[0] ? painted.audioError : shownInputFault ? "麦克风无有效信号，请检查连接" :
            painted.heard[0] ? painted.heard : "点击开始对话，或说唤醒词",
            142,71,316,painted.audioError[0] || shownInputFault ? 0xC926 : Ink,Pale);
        c.fillRoundRect(128,108,338,51,12,Pale);
        if (painted.reply[0]) wrappedReply(c,painted.reply);
        else factoryDrawText(c,"我在这里，随时为你解答",142,125,316,Muted,Pale);
    }
    const char *detail = FactoryXiaozhiState::detail(painted.detail,
        painted.audioError[0] ? painted.audioError : shownInputFault ? "麦克风无有效信号，请检查扩展板连接" : "",!shownArmed,
        painted.phase == Phase::Binding,painted.phase == Phase::Error || painted.phase == Phase::Offline);
    factoryDrawText(c,detail,16,165,450,Muted,WHITE);
    c.fillRoundRect(14,188,140,29,11,Pale);
    factoryDrawText(c,painted.audioError[0] ? "重试麦克风" : shownArmed ? "麦克风：开" : "麦克风：关",30,194,124,Ink,Pale);
    c.fillRoundRect(164,188,302,29,11,Blue);
    const auto action = FactoryXiaozhiState::action(WiFi.status()==WL_CONNECTED,initialized,
        serviceReady.load(),micReady.load(),shownBusy);
    using Action = FactoryXiaozhiState::Action;
    const char *button = action==Action::Stop ? "结束对话" : action==Action::ConnectWifi ? "连接 Wi-Fi" :
        action==Action::RetryService ? "重试连接" : action==Action::CheckActivation ?
        (painted.activation[0] ? "检查绑定" : "获取激活码 / 重试连接") :
        action==Action::WaitMicrophone ? "重试麦克风" : "开始对话";
    factoryDrawText(c,button,204,194,250,WHITE,Blue);
}
void render() {
    if (!screen || wifiPage) return;
    portENTER_CRITICAL(&uiLock); painted = shared; portEXIT_CRITICAL(&uiLock);
    shownArmed = armed.load(); shownWake = wakeReady.load(); shownBusy = dialog.load() || talkRequested.load();
    shownInputFault = inputFault.load() && shownArmed;
    factorySurface().present(*screen,paint,force); force = false; lastRender = millis();
}
void backFromWifi() {
    FactoryNetwork::close(); wifiPage = false;
    if (screen) { screen->setRotation(1); foreground.store(true); force = true; render(); }
}
} // namespace

void drawIcon(Arduino_GFX &c, int16_t x, int16_t y) {
    c.fillCircle(x,y+2,27,0xE73F);
    c.fillRoundRect(x-22,y-16,44,35,12,0x4C9F);
    c.fillRoundRect(x-18,y-12,36,26,9,0x18E7);
    c.fillRoundRect(x-11,y-5,5,10,2,0x97FF);
    c.fillRoundRect(x+6,y-5,5,10,2,0x97FF);
    c.drawFastHLine(x-4,y+8,8,0x97FF);
    c.fillRoundRect(x-28,y-6,4,14,2,0x855F);
    c.fillRoundRect(x+24,y-6,4,14,2,0x855F);
    c.drawFastVLine(x,y-23,8,0x4C9F); c.fillCircle(x,y-23,3,0x855F);
}
void open(Arduino_GFX &display) {
    screen = &display; wifiPage = false; screen->setRotation(1); foreground.store(true);
    abortRequested.store(false);
    force = true; render(); startService(); force = true;
    if (audioStarted.load()) cueRequested.store(true);
}
void closeView() {
    if (wifiPage) FactoryNetwork::close();
    releaseConversation(); screen = nullptr; wifiPage = false; wakeRequested.store(false);
}
void talk() {
    if (!screen || wifiPage) return;
    using Action = FactoryXiaozhiState::Action;
    const auto action = FactoryXiaozhiState::action(WiFi.status()==WL_CONNECTED,initialized,
        serviceReady.load(),micReady.load(),dialog.load() || talkRequested.load());
    if (action==Action::ConnectWifi) {
        releaseConversation(); wifiPage = true; FactoryNetwork::openWifi(*screen,backFromWifi); return;
    }
    if (action==Action::Stop) return;
    if (action==Action::RetryService && !startService()) { force = true; return; }
    if (action==Action::CheckActivation || action==Action::RetryService) {
        retryConfiguration.store(true); status(Phase::Connecting,"正在向官方查询激活状态…"); force = true; return;
    }
    if (action==Action::WaitMicrophone) {
        startService(); status(Phase::Error,hardwareFailed.load() ? "麦克风初始化失败，请重启设备" :
            "麦克风未就绪，请查看下方麦克风状态"); force = true; return;
    }
    armed.store(true); foreground.store(true); abortRequested.store(false);
    portENTER_CRITICAL(&uiLock); shared.heard[0] = shared.reply[0] = 0; portEXIT_CRITICAL(&uiLock);
    talkRequested.store(true); force = true;
}
void touch(int16_t x, int16_t y) {
    if (wifiPage) { FactoryNetwork::touch(x,y); return; }
    if (x >= 384 && y < 43) {
        releaseConversation(); wifiPage = true; FactoryNetwork::openWifi(*screen,backFromWifi); return;
    }
    if (y < 188 || y >= 218) return;
    if (x >= 14 && x < 154) {
        if (armed.load() && micReady.load()) { releaseConversation(); armed.store(false); wakeRequested.store(false); }
        else { startService(); armed.store(audioStarted.load() && !hardwareFailed.load()); }
        foreground.store(true); force = true;
    } else if (x >= 164 && x < 466) {
        if (dialog.load() || talkRequested.load()) {
            releaseConversation(); foreground.store(true); status(Phase::Idle,"对话已结束，可重新唤醒"); force = true;
        } else talk();
    }
}
void tick() {
    if (!screen || wifiPage || millis()-lastRender < 160) return;
    ScreenState next;
    portENTER_CRITICAL(&uiLock); next = shared; portEXIT_CRITICAL(&uiLock);
    if (force || memcmp(&next,&painted,sizeof(next)) || shownArmed != armed.load() ||
        shownWake != wakeReady.load() || shownBusy != (dialog.load() || talkRequested.load()) ||
        shownInputFault != (inputFault.load() && armed.load())) render();
}
bool takeWakeRequest() {
    const bool detected = wakeRequested.exchange(false);
    return detected && armed.load() && !wifiPage;
}
void prepareSleep() {
    armed.store(false); wakeRequested.store(false); releaseConversation();
}
} // namespace FactoryXiaozhi
