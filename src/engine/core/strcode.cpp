#include "engine/core/binary_read.h"
#include "engine/core/strcode.h"

#include <cstring>
#include <string>
#include <utility>

namespace pt {

uint64_t CityHashMix16(uint64_t u, uint64_t v) {
    constexpr uint64_t kMul = 0x9DDFEA08EB382D69ull;
    uint64_t a = (u ^ v) * kMul;
    a ^= a >> 47;
    uint64_t b = (v ^ a) * kMul;
    b ^= b >> 47;
    return b * kMul;
}

namespace {

/* Google CityHash64's constants: Fox's StrCode is CityHash64 seeded with the first byte and the length, kept to 48 bits. */
constexpr uint64_t k0 = 0xC3A5C85C97CB3127ull;
constexpr uint64_t k1 = 0xB492B66FBE98F273ull;
constexpr uint64_t k2 = 0x9AE16A3B2F90404Full;
constexpr uint64_t k3 = 0xC949D7C7509E6557ull;

uint64_t Rotate(uint64_t v, int shift) {
    return shift == 0 ? v : ((v >> shift) | (v << (64 - shift)));
}

uint64_t RotateByAtLeast1(uint64_t v, int shift) {
    return (v >> shift) | (v << (64 - shift));
}

uint64_t ShiftMix(uint64_t v) {
    return v ^ (v >> 47);
}

uint64_t HashLen0to16(const uint8_t* s, size_t len) {
    if (len > 8) {
        const uint64_t a = ReadBinaryUnchecked<uint64_t>(s);
        const uint64_t b = ReadBinaryUnchecked<uint64_t>(s + len - 8);
        return CityHashMix16(a, RotateByAtLeast1(b + len, static_cast<int>(len))) ^ b;
    }
    if (len >= 4) {
        const uint64_t a = ReadBinaryUnchecked<uint32_t>(s);
        return CityHashMix16(len + (a << 3), ReadBinaryUnchecked<uint32_t>(s + len - 4));
    }
    if (len > 0) {
        const uint8_t a = s[0];
        const uint8_t b = s[len >> 1];
        const uint8_t c = s[len - 1];
        const uint32_t y = static_cast<uint32_t>(a) + (static_cast<uint32_t>(b) << 8);
        const uint32_t z = static_cast<uint32_t>(len) + (static_cast<uint32_t>(c) << 2);
        return ShiftMix(y * k2 ^ z * k3) * k2;
    }
    return k2;
}

uint64_t HashLen17to32(const uint8_t* s, size_t len) {
    const uint64_t a = ReadBinaryUnchecked<uint64_t>(s) * k1;
    const uint64_t b = ReadBinaryUnchecked<uint64_t>(s + 8);
    const uint64_t c = ReadBinaryUnchecked<uint64_t>(s + len - 8) * k2;
    const uint64_t d = ReadBinaryUnchecked<uint64_t>(s + len - 16) * k0;
    return CityHashMix16(Rotate(a - b, 43) + Rotate(c, 30) + d, a + Rotate(b ^ k3, 20) - c + len);
}

std::pair<uint64_t, uint64_t> WeakHashLen32WithSeeds(uint64_t w, uint64_t x, uint64_t y, uint64_t z, uint64_t a, uint64_t b) {
    a += w;
    b = Rotate(b + a + z, 21);
    const uint64_t c = a;
    a += x;
    a += y;
    b += Rotate(a, 44);
    return {a + z, b + c};
}

std::pair<uint64_t, uint64_t> WeakHashLen32WithSeeds(const uint8_t* s, uint64_t a, uint64_t b) {
    return WeakHashLen32WithSeeds(ReadBinaryUnchecked<uint64_t>(s), ReadBinaryUnchecked<uint64_t>(s + 8), ReadBinaryUnchecked<uint64_t>(s + 16), ReadBinaryUnchecked<uint64_t>(s + 24), a, b);
}

uint64_t HashLen33to64(const uint8_t* s, size_t len) {
    uint64_t z = ReadBinaryUnchecked<uint64_t>(s + 24);
    uint64_t a = ReadBinaryUnchecked<uint64_t>(s) + (len + ReadBinaryUnchecked<uint64_t>(s + len - 16)) * k0;
    uint64_t b = Rotate(a + z, 52);
    uint64_t c = Rotate(a, 37);
    a += ReadBinaryUnchecked<uint64_t>(s + 8);
    c += Rotate(a, 7);
    a += ReadBinaryUnchecked<uint64_t>(s + 16);
    const uint64_t vf = a + z;
    const uint64_t vs = b + Rotate(a, 31) + c;
    a = ReadBinaryUnchecked<uint64_t>(s + 16) + ReadBinaryUnchecked<uint64_t>(s + len - 32);
    z = ReadBinaryUnchecked<uint64_t>(s + len - 8);
    b = Rotate(a + z, 52);
    c = Rotate(a, 37);
    a += ReadBinaryUnchecked<uint64_t>(s + len - 24);
    c += Rotate(a, 7);
    a += ReadBinaryUnchecked<uint64_t>(s + len - 16);
    const uint64_t wf = a + z;
    const uint64_t ws = b + Rotate(a, 31) + c;
    const uint64_t r = ShiftMix((vf + ws) * k2 + (wf + vs) * k0);
    return ShiftMix(r * k0 + vs) * k2;
}

}

uint64_t CityHash64(const void* data, size_t len) {
    const uint8_t* s = static_cast<const uint8_t*>(data);
    if (len <= 32) {
        return len <= 16 ? HashLen0to16(s, len) : HashLen17to32(s, len);
    }
    if (len <= 64) {
        return HashLen33to64(s, len);
    }
    uint64_t x = ReadBinaryUnchecked<uint64_t>(s + len - 40);
    uint64_t y = ReadBinaryUnchecked<uint64_t>(s + len - 16) + ReadBinaryUnchecked<uint64_t>(s + len - 56);
    uint64_t z = CityHashMix16(ReadBinaryUnchecked<uint64_t>(s + len - 48) + len, ReadBinaryUnchecked<uint64_t>(s + len - 24));
    auto v = WeakHashLen32WithSeeds(s + len - 64, len, z);
    auto w = WeakHashLen32WithSeeds(s + len - 32, y + k1, x);
    x = x * k1 + ReadBinaryUnchecked<uint64_t>(s);
    len = (len - 1) & ~static_cast<size_t>(63);
    do {
        x = Rotate(x + y + v.first + ReadBinaryUnchecked<uint64_t>(s + 8), 37) * k1;
        y = Rotate(y + v.second + ReadBinaryUnchecked<uint64_t>(s + 48), 42) * k1;
        x ^= w.second;
        y += v.first + ReadBinaryUnchecked<uint64_t>(s + 40);
        z = Rotate(z + w.first, 33) * k1;
        v = WeakHashLen32WithSeeds(s, v.second * k1, x + w.first);
        w = WeakHashLen32WithSeeds(s + 32, z + w.second, y + ReadBinaryUnchecked<uint64_t>(s + 16));
        std::swap(z, x);
        s += 64;
        len -= 64;
    } while (len != 0);
    return CityHashMix16(CityHashMix16(v.first, w.first) + ShiftMix(y) * k1 + z, CityHashMix16(v.second, w.second) + x);
}

uint64_t CityHash64WithSeeds(const void* data, size_t length, uint64_t seed0, uint64_t seed1) {
    return CityHashMix16(CityHash64(data, length) - seed0, seed1);
}

uint64_t StrCode64(std::string_view text) {
    std::string buffer(text);
    buffer.push_back('\0');
    const uint64_t seed1 = text.empty() ? 0 : (static_cast<uint64_t>(static_cast<uint8_t>(text[0])) << 16) + text.size();
    return CityHash64WithSeeds(buffer.data(), buffer.size(), k2, seed1) & kStrCode64Mask;
}

}
