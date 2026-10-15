#include "engine/platform/os.h"
#include "game/lisa_date/lisa_date_script.h"

#include <cstdlib>
#include <format>
#include <sstream>

#include "engine/core/resource_path.h"

#ifndef PT_LISA_DATE_DIR
#define PT_LISA_DATE_DIR ""
#endif

namespace pt::game {
namespace {

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::vector<std::string> Words(std::string_view line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        const size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
        if (i > start) out.emplace_back(line.substr(start, i - start));
    }
    return out;
}

bool ToInt(const std::string& s, int& out) {
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (!end || *end != '\0' || s.empty()) return false;
    out = static_cast<int>(v);
    return true;
}

}

bool LisaDateScript::Parse(std::string_view text, std::string* error) {
    commands_.clear();
    choices_.clear();
    labels_.clear();
    auto fail = [&](int line, const std::string& why) {
        if (error) *error = std::format("script line {}: {}", line, why);
        return false;
    };
    using Op = LisaDateCommand::Op;
    struct PendingJump {
        size_t command;
        std::string label;
        int line;
    };
    std::vector<PendingJump> jumps;
    int open_choice = -1;
    std::vector<size_t> option_ends;
    int line_number = 0;
    size_t at = 0;
    while (at <= text.size()) {
        size_t next = text.find('\n', at);
        if (next == std::string_view::npos) next = text.size();
        std::string_view raw = text.substr(at, next - at);
        at = next + 1;
        ++line_number;
        if (const size_t hash = raw.find('#'); hash != std::string_view::npos) raw = raw.substr(0, hash);
        const std::vector<std::string> w = Words(Trim(raw));
        if (w.empty()) {
            if (next >= text.size()) break;
            continue;
        }
        const std::string& op = w[0];
        auto need = [&](size_t n) { return w.size() >= n + 1; };
        LisaDateCommand c;
        c.line = line_number;
        if (op == "option") {
            if (open_choice < 0) return fail(line_number, "option outside a choice");
            if (!need(2)) return fail(line_number, "option <key> <affection>");
            LisaDateOption o;
            o.key = w[1];
            if (!ToInt(w[2], o.affection)) return fail(line_number, "affection is not a number");
            std::string go;
            for (size_t i = 3; i < w.size(); ++i) {
                if (w[i].starts_with("flag:")) o.flag = w[i].substr(5);
                else if (w[i].starts_with("goto:")) go = w[i].substr(5);
                else return fail(line_number, "unknown option word " + w[i]);
            }
            if (!choices_[static_cast<size_t>(open_choice)].options.empty()) {
                option_ends.push_back(commands_.size());
                commands_.push_back({Op::Goto, {}, {}, 0.0f, -1, line_number});
            }
            o.start = static_cast<int>(commands_.size());
            choices_[static_cast<size_t>(open_choice)].options.push_back(o);
            if (!go.empty()) {
                jumps.push_back({commands_.size(), go, line_number});
                commands_.push_back({Op::Goto, go, {}, 0.0f, -1, line_number});
            }
            if (next >= text.size()) break;
            continue;
        }
        if (op == "end") {
            if (open_choice < 0) return fail(line_number, "end without a choice");
            if (choices_[static_cast<size_t>(open_choice)].options.empty()) return fail(line_number, "a choice without options");
            const int after = static_cast<int>(commands_.size());
            for (size_t e : option_ends) commands_[e].target = after;
            option_ends.clear();
            open_choice = -1;
            if (next >= text.size()) break;
            continue;
        }
        if (op == "choice") {
            if (open_choice >= 0) return fail(line_number, "a choice inside a choice");
            choices_.push_back({need(1) ? w[1] : std::format("choice{}", choices_.size() + 1), {}});
            open_choice = static_cast<int>(choices_.size()) - 1;
            c.op = Op::Choice;
            c.a = choices_.back().key;
            c.target = open_choice;
        } else if (op == "label") {
            if (!need(1)) return fail(line_number, "label <name>");
            labels_[w[1]] = static_cast<int>(commands_.size());
            if (next >= text.size()) break;
            continue;
        } else if (op == "scene") {
            if (!need(2)) return fail(line_number, "scene <n> <title key>");
            c.op = Op::Scene;
            c.a = w[1];
            c.b = w[2];
        } else if (op == "say") {
            if (!need(2)) return fail(line_number, "say <who> <key>");
            if (w[1] != "lisa" && w[1] != "you" && w[1] != "none" && w[1] != "glitch") return fail(line_number, "unknown speaker " + w[1]);
            c.op = Op::Say;
            c.a = w[1];
            c.b = w[2];
            if (w.size() >= 4) c.expr = w[3];
        } else if (op == "wait") {
            if (!need(1)) return fail(line_number, "wait <seconds>");
            c.op = Op::Wait;
            c.number = std::strtof(w[1].c_str(), nullptr);
        } else if (op == "goto") {
            if (!need(1)) return fail(line_number, "goto <label>");
            c.op = Op::Goto;
            c.a = w[1];
            jumps.push_back({commands_.size(), w[1], line_number});
        } else if (op == "if") {
            if (!need(4)) return fail(line_number, "if <var> <op> <number> <label>");
            static constexpr std::string_view kOps[] = {"<", "<=", ">", ">=", "==", "!="};
            bool known = false;
            for (std::string_view k : kOps) known = known || w[2] == k;
            if (!known) return fail(line_number, "unknown comparison " + w[2]);
            if (w[1] != "affection" && w[1] != "secrets" && !w[1].starts_with("flag:") && w[1] != "selfie:lisa" && w[1] != "selfie:you") {
                return fail(line_number, "unknown variable " + w[1]);
            }
            c.op = Op::If;
            c.a = w[1];
            c.b = w[2];
            c.number = std::strtof(w[3].c_str(), nullptr);
            jumps.push_back({commands_.size(), w[4], line_number});
        } else if (op == "selfie") {
            c.op = Op::Selfie;
        } else if (op == "ending") {
            if (!need(1) || (w[1] != "good" && w[1] != "bad" && w[1] != "true")) return fail(line_number, "ending good|bad|true");
            c.op = Op::Ending;
            c.a = w[1];
        } else {
            static constexpr std::pair<std::string_view, Op> kDirectives[] = {
                {"bg", Op::Bg}, {"music", Op::Music}, {"expr", Op::Expr}, {"light", Op::Light}, {"cam", Op::Cam}, {"fx", Op::Fx}};
            bool found = false;
            for (const auto& [name, d] : kDirectives) {
                if (op == name) {
                    if (!need(1)) return fail(line_number, op + " <name>");
                    c.op = d;
                    c.a = w[1];
                    found = true;
                }
            }
            if (!found) return fail(line_number, "unknown command " + op);
        }
        commands_.push_back(std::move(c));
        if (next >= text.size()) break;
    }
    if (open_choice >= 0) return fail(line_number, "choice without end");
    for (const PendingJump& j : jumps) {
        const int target = Label(j.label);
        if (target < 0) return fail(j.line, "no label " + j.label);
        commands_[j.command].target = target;
    }
    return true;
}

int LisaDateScript::Label(std::string_view name) const {
    auto it = labels_.find(name);
    return it == labels_.end() ? -1 : it->second;
}

std::map<std::string, std::string> LisaDateScript::ParseText(std::string_view text) {
    std::map<std::string, std::string> out;
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    size_t at = 0;
    while (at < text.size()) {
        size_t next = text.find('\n', at);
        if (next == std::string_view::npos) next = text.size();
        const std::string_view line = Trim(text.substr(at, next - at));
        at = next + 1;
        if (line.empty() || line.front() == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view key = Trim(line.substr(0, eq));
        const std::string_view value = Trim(line.substr(eq + 1));
        if (!key.empty()) out[std::string(key)] = std::string(value);
    }
    return out;
}

std::string LisaDateText::Get(std::string_view key) const {
    const std::string k(key);
    if (auto it = language_.find(k); it != language_.end()) return it->second;
    if (auto it = english_.find(k); it != english_.end()) return it->second;
    return k;
}

bool LisaDateText::Has(std::string_view key) const {
    const std::string k(key);
    return language_.contains(k) || english_.contains(k);
}

void LisaDateRun::Restart() {
    pc_ = 0;
    current_ = -1;
    affection_ = 0;
    choices_made_ = 0;
    flags_.clear();
    vars_.clear();
    ending_.clear();
}

bool LisaDateRun::Test(const LisaDateCommand& c) const {
    float value = 0.0f;
    if (c.a == "affection") value = static_cast<float>(affection_);
    else if (c.a == "secrets") value = static_cast<float>(flags_.size());
    else if (c.a.starts_with("flag:")) value = flags_.contains(c.a.substr(5)) ? 1.0f : 0.0f;
    else if (c.a.starts_with("selfie:")) value = static_cast<float>(Var(c.a));
    const float n = c.number;
    if (c.b == "<") return value < n;
    if (c.b == "<=") return value <= n;
    if (c.b == ">") return value > n;
    if (c.b == ">=") return value >= n;
    if (c.b == "==") return value == n;
    return value != n;
}

LisaDateRun::Stop LisaDateRun::Advance(std::vector<Directive>& directives) {
    using Op = LisaDateCommand::Op;
    const auto& commands = script_.Commands();
    for (int steps = 0; steps < 100000; ++steps) {
        if (pc_ < 0 || pc_ >= static_cast<int>(commands.size())) {
            current_ = -1;
            return Stop::Done;
        }
        const LisaDateCommand& c = commands[static_cast<size_t>(pc_)];
        current_ = pc_;
        switch (c.op) {
        case Op::Bg:
        case Op::Music:
        case Op::Expr:
        case Op::Light:
        case Op::Cam:
        case Op::Fx:
            directives.push_back({c.op, c.a});
            ++pc_;
            break;
        case Op::Goto:
            pc_ = c.target;
            break;
        case Op::If:
            pc_ = Test(c) ? c.target : pc_ + 1;
            break;
        case Op::Say:
            ++pc_;
            return Stop::Line;
        case Op::Scene:
            ++pc_;
            return Stop::Title;
        case Op::Wait:
            ++pc_;
            return Stop::Wait;
        case Op::Choice:
            return Stop::Choice;
        case Op::Selfie:
            ++pc_;
            return Stop::Selfie;
        case Op::Ending:
            ending_ = c.a;
            pc_ = static_cast<int>(commands.size());
            return Stop::Ending;
        case Op::Nop:
            ++pc_;
            break;
        }
    }
    return Stop::Done;
}

bool LisaDateRun::Choose(int index) {
    const LisaDateChoice* choice = CurrentChoice();
    if (!choice || index < 0 || index >= static_cast<int>(choice->options.size())) return false;
    const LisaDateOption& o = choice->options[static_cast<size_t>(index)];
    affection_ += o.affection;
    if (!o.flag.empty()) flags_.insert(o.flag);
    ++choices_made_;
    pc_ = o.start;
    return true;
}

const LisaDateCommand* LisaDateRun::Current() const {
    const auto& commands = script_.Commands();
    return current_ >= 0 && current_ < static_cast<int>(commands.size()) ? &commands[static_cast<size_t>(current_)] : nullptr;
}

const LisaDateChoice* LisaDateRun::CurrentChoice() const {
    const LisaDateCommand* c = Current();
    if (!c || c->op != LisaDateCommand::Op::Choice || pc_ != current_) return nullptr;
    return c->target >= 0 && c->target < static_cast<int>(script_.Choices().size()) ? &script_.Choices()[static_cast<size_t>(c->target)] : nullptr;
}

std::filesystem::path LisaDateDataDir() {
    const std::filesystem::path beside = pt::ExecutableDir() / "lisa_date";
    std::error_code ec;
    if (std::filesystem::is_directory(beside, ec)) return beside;
    return pt::os::PathFromUtf8(PT_LISA_DATE_DIR);
}

}
