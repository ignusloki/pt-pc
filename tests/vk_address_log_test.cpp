#include <cstdio>
#include <string>
#include <vector>

#include "engine/render/vk_address_log.h"

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    std::printf("%s: %s\n", what, ok ? "PASS" : "FAIL");
    failures += !ok;
}

bool Has(const std::vector<std::string>& lines, const std::string& part) {
    for (const std::string& l : lines) {
        if (l.find(part) != std::string::npos) return true;
    }
    return false;
}

void Dump(const std::vector<std::string>& lines) {
    for (const std::string& l : lines) std::printf("  %s\n", l.c_str());
}

pt::vk::address_log::Binding B(uint64_t handle, int32_t type, uint64_t base, uint64_t size, bool bind = true) {
    pt::vk::address_log::Binding b;
    b.handle = handle;
    b.type = type;
    b.base = base;
    b.size = size;
    b.bind = bind;
    return b;
}

}

int main() {
    namespace al = pt::vk::address_log;
    al::Clear();
    al::Record(B(0x10, 10, 0x1000000, 0x10000));
    Check(al::Count().binds == 0, "nothing is kept while the report is off");
    al::SetEnabled(true);

    al::Record(B(0xA, 8, 0x20000000, 0x4000000));
    al::Record(B(0xB, 10, 0x20000000, 0x200000));
    al::Record(B(0xC, 10, 0x20200000, 0x200000));
    al::Describe(0xB, "scene targets image 1707x960x1 format 83");
    al::Describe(0xC, "scene targets image 2560x1440x1 format 97");

    std::vector<std::string> in_c = al::Explain(0x20234567, 0x1000);
    Dump(in_c);
    Check(!in_c.empty() && in_c[0].find("image 0xc") != std::string::npos, "the innermost live range comes first");
    Check(Has(in_c, "2560x1440x1"), "the port's words for the image are shown");
    Check(Has(in_c, "device memory 0xa"), "the memory block over it is shown too");
    Check(!Has(in_c, "image 0xb"), "a range beside the page is not shown");

    al::Record(B(0xC, 10, 0x20200000, 0x200000, false));
    std::vector<std::string> after = al::Explain(0x20234567, 0x1000);
    Dump(after);
    Check(Has(after, "was image 0xc") && Has(after, "unbound at"), "a destroyed image that held the page is named");
    Check(Has(after, "2560x1440x1"), "its words stay with it after it is destroyed");
    Check(Has(after, "in device memory 0xa"), "the block still holding the page is live");

    al::Record(B(0xB, 10, 0x20000000, 0x200000, false));
    al::Record(B(0xA, 8, 0x20000000, 0x4000000, false));
    std::vector<std::string> freed = al::Explain(0x20234000, 0x1000);
    Dump(freed);
    Check(Has(freed, "no live object holds it"), "an address only freed ranges held says the memory was freed");
    Check(Has(freed, "was device memory 0xa"), "the freed block is named");

    al::Record(B(0xC, 10, 0x30000000, 0x1000));
    al::Describe(0xC, "effects buffer 4096 bytes");
    std::vector<std::string> reused = al::Explain(0x30000010, 0);
    Dump(reused);
    Check(Has(reused, "effects buffer") && !Has(reused, "2560x1440"), "a reused handle is named by its new words");
    Check(Has(al::Explain(0x20200000, 0x1000), "2560x1440"), "the freed record of the old object keeps its words");

    std::vector<std::string> none = al::Explain(0x7000000000, 0x1000);
    Dump(none);
    Check(none.size() == 1 && Has(none, "was reported"), "an address never reported is said to be so");

    al::Binding code = B(0, 0, 0x2200000000, 0x2000000);
    code.internal = true;
    al::Record(code);
    std::vector<std::string> ip = al::Explain(0x2200e2cf50, 0x10);
    Dump(ip);
    Check(Has(ip, "driver internal"), "an internal range is marked as the driver's");

    const al::Counts c = al::Count();
    Check(c.binds == 5 && c.unbinds == 3 && c.live == 2 && c.freed == 3, "the counts follow the messages");

    std::printf("%s (%d failed)\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
