#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pt::ed25519 {

void Sha512(const uint8_t* data, size_t size, uint8_t out[64]);

bool Verify(const uint8_t signature[64], const uint8_t* message, size_t size, const uint8_t public_key[32]);

void PublicKey(const uint8_t seed[32], uint8_t public_key[32]);
void Sign(const uint8_t seed[32], const uint8_t* message, size_t size, uint8_t signature[64]);

std::optional<std::string> FromHex(std::string_view text, size_t bytes);
std::string ToHex(const uint8_t* data, size_t size);

}
