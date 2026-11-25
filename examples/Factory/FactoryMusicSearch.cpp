#include "FactoryMusicSearch.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

namespace FactoryMusicSearch {
namespace {
// HTTPClient handles chunked transfer decoding; this sink caps RAM usage and
// stops accepting data when the user leaves or replaces the search.
class Body : public Stream {
public:
    static constexpr size_t Capacity = 48 * 1024;
    char *data = static_cast<char *>(heap_caps_malloc(Capacity + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    size_t length = 0;
    bool failed = false;
    bool (*cancelled)();
    explicit Body(bool (*check)()) : cancelled(check) {
        if (!data) data = static_cast<char *>(malloc(Capacity + 1));
    }
    ~Body() { free(data); }
    size_t write(const uint8_t *buffer, size_t size) override {
        if (!data || cancelled() || size > Capacity - length) { failed = true; return 0; }
        memcpy(data + length, buffer, size); length += size; data[length] = 0;
        return size;
    }
    size_t write(uint8_t value) override { return write(&value, 1); }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};
String encode(const char *query) {
    static const char hex[] = "0123456789ABCDEF";
    String value;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(query); *p; ++p) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.') value += char(*p);
        else { value += '%'; value += hex[*p >> 4]; value += hex[*p & 15]; }
    }
    return value;
}
}
void search(const char *query, Results &output, bool (*cancelled)()) {
    output.count = 0;
    if (cancelled()) { output.status = Status::Cancelled; return; }
    if (WiFi.status() != WL_CONNECTED) { output.status = Status::Offline; return; }
    WiFiClientSecure client;
    // Matches the existing audio library's public-media TLS behavior; no
    // account, password or stored Wi-Fi credentials are sent to this service.
    client.setInsecure();
    client.setHandshakeTimeout(6);
    HTTPClient http;
    http.setConnectTimeout(4000); http.setTimeout(5000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    const String url = "https://itunes.apple.com/search?entity=song&media=music&limit=8&country=US&term=" + encode(query);
    output.status = Status::NetworkError;
    if (!http.begin(client, url)) return;
    const int code = http.GET();
    if (code == 429) output.status = Status::RateLimited;
    else if (code == HTTP_CODE_OK) {
        Body body(cancelled);
        if (body.data && http.getSize() <= int(Body::Capacity) &&
            http.writeToStream(&body) >= 0 && !body.failed && body.length) {
            output.status = parse(body.data, body.length + 1, output) ? Status::Ok : Status::InvalidResponse;
        } else output.status = Status::InvalidResponse;
    }
    http.end();
    if (cancelled()) { output.count = 0; output.status = Status::Cancelled; }
}
}
