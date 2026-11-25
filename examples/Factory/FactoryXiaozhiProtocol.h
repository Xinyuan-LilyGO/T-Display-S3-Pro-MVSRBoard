#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace FactoryXiaozhiProtocol {
constexpr size_t MaxPacket = 2048, MaxMessage = 8192;
inline bool hex16(const char *text, uint8_t *output) {
    if (!text || strlen(text)!=32) return false;
    for (size_t i=0;i<16;++i) {
        unsigned value=0;
        for (int j=0;j<2;++j) {
            const char c=text[2*i+j];
            const int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
            if (n<0) return false;
            value=(value<<4)|n;
        }
        output[i]=value;
    }
    return true;
}
inline uint32_t read32(const uint8_t *p) {
    return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
}
inline void write32(uint8_t *p, uint32_t n) { p[0]=n>>24; p[1]=n>>16; p[2]=n>>8; p[3]=n; }
inline bool udpPayload(const uint8_t *p,size_t size,uint32_t previous,size_t &length,uint32_t &sequence) {
    if (!p || size<17 || p[0]!=1) return false;
    length=(size_t(p[2])<<8)|p[3]; sequence=read32(p+12);
    return length && length<=MaxPacket && size==length+16 && sequence>previous;
}
inline bool audioPayload(const uint8_t *data, size_t size, int version, const uint8_t *&payload, size_t &length) {
    payload = nullptr; length = 0;
    if (!data) return false;
    size_t header = 0;
    if (version == 2) {
        if (size < 16 || data[0] != 0 || data[1] != 2 || data[2] || data[3]) return false;
        header = 16;
        length = (uint32_t(data[12]) << 24) | (uint32_t(data[13]) << 16) | (uint32_t(data[14]) << 8) | data[15];
    } else if (version == 3) {
        if (size < 4 || data[0]) return false;
        header = 4; length = (size_t(data[2]) << 8) | data[3];
    } else if (version == 1) length = size;
    else return false;
    if (!length || length > MaxPacket || length != size - header) return false;
    payload = data + header; return true;
}
inline size_t wrap(uint8_t *output, size_t capacity, const uint8_t *payload, size_t length, int version) {
    const size_t header = version == 2 ? 16 : version == 3 ? 4 : 0;
    if (!output || !payload || version < 1 || version > 3 || !length || length > MaxPacket || capacity < header + length) return 0;
    memset(output, 0, header);
    if (version == 2) {
        output[1] = 2; output[12] = length >> 24; output[13] = length >> 16;
        output[14] = length >> 8; output[15] = length;
    } else if (version == 3) { output[2] = length >> 8; output[3] = length; }
    memcpy(output + header, payload, length); return length + header;
}
// Bounded accumulator for a single WebSocket message delivered in chunks.
struct Message {
    uint8_t bytes[MaxMessage + 1] = {};
    size_t used = 0, expected = 0;
    int opcode = 0;
    bool append(int op, size_t total, size_t offset, const void *data, size_t length) {
        if (offset == 0) { used = 0; expected = total; opcode = op; }
        if ((op != 1 && op != 2) || op != opcode || total > MaxMessage || offset != used ||
            total != expected || used > total || length > total - used || (length && !data)) {
            used = expected = 0; return false;
        }
        if (length) memcpy(bytes + used, data, length);
        used += length; bytes[used] = 0;
        return used == expected && expected != 0;
    }
};
}
