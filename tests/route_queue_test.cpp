#include <cstdio>
#include <string>

#include "engine/render/upscale/route_queue.h"

using namespace pt::route_queue;

static int g_fails = 0;
static void Check(bool ok, const std::string& what) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fails;
}

int main() {
    const int table[4][3] = {
        /* none      */ {1, 2, 3},
        /* install   */ {1, 0, 1},
        /* remove    */ {3, 2, 3},
        /* reinstall */ {3, 2, 3},
    };
    const Op ops[3] = {Op::Install, Op::Remove, Op::Reinstall};
    for (int p = 0; p < 4; ++p)
        for (int r = 0; r < 3; ++r)
            Check(Merge(p, ops[r]) == table[p][r], std::string("merge ") + (p ? OpName(Op(p)) : "none") + " then " + OpName(ops[r]) + " = " +
                                                      (table[p][r] ? OpName(Op(table[p][r])) : "none"));

    {
        Queue q;
        q.Request(Route::Fg, Op::Install);
        q.Request(Route::Nr, Op::Install);
        Check(q.Pending(Route::Fg) == 1 && q.Pending(Route::Nr) == 1, "FG install then NR install: both pending");
        Check(q.Serialize() == "fg:install,nr:install", "FG install then NR install: " + q.Serialize());
    }
    {
        Queue q;
        q.Request(Route::Nr, Op::Install);
        q.Request(Route::Fg, Op::Install);
        Check(q.Serialize() == "nr:install,fg:install", "NR install then FG install: " + q.Serialize());
    }
    {
        Queue q;
        q.Request(Route::Fg, Op::Install);
        q.Request(Route::Nr, Op::Install);
        q.Request(Route::Fg, Op::Remove);
        Check(q.Pending(Route::Fg) == 0 && q.Pending(Route::Nr) == 1, "FG install then FG remove: FG back to nothing, NR still pending");
        Check(q.Serialize() == "nr:install", "after the cancel: " + q.Serialize());
        q.Request(Route::Fg, Op::Install);
        Check(q.Serialize() == "nr:install,fg:install", "FG asked again goes to the end: " + q.Serialize());
    }
    {
        Queue q;
        q.Request(Route::Fg, Op::Remove);
        q.Request(Route::Nr, Op::Reinstall);
        Check(q.Serialize() == "fg:remove,nr:reinstall", "FG remove then NR reinstall: " + q.Serialize());
    }
    {
        Queue q;
        q.Request(Route::Nr, Op::Install);
        q.Request(Route::Fg, Op::Install);
        q.Request(Route::Nr, Op::Install);
        Check(q.Entries().size() == 2 && q.Serialize() == "nr:install,fg:install", "a repeated request is one entry in its first place: " + q.Serialize());
    }
    {
        Queue q;
        Check(Queue::Parse("fg:remove,nr:reinstall", q) && q.Serialize() == "fg:remove,nr:reinstall", "parse of a list");
        Check(Queue::Parse("nr:install,nr:remove", q) && q.Empty(), "a list that cancels itself parses to nothing");
        Check(Queue::Parse("", q) && q.Empty(), "an empty list parses to nothing");
        Check(!Queue::Parse("fg:explode", q), "an unknown operation is refused");
        Check(!Queue::Parse("gpu:install", q), "an unknown route is refused");
        Check(!Queue::Parse("fg", q), "a part without an operation is refused");
    }
    std::printf(g_fails ? "FAIL: %d\n" : "PASS\n", g_fails);
    return g_fails ? 1 : 0;
}
