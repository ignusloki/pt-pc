#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace pt::game {

struct LisaDateCommand {
    enum class Op : uint8_t { Scene, Bg, Music, Expr, Light, Cam, Fx, Say, Wait, Choice, Goto, If, Ending, Selfie, Nop };
    Op op = Op::Nop;
    std::string a;
    std::string b;
    float number = 0.0f;
    int target = -1;
    int line = 0;
    std::string expr;
};

struct LisaDateOption {
    std::string key;
    int affection = 0;
    std::string flag;
    int start = -1;
};

struct LisaDateChoice {
    std::string key;
    std::vector<LisaDateOption> options;
};

class LisaDateScript {
public:
    bool Parse(std::string_view text, std::string* error = nullptr);
    static std::map<std::string, std::string> ParseText(std::string_view text);
    const std::vector<LisaDateCommand>& Commands() const { return commands_; }
    const std::vector<LisaDateChoice>& Choices() const { return choices_; }
    int Label(std::string_view name) const;

private:
    std::vector<LisaDateCommand> commands_;
    std::vector<LisaDateChoice> choices_;
    std::map<std::string, int, std::less<>> labels_;
};

class LisaDateText {
public:
    void SetEnglish(std::map<std::string, std::string> table) { english_ = std::move(table); }
    void SetLanguage(std::map<std::string, std::string> table) { language_ = std::move(table); }
    std::string Get(std::string_view key) const;
    bool Has(std::string_view key) const;
    size_t EnglishCount() const { return english_.size(); }
    std::string All() const {
        std::string out;
        for (const auto& [k, v] : english_) out += v;
        for (const auto& [k, v] : language_) out += v;
        return out;
    }
    size_t LanguageCount() const { return language_.size(); }

private:
    std::map<std::string, std::string> english_;
    std::map<std::string, std::string> language_;
};

class LisaDateRun {
public:
    enum class Stop { Line, Choice, Wait, Title, Ending, Selfie, Done };
    struct Directive {
        LisaDateCommand::Op op;
        std::string name;
    };

    explicit LisaDateRun(const LisaDateScript& script) : script_(script) {}
    void Restart();
    Stop Advance(std::vector<Directive>& directives);
    bool Choose(int index);

    const LisaDateCommand* Current() const;
    const LisaDateChoice* CurrentChoice() const;
    int Affection() const { return affection_; }
    int Secrets() const { return static_cast<int>(flags_.size()); }
    bool Flag(std::string_view name) const { return flags_.contains(std::string(name)); }
    const std::set<std::string>& Flags() const { return flags_; }
    std::string_view EndingName() const { return ending_; }
    void SetVar(const std::string& name, int value) { vars_[name] = value; }
    int Var(const std::string& name) const {
        auto it = vars_.find(name);
        return it == vars_.end() ? 0 : it->second;
    }
    int ChoicesMade() const { return choices_made_; }

private:
    bool Test(const LisaDateCommand& c) const;

    const LisaDateScript& script_;
    int pc_ = 0;
    int current_ = -1;
    int affection_ = 0;
    int choices_made_ = 0;
    std::set<std::string> flags_;
    std::map<std::string, int> vars_;
    std::string ending_;
};

std::filesystem::path LisaDateDataDir();

}
