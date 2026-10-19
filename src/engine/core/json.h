#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pt::json {

struct Member;

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
    const Value* Find(std::string_view key) const;
    double Number(std::string_view key, double fallback) const;
    std::string String(std::string_view key, std::string_view fallback = {}) const;
    bool Bool(std::string_view key, bool fallback) const;
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
