// Optional acceleration for offline resource discovery. No renderer integration.
#include <cstddef>
#include <cstdint>
#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif
static int Decode(const uint8_t* src, size_t size, uint8_t* dst, size_t capacity, size_t* consumed) {
    if (size < 16 || src[0] != 'Y' || src[1] != 'a' || src[2] != 'z' || src[3] != '0') return -1;
    const size_t expected = (size_t(src[4]) << 24) | (size_t(src[5]) << 16) |
                            (size_t(src[6]) << 8) | src[7];
    if (expected != capacity) return -2;
    size_t input = 16, output = 0;
    while (output < expected) {
        if (input >= size) return -3;
        uint8_t flags = src[input++];
        for (unsigned bit = 0; bit < 8 && output < expected; ++bit, flags <<= 1) {
            if (flags & 0x80) {
                if (input >= size) return -4;
                dst[output++] = src[input++];
            } else {
                if (input + 2 > size) return -5;
                unsigned first = src[input++], second = src[input++];
                size_t distance = ((first & 15) << 8) + second + 1;
                size_t length = first >> 4;
                if (length == 0) {
                    if (input >= size) return -6;
                    length = src[input++] + 18;
                } else length += 2;
                if (distance > output || length > expected - output) return -7;
                for (size_t i = 0; i < length; ++i) {
                    dst[output] = dst[output - distance];
                    ++output;
                }
            }
        }
    }
    if (consumed) *consumed = input;
    return 0;
}

EXPORT int nxshader_yaz0(const uint8_t* src, size_t size, uint8_t* dst, size_t capacity) {
    return Decode(src, size, dst, capacity, nullptr);
}

EXPORT int nxshader_yaz0_extent(const uint8_t* src, size_t size, uint8_t* dst,
                              size_t capacity, size_t* consumed) {
    return Decode(src, size, dst, capacity, consumed);
}
