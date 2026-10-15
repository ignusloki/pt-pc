#include "game/ui/notice_queue.h"
#include "game/ui/pc_settings.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failed = 0;

void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failed += !ok;
}

}

int main() {
    using pt::game::NoticeQueue;
    const std::string update = "pc_update_notice";
    const std::string tier = std::string("pc_gameplus_notice") + pt::game::kPcNoteArgument + "2";
    const std::string enhanced = "pc_notice_enhanced_working";

    {
        NoticeQueue queue;
        Check(queue.Push(update) && queue.Push(tier) && queue.Push(enhanced), "three lines queue at once");
        Check(!queue.Push(tier), "a line that is waiting is not queued again");
        std::vector<std::string> order;
        std::vector<float> lengths;
        std::string last;
        float shown = 0.0f;
        bool overlap = false, early = false;
        const float dt = 1.0f / 60.0f;
        for (int frame = 0; frame < 60 * 40; ++frame) {
            queue.Update(dt, true);
            if (queue.Visible()) {
                if (queue.Current() != last) {
                    if (!last.empty()) {
                        lengths.push_back(shown);
                        early |= shown < NoticeQueue::kLength - 0.1f;
                    }
                    order.push_back(queue.Current());
                    last = queue.Current();
                    shown = 0.0f;
                }
                shown += dt;
                overlap |= queue.Alpha() <= 0.0f && queue.Time() > 0.01f && queue.Time() < NoticeQueue::kLength - 0.01f;
            }
        }
        lengths.push_back(shown);
        Check(order.size() == 3 && order[0] == update && order[1] == tier && order[2] == enhanced, "the lines come in the order they were asked");
        Check(!early && lengths.size() == 3 && lengths[2] > NoticeQueue::kLength - 0.1f, "each line stays for its whole time");
        Check(!overlap, "a line on screen has an opacity");
        Check(queue.Idle(), "the queue is empty after the last");
    }

    {
        NoticeQueue queue;
        queue.Push(update);
        queue.Update(0.0f, false);
        Check(!queue.Visible() && queue.Time() < 0.0f, "a line waits while the banner may not show");
        for (int i = 0; i < 100; ++i) queue.Update(0.1f, true);
        Check(!queue.Visible() && queue.Idle(), "a line is not shown past its time");
    }
    {
        NoticeQueue queue;
        queue.Push(update);
        queue.Push(tier);
        for (int i = 0; i < 30; ++i) queue.Update(0.1f, true);
        const float at = queue.Time();
        Check(queue.Visible() && at >= NoticeQueue::kSeen && at < NoticeQueue::kLength, "a line has been on screen for a while");
        queue.Update(0.1f, false);
        Check(!queue.Visible() && queue.Cut() && queue.Done() && queue.Time() == at, "the line taken away is cut and hidden");
        int shown_update = 0;
        std::vector<std::string> after;
        std::string last;
        for (int i = 0; i < 400; ++i) {
            queue.Update(0.1f, true);
            if (queue.Visible() && queue.Current() != last) {
                last = queue.Current();
                after.push_back(last);
                shown_update += last == update;
            }
        }
        Check(shown_update == 0 && after.size() == 1 && after[0] == tier, "the cut line is not shown again, the next line comes");
        Check(queue.Idle(), "the queue is empty after it");
    }
    {
        NoticeQueue queue;
        queue.Push(update);
        for (int i = 0; i < 6; ++i) queue.Update(0.1f, true);
        queue.Update(0.1f, false);
        Check(!queue.Visible() && queue.Restarted() && !queue.Cut() && queue.Time() < 0.0f, "a line taken away too soon starts again");
        queue.Update(0.1f, false);
        Check(!queue.Restarted(), "the restart is reported once");
        queue.Update(0.1f, true);
        Check(queue.Visible() && queue.Current() == update && queue.Time() <= 0.0f, "it comes again from its beginning");
        for (int i = 0; i < 100; ++i) queue.Update(0.1f, true);
        Check(queue.Idle() && !queue.Cut(), "and then it is shown for its whole time");
    }
    {
        NoticeQueue queue;
        queue.Push(enhanced);
        for (int i = 0; i < 30; ++i) queue.Update(0.1f, true);
        queue.Update(0.1f, false);
        Check(queue.Cut() && queue.Push(enhanced), "a cut line can come again as a new event");
    }

    {
        NoticeQueue queue;
        queue.Push(enhanced);
        for (int i = 0; i < 100; ++i) queue.Update(0.1f, true);
        Check(queue.Push(enhanced), "a finished line can come again");
    }

    for (int language = 0; language < 14; ++language) {
        for (const char* key : {"pc_notice_enhanced_working", "pc_notice_enhanced_ready", "pc_boot_preparing_textures", "pc_boot_textures_skip",
                                "pc_boot_textures_ask"}) {
            const std::string text = pt::game::PcNoteText(key, language);
            const bool ok = !text.empty() && text != key && text.size() < (std::string(key) == "pc_boot_textures_ask" ? 300u : 140u) && text.find('-') == std::string::npos;
            if (!ok) std::printf("FAIL: %s in language %d: %s\n", key, language, text.c_str());
            failed += !ok;
        }
    }
    return failed ? 1 : 0;
}
