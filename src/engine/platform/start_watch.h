#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace pt::start_watch {

struct Previous {
    bool found = false;
    std::string api;
    std::string step;
    std::string time;
    unsigned long pid = 0;
    bool layers_off = false;
};

Previous TakePrevious(const std::filesystem::path& marker);

bool LoaderStep(std::string_view step);

struct Config {
    std::filesystem::path marker;
    std::string api;
    bool layers_off = false;
    std::filesystem::path log_path;
    std::function<std::string()> hang_title;
    std::function<std::string(int seconds, const std::string& step)> hang_text;
};

void Arm(const Config& config);

bool Step(std::string_view step, std::string_view detail = {});
void Done(std::string_view note = {});
void Finish();

bool TestFault(std::string_view step);

std::vector<std::string> RecentErrorReports(double hours, int limit);

}
