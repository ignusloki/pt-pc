#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pt::json {

struct Member;

// A small JSON document model for the mods' glTF models and level.json (RFC 8259; comments and trailing commas let through).
// An object's members are a struct declared after Value: a std::pair holding Value inside Value does not compile with
// libstdc++ (Linux), which requires the pair's types complete
struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    std::vector<Member> object;

    bool IsNull() const { return type == Type::Null; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }
    // the member of an object, or nullptr
    const Value* Find(std::string_view key) const;
    double Number(std::string_view key, double fallback) const;
    std::string String(std::string_view key, std::string_view fallback = {}) const;
    bool Bool(std::string_view key, bool fallback) const;
    // the element of an array, or nullptr
    const Value* At(size_t index) const;
    size_t Size() const;
};

struct Member {
    std::string key;
    Value value;
};

inline const Value* Value::At(size_t index) const { return type == Type::Array && index < array.size() ? &array[index] : nullptr; }
inline size_t Value::Size() const { return type == Type::Array ? array.size() : type == Type::Object ? object.size() : 0; }

bool Parse(std::string_view text, Value& out, std::string* error = nullptr);

}
