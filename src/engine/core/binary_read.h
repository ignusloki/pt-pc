#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace pt {

template <typename T>
T ReadBinaryUnchecked(const uint8_t* data) {
    T value;
    std::memcpy(&value, data, sizeof(T));
    return value;
}

template <typename T>
T ReadBinaryUnchecked(std::span<const uint8_t> data, size_t offset) {
    return ReadBinaryUnchecked<T>(data.data() + offset);
}

template <typename T>
T ReadBinary(std::span<const uint8_t> data, size_t offset) {
    T value{};
    if (offset + sizeof(T) <= data.size()) {
        std::memcpy(&value, data.data() + offset, sizeof(T));
    }
    return value;
}

struct BinaryReader {
    std::span<const uint8_t> data;

    template <typename T>
    T At(size_t offset) const { return ReadBinary<T>(data, offset); }
};

}
