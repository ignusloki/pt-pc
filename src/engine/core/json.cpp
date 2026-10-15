#include "engine/core/json.h"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <format>
#ifdef __APPLE__
#include <locale>
#include <sstream>
#endif

namespace pt::json {
namespace {

class Reader {
public:
    explicit Reader(std::string_view text) : text_(text) {}

    bool Document(Value& out, std::string* error) {
        if (!Read(out, 0)) {
            if (error) *error = std::format("{} at byte {}", error_, at_);
            return false;
        }
        Skip();
        if (at_ != text_.size()) {
            if (error) *error = std::format("unexpected text after the value at byte {}", at_);
            return false;
        }
        return true;
    }

private:
    bool Fail(const char* what) {
        error_ = what;
        return false;
    }

    void Skip() {
        while (at_ < text_.size()) {
            const char c = text_[at_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++at_;
            } else if (c == '/' && at_ + 1 < text_.size() && text_[at_ + 1] == '/') {
                while (at_ < text_.size() && text_[at_] != '\n') ++at_;
            } else if (c == '/' && at_ + 1 < text_.size() && text_[at_ + 1] == '*') {
                const size_t end = text_.find("*/", at_ + 2);
                at_ = end == std::string_view::npos ? text_.size() : end + 2;
            } else {
                break;
            }
        }
    }

    bool Literal(std::string_view word) {
        if (text_.substr(at_, word.size()) != word) return Fail("unknown value");
        at_ += word.size();
        return true;
    }

    static void PutUtf8(std::string& out, uint32_t c) {
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }

    bool Hex4(uint32_t& out) {
        if (at_ + 4 > text_.size()) return Fail("short \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[at_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<uint32_t>(c - 'A' + 10);
            else return Fail("bad \\u escape");
        }
        return true;
    }

    bool String(std::string& out) {
        ++at_;
        while (at_ < text_.size()) {
            const char c = text_[at_++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_ >= text_.size()) break;
            const char e = text_[at_++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t c1 = 0;
                if (!Hex4(c1)) return false;
                if (c1 >= 0xD800 && c1 < 0xDC00 && text_.substr(at_, 2) == "\\u") {
                    at_ += 2;
                    uint32_t c2 = 0;
                    if (!Hex4(c2)) return false;
                    c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
                }
                PutUtf8(out, c1);
                break;
            }
            default: return Fail("bad escape");
            }
        }
        return Fail("unterminated string");
    }

    bool Read(Value& out, int depth) {
        if (depth > 256) return Fail("nested too deep");
        Skip();
        if (at_ >= text_.size()) return Fail("value expected");
        const char c = text_[at_];
        if (c == '{') {
            out.type = Value::Type::Object;
            ++at_;
            while (true) {
                Skip();
                if (at_ < text_.size() && text_[at_] == '}') {
                    ++at_;
                    return true;
                }
                if (at_ >= text_.size() || text_[at_] != '"') return Fail("key expected");
                Member member;
                if (!String(member.key)) return false;
                Skip();
                if (at_ >= text_.size() || text_[at_] != ':') return Fail("':' expected");
                ++at_;
                if (!Read(member.value, depth + 1)) return false;
                out.object.push_back(std::move(member));
                Skip();
                if (at_ < text_.size() && text_[at_] == ',') {
                    ++at_;
                } else if (at_ < text_.size() && text_[at_] == '}') {
                    ++at_;
                    return true;
                } else {
                    return Fail("',' or '}' expected");
                }
            }
        }
        if (c == '[') {
            out.type = Value::Type::Array;
            ++at_;
            while (true) {
                Skip();
                if (at_ < text_.size() && text_[at_] == ']') {
                    ++at_;
                    return true;
                }
                Value element;
                if (!Read(element, depth + 1)) return false;
                out.array.push_back(std::move(element));
                Skip();
                if (at_ < text_.size() && text_[at_] == ',') {
                    ++at_;
                } else if (at_ < text_.size() && text_[at_] == ']') {
                    ++at_;
                    return true;
                } else {
                    return Fail("',' or ']' expected");
                }
            }
        }
        if (c == '"') {
            out.type = Value::Type::String;
            return String(out.string);
        }
        if (c == 't') {
            out.type = Value::Type::Bool;
            out.boolean = true;
            return Literal("true");
        }
        if (c == 'f') {
            out.type = Value::Type::Bool;
            return Literal("false");
        }
        if (c == 'n') {
            return Literal("null");
        }
        size_t end = at_;
        while (end < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[end])) || text_[end] == '-' || text_[end] == '+' ||
                                      text_[end] == '.' || text_[end] == 'e' || text_[end] == 'E')) {
            ++end;
        }
        if (end == at_) return Fail("unknown value");
        out.type = Value::Type::Number;
        const char* first = text_.data() + at_ + (text_[at_] == '+' ? 1 : 0);
#ifdef __APPLE__
        std::istringstream number(std::string(first, text_.data() + end));
        number.imbue(std::locale::classic());
        if (!(number >> out.number)) return Fail("bad number");
#else
        const auto result = std::from_chars(first, text_.data() + end, out.number);
        if (result.ec != std::errc()) return Fail("bad number");
#endif
        at_ = end;
        return true;
    }

    std::string_view text_;
    size_t at_ = 0;
    const char* error_ = "";
};

}

const Value* Value::Find(std::string_view key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& [name, value] : object) {
        if (name == key) return &value;
    }
    return nullptr;
}

double Value::Number(std::string_view key, double fallback) const {
    const Value* v = Find(key);
    return v && v->type == Type::Number ? v->number : fallback;
}

std::string Value::String(std::string_view key, std::string_view fallback) const {
    const Value* v = Find(key);
    return v && v->type == Type::String ? v->string : std::string(fallback);
}

bool Value::Bool(std::string_view key, bool fallback) const {
    const Value* v = Find(key);
    return v && v->type == Type::Bool ? v->boolean : fallback;
}

bool Parse(std::string_view text, Value& out, std::string* error) {
    out = Value{};
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    return Reader(text).Document(out, error);
}

}
