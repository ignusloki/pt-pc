#pragma once

#include <cctype>
#include <charconv>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Keep metadata types distinct from engine/core/json.h: shared template symbols must not mix their different layouts.
namespace pt::metadata_json {

struct Value {
    std::string text;
    std::map<std::string, Value> members;
    std::vector<Value> items;
    bool object = false;
    bool is_string = false;
};
class Reader {
public:
    explicit Reader(std::string_view s) : s_(s) {}
    bool Parse(Value& out) {
        if (!Read(out, 0)) return false;
        Space();
        return at_ == s_.size();
    }

private:
    void Space() {
        while (at_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[at_]))) ++at_;
    }
    bool String(std::string& out) {
        if (at_ >= s_.size() || s_[at_] != '"') return false;
        for (++at_; at_ < s_.size(); ++at_) {
            const char c = s_[at_];
            if (c == '"') {
                ++at_;
                return true;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (++at_ >= s_.size()) return false;
            switch (s_[at_]) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': break;
                case 'b': case 'f': break;
                case 'u': {
                    unsigned code = 0;
                    if (at_ + 4 >= s_.size() || std::from_chars(s_.data() + at_ + 1, s_.data() + at_ + 5, code, 16).ec != std::errc()) return false;
                    at_ += 4;
                    if (code < 0x80) out.push_back(char(code));
                    else if (code < 0x800) { out.push_back(char(0xC0 | (code >> 6))); out.push_back(char(0x80 | (code & 0x3F))); }
                    else { out.push_back(char(0xE0 | (code >> 12))); out.push_back(char(0x80 | ((code >> 6) & 0x3F))); out.push_back(char(0x80 | (code & 0x3F))); }
                    break;
                }
                default: out.push_back(s_[at_]); break;
            }
        }
        return false;
    }
    bool Read(Value& out, int depth) {
        if (depth > 16) return false;
        Space();
        if (at_ >= s_.size()) return false;
        const char c = s_[at_];
        if (c == '"') {
            out.is_string = true;
            return String(out.text);
        }
        if (c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            out.object = c == '{';
            ++at_;
            Space();
            if (at_ < s_.size() && s_[at_] == close) { ++at_; return true; }
            for (;;) {
                Space();
                std::string key;
                if (out.object) {
                    if (!String(key)) return false;
                    Space();
                    if (at_ >= s_.size() || s_[at_++] != ':') return false;
                }
                Value item;
                if (!Read(item, depth + 1)) return false;
                if (out.object) out.members[key] = std::move(item);
                else out.items.push_back(std::move(item));
                Space();
                if (at_ >= s_.size()) return false;
                if (s_[at_] == ',') { ++at_; continue; }
                if (s_[at_] == close) { ++at_; return true; }
                return false;
            }
        }
        const size_t start = at_;
        while (at_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[at_])) || s_[at_] == '.' || s_[at_] == '-' || s_[at_] == '+')) ++at_;
        out.text = std::string(s_.substr(start, at_ - start));
        return at_ > start;
    }
    std::string_view s_;
    size_t at_ = 0;
};
inline const std::string* Text(const Value& object, const char* key) {
    const auto it = object.members.find(key);
    return it != object.members.end() && it->second.is_string && !it->second.text.empty() ? &it->second.text : nullptr;
}

}
