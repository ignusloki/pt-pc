#pragma once
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace pt::first_run {

struct Context {
    std::filesystem::path program_dir;
    std::filesystem::path user_dir;
    std::filesystem::path remembered_file;
    std::vector<std::filesystem::path> helper_candidates;
    bool program_dir_read_only = false;
    bool interactive = true;
    int language = 0;
    std::function<int(const std::filesystem::path&)> missing_packages;
};

struct Result {
    std::filesystem::path game_dir;
    std::string source;
};

Result Run(const Context& context);

bool LooksLikeGameDir(const std::filesystem::path& dir);
std::filesystem::path FindGameFolder(const std::filesystem::path& picked);
int PlayerLanguage(const std::filesystem::path& user_dir);
std::string Text(std::string_view key, int language, std::initializer_list<std::string> args = {});

}
