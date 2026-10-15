#include "engine/platform/ed25519.h"

#include <cctype>
#include <cstring>
#include <vector>

namespace pt::ed25519 {
namespace {

using gf = int64_t[16];

const gf gf0 = {0};
const gf gf1 = {1};
const gf D = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070, 0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
const gf D2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0, 0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
const gf X = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c, 0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
const gf Y = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
const gf I = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43, 0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};
const int64_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};

int Verify32(const uint8_t* x, const uint8_t* y) {
    uint32_t d = 0;
    for (int i = 0; i < 32; ++i) d |= uint32_t(x[i] ^ y[i]);
    return (1 & ((d - 1) >> 8)) - 1;
}
void Set(gf r, const gf a) { for (int i = 0; i < 16; ++i) r[i] = a[i]; }
void Carry(gf o) {
    for (int i = 0; i < 16; ++i) {
        o[i] += (int64_t(1) << 16);
        const int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= uint64_t(c) << 16;
    }
}
void Select(gf p, gf q, int b) {
    const int64_t c = ~(int64_t(b) - 1);
    for (int i = 0; i < 16; ++i) {
        const int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}
void Pack25519(uint8_t* o, const gf n) {
    gf m, t;
    for (int i = 0; i < 16; ++i) t[i] = n[i];
    Carry(t);
    Carry(t);
    Carry(t);
    for (int j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const int b = int((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        Select(t, m, 1 - b);
    }
    for (int i = 0; i < 16; ++i) {
        o[2 * i] = uint8_t(t[i] & 0xff);
        o[2 * i + 1] = uint8_t(t[i] >> 8);
    }
}
int Neq(const gf a, const gf b) {
    uint8_t c[32], d[32];
    Pack25519(c, a);
    Pack25519(d, b);
    return Verify32(c, d);
}
uint8_t Parity(const gf a) {
    uint8_t d[32];
    Pack25519(d, a);
    return d[0] & 1;
}
void Unpack25519(gf o, const uint8_t* n) {
    for (int i = 0; i < 16; ++i) o[i] = n[2 * i] + (int64_t(n[2 * i + 1]) << 8);
    o[15] &= 0x7fff;
}
void Add(gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i]; }
void Sub(gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i]; }
void Mul(gf o, const gf a, const gf b) {
    int64_t t[31] = {};
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; ++i) o[i] = t[i];
    Carry(o);
    Carry(o);
}
void Square(gf o, const gf a) { Mul(o, a, a); }
void Inverse(gf o, const gf i) {
    gf c;
    Set(c, i);
    for (int a = 253; a >= 0; --a) {
        Square(c, c);
        if (a != 2 && a != 4) Mul(c, c, i);
    }
    Set(o, c);
}
void Pow2523(gf o, const gf i) {
    gf c;
    Set(c, i);
    for (int a = 250; a >= 0; --a) {
        Square(c, c);
        if (a != 1) Mul(c, c, i);
    }
    Set(o, c);
}

void PointAdd(gf p[4], gf q[4]) {
    gf a, b, c, d, t, e, f, g, h;
    Sub(a, p[1], p[0]);
    Sub(t, q[1], q[0]);
    Mul(a, a, t);
    Add(b, p[0], p[1]);
    Add(t, q[0], q[1]);
    Mul(b, b, t);
    Mul(c, p[3], q[3]);
    Mul(c, c, D2);
    Mul(d, p[2], q[2]);
    Add(d, d, d);
    Sub(e, b, a);
    Sub(f, d, c);
    Add(g, d, c);
    Add(h, b, a);
    Mul(p[0], e, f);
    Mul(p[1], h, g);
    Mul(p[2], g, f);
    Mul(p[3], e, h);
}
void PointSwap(gf p[4], gf q[4], uint8_t b) { for (int i = 0; i < 4; ++i) Select(p[i], q[i], b); }
void PointPack(uint8_t* r, gf p[4]) {
    gf tx, ty, zi;
    Inverse(zi, p[2]);
    Mul(tx, p[0], zi);
    Mul(ty, p[1], zi);
    Pack25519(r, ty);
    r[31] ^= uint8_t(Parity(tx) << 7);
}
void ScalarMult(gf p[4], gf q[4], const uint8_t* s) {
    Set(p[0], gf0);
    Set(p[1], gf1);
    Set(p[2], gf1);
    Set(p[3], gf0);
    for (int i = 255; i >= 0; --i) {
        const uint8_t b = (s[i / 8] >> (i & 7)) & 1;
        PointSwap(p, q, b);
        PointAdd(q, p);
        PointAdd(p, p);
        PointSwap(p, q, b);
    }
}
void ScalarBase(gf p[4], const uint8_t* s) {
    gf q[4];
    Set(q[0], X);
    Set(q[1], Y);
    Set(q[2], gf1);
    Mul(q[3], X, Y);
    ScalarMult(p, q, s);
}
void ModL(uint8_t* r, int64_t x[64]) {
    int64_t carry;
    for (int i = 63; i >= 32; --i) {
        carry = 0;
        int j;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * L[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= uint64_t(carry) << 8;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (int j = 0; j < 32; ++j) {
        x[j] += carry - (x[31] >> 4) * L[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (int j = 0; j < 32; ++j) x[j] -= carry * L[j];
    for (int i = 0; i < 32; ++i) {
        x[i + 1] += x[i] >> 8;
        r[i] = uint8_t(x[i] & 255);
    }
}
void Reduce(uint8_t* r) {
    int64_t x[64];
    for (int i = 0; i < 64; ++i) x[i] = r[i];
    for (int i = 0; i < 64; ++i) r[i] = 0;
    ModL(r, x);
}
bool UnpackNegative(gf r[4], const uint8_t p[32]) {
    gf t, chk, num, den, den2, den4, den6;
    Set(r[2], gf1);
    Unpack25519(r[1], p);
    Square(num, r[1]);
    Mul(den, num, D);
    Sub(num, num, r[2]);
    Add(den, r[2], den);
    Square(den2, den);
    Square(den4, den2);
    Mul(den6, den4, den2);
    Mul(t, den6, num);
    Mul(t, t, den);
    Pow2523(t, t);
    Mul(t, t, num);
    Mul(t, t, den);
    Mul(t, t, den);
    Mul(r[0], t, den);
    Square(chk, r[0]);
    Mul(chk, chk, den);
    if (Neq(chk, num)) Mul(r[0], r[0], I);
    Square(chk, r[0]);
    Mul(chk, chk, den);
    if (Neq(chk, num)) return false;
    if (Parity(r[0]) == (p[31] >> 7)) Sub(r[0], gf0, r[0]);
    Mul(r[3], r[0], r[1]);
    return true;
}
bool Canonical(const uint8_t s[32]) {
    for (int i = 31; i >= 0; --i) {
        if (s[i] < L[i]) return true;
        if (s[i] > L[i]) return false;
    }
    return false;
}

const uint64_t K[80] = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull, 0x3956c25bf348b538ull, 0x59f111f1b605d019ull,
    0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull, 0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull, 0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull,
    0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull, 0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
    0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull, 0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull,
    0x06ca6351e003826full, 0x142929670a0e6e70ull, 0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
    0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull, 0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull,
    0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull, 0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull, 0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull,
    0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull, 0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull, 0xca273eceea26619cull, 0xd186b8c721c0c207ull,
    0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull, 0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
    0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull, 0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull,
    0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull};
uint64_t Rotr(uint64_t x, int c) { return (x >> c) | (x << (64 - c)); }
void Block(uint64_t h[8], const uint8_t* p) {
    uint64_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = 0;
        for (int b = 0; b < 8; ++b) w[i] = (w[i] << 8) | p[i * 8 + b];
    }
    for (int i = 16; i < 80; ++i) {
        const uint64_t s0 = Rotr(w[i - 15], 1) ^ Rotr(w[i - 15], 8) ^ (w[i - 15] >> 7);
        const uint64_t s1 = Rotr(w[i - 2], 19) ^ Rotr(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 80; ++i) {
        const uint64_t t1 = k + (Rotr(e, 14) ^ Rotr(e, 18) ^ Rotr(e, 41)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        const uint64_t t2 = (Rotr(a, 28) ^ Rotr(a, 34) ^ Rotr(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}
}

void Sha512(const uint8_t* data, size_t size, uint8_t out[64]) {
    uint64_t h[8] = {0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
                     0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull};
    size_t at = 0;
    for (; size - at >= 128; at += 128) Block(h, data + at);
    uint8_t tail[256] = {};
    const size_t rest = size - at;
    if (rest) std::memcpy(tail, data + at, rest);
    tail[rest] = 0x80;
    const size_t length = rest < 112 ? 128 : 256;
    const uint64_t bits = uint64_t(size) << 3;
    tail[length - 9] = uint8_t(uint64_t(size) >> 61);
    for (int i = 0; i < 8; ++i) tail[length - 1 - i] = uint8_t(bits >> (8 * i));
    Block(h, tail);
    if (length == 256) Block(h, tail + 128);
    for (int i = 0; i < 8; ++i)
        for (int b = 0; b < 8; ++b) out[i * 8 + b] = uint8_t(h[i] >> (56 - 8 * b));
}

bool Verify(const uint8_t signature[64], const uint8_t* message, size_t size, const uint8_t public_key[32]) {
    if (!Canonical(signature + 32)) return false;
    gf p[4], q[4];
    if (!UnpackNegative(q, public_key)) return false;
    std::vector<uint8_t> buffer(64 + size);
    std::memcpy(buffer.data(), signature, 32);
    std::memcpy(buffer.data() + 32, public_key, 32);
    if (size) std::memcpy(buffer.data() + 64, message, size);
    uint8_t h[64];
    Sha512(buffer.data(), buffer.size(), h);
    Reduce(h);
    ScalarMult(p, q, h);
    ScalarBase(q, signature + 32);
    PointAdd(p, q);
    uint8_t t[32];
    PointPack(t, p);
    return Verify32(signature, t) == 0;
}

void PublicKey(const uint8_t seed[32], uint8_t public_key[32]) {
    uint8_t d[64];
    Sha512(seed, 32, d);
    d[0] &= 248;
    d[31] &= 127;
    d[31] |= 64;
    gf p[4];
    ScalarBase(p, d);
    PointPack(public_key, p);
}

void Sign(const uint8_t seed[32], const uint8_t* message, size_t size, uint8_t signature[64]) {
    uint8_t d[64], pk[32];
    Sha512(seed, 32, d);
    d[0] &= 248;
    d[31] &= 127;
    d[31] |= 64;
    PublicKey(seed, pk);
    std::vector<uint8_t> buffer(64 + size);
    std::memcpy(buffer.data() + 32, d + 32, 32);
    if (size) std::memcpy(buffer.data() + 64, message, size);
    uint8_t r[64];
    Sha512(buffer.data() + 32, 32 + size, r);
    Reduce(r);
    gf p[4];
    ScalarBase(p, r);
    PointPack(signature, p);
    std::memcpy(buffer.data(), signature, 32);
    std::memcpy(buffer.data() + 32, pk, 32);
    uint8_t h[64];
    Sha512(buffer.data(), buffer.size(), h);
    Reduce(h);
    int64_t x[64] = {};
    for (int i = 0; i < 32; ++i) x[i] = r[i];
    for (int i = 0; i < 32; ++i)
        for (int j = 0; j < 32; ++j) x[i + j] += int64_t(h[i]) * int64_t(d[j]);
    ModL(signature + 32, x);
}

std::optional<std::string> FromHex(std::string_view text, size_t bytes) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    if (text.size() != bytes * 2) return std::nullopt;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out(bytes, '\0');
    for (size_t i = 0; i < bytes; ++i) {
        const int hi = nibble(text[2 * i]), lo = nibble(text[2 * i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out[i] = char(hi << 4 | lo);
    }
    return out;
}

std::string ToHex(const uint8_t* data, size_t size) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out += hex[data[i] >> 4];
        out += hex[data[i] & 15];
    }
    return out;
}

}
