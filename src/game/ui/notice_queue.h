#pragma once

#include <cstddef>
#include <deque>
#include <string>

namespace pt::game {

class NoticeQueue {
public:
    static constexpr float kFadeIn = 0.5f;
    static constexpr float kHold = 6.0f;
    static constexpr float kFadeOut = 1.0f;
    static constexpr float kLength = kFadeIn + kHold + kFadeOut;
    static constexpr float kSeen = 2.0f;

    bool Push(std::string note);
    void Update(float dt, bool allowed);
    bool Visible() const { return visible_; }
    const std::string& Current() const { return current_; }
    float Time() const { return time_; }
    bool Done() const { return !current_.empty() && Finished(); }
    bool Cut() const { return cut_; }
    bool Restarted() const { return restarted_; }
    float Alpha() const;
    size_t Waiting() const { return waiting_.size(); }
    bool Idle() const { return waiting_.empty() && (current_.empty() || Finished()); }

private:
    bool Finished() const { return cut_ || time_ >= kLength; }

    std::string current_;
    std::deque<std::string> waiting_;
    float time_ = -1.0f;
    bool visible_ = false;
    bool cut_ = false;
    bool restarted_ = false;
};

}
