#include "game/ui/notice_queue.h"

#include <algorithm>
#include <utility>

namespace pt::game {

bool NoticeQueue::Push(std::string note) {
    if (note.empty()) return false;
    if ((!current_.empty() && !Finished() && current_ == note) || std::find(waiting_.begin(), waiting_.end(), note) != waiting_.end()) return false;
    waiting_.push_back(std::move(note));
    return true;
}

void NoticeQueue::Update(float dt, bool allowed) {
    if ((current_.empty() || Finished()) && !waiting_.empty()) {
        current_ = std::move(waiting_.front());
        waiting_.pop_front();
        time_ = -1.0f;
        cut_ = false;
    }
    visible_ = false;
    restarted_ = false;
    if (current_.empty() || Finished()) return;
    if (!allowed) {
        if (time_ >= kSeen) {
            cut_ = true;
        } else if (time_ >= 0.0f) {
            time_ = -1.0f;
            restarted_ = true;
        }
        return;
    }
    time_ = time_ < 0.0f ? 0.0f : time_ + dt;
    visible_ = !Finished();
}

float NoticeQueue::Alpha() const {
    if (time_ < 0.0f || Finished()) return 0.0f;
    const float fade = time_ < kFadeIn ? time_ / kFadeIn : time_ > kFadeIn + kHold ? (kLength - time_) / kFadeOut : 1.0f;
    return std::clamp(fade, 0.0f, 1.0f);
}

}
