#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pt::route_queue {

enum class Route { Fg = 0, Nr = 1 };
enum class Op { Install = 1, Remove = 2, Reinstall = 3 };

struct Entry {
    Route route = Route::Fg;
    Op op = Op::Install;
};

inline const char* RouteName(Route route) { return route == Route::Fg ? "fg" : "nr"; }
inline const char* OpName(Op op) { return op == Op::Remove ? "remove" : op == Op::Reinstall ? "reinstall" : "install"; }

inline int Merge(int pending, Op request) {
    const int next = static_cast<int>(request);
    if (pending == 0) return next;
    switch (static_cast<Op>(pending)) {
        case Op::Install:
            return request == Op::Remove ? 0 : pending;
        case Op::Remove:
            return request == Op::Remove ? pending : static_cast<int>(Op::Reinstall);
        case Op::Reinstall:
            return request == Op::Remove ? next : pending;
    }
    return next;
}

class Queue {
public:
    int Request(Route route, Op op) {
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].route != route) continue;
            const int merged = Merge(static_cast<int>(entries_[i].op), op);
            if (merged == 0) entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
            else entries_[i].op = static_cast<Op>(merged);
            return merged;
        }
        entries_.push_back({route, op});
        return static_cast<int>(op);
    }
    int Pending(Route route) const {
        for (const Entry& e : entries_)
            if (e.route == route) return static_cast<int>(e.op);
        return 0;
    }
    bool Empty() const { return entries_.empty(); }
    const std::vector<Entry>& Entries() const { return entries_; }

    std::string Serialize() const {
        std::string text;
        for (const Entry& e : entries_) {
            if (!text.empty()) text += ',';
            text += RouteName(e.route);
            text += ':';
            text += OpName(e.op);
        }
        return text;
    }

    static bool Parse(std::string_view text, Queue& out) {
        out = Queue();
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find(',', at);
            if (end == std::string_view::npos) end = text.size();
            const std::string_view part = text.substr(at, end - at);
            at = end + 1;
            const size_t colon = part.find(':');
            if (colon == std::string_view::npos) return false;
            const std::string_view route = part.substr(0, colon), op = part.substr(colon + 1);
            Route r;
            if (route == "fg") r = Route::Fg;
            else if (route == "nr") r = Route::Nr;
            else return false;
            Op o;
            if (op == "install") o = Op::Install;
            else if (op == "remove") o = Op::Remove;
            else if (op == "reinstall") o = Op::Reinstall;
            else return false;
            out.Request(r, o);
        }
        return true;
    }

private:
    std::vector<Entry> entries_;
};

}
