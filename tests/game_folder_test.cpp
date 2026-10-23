#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "engine/fs/game_folder.h"
#include "engine/fs/pt_release.h"

namespace fs = std::filesystem;
namespace gf = pt::game_folder;
namespace rel = pt::release;

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    if (ok) return;
    ++failures;
    std::printf("FAIL: %s\n", what);
}

void Write(const fs::path& file, const std::string& bytes = "x") {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << bytes;
}

void MakeGame(const fs::path& dir) {
    Write(dir / "chunk1.psarc");
    Write(dir / "texture.qar");
}

std::string ParamSfo(const std::string& title, const std::string& content_id) {
    std::vector<std::pair<std::string, std::string>> items{{"CONTENT_ID", content_id}, {"TITLE_ID", title}};
    std::string keys, values, entries;
    auto u16 = [](std::string& out, uint16_t v) { out.append(reinterpret_cast<const char*>(&v), 2); };
    auto u32 = [](std::string& out, uint32_t v) { out.append(reinterpret_cast<const char*>(&v), 4); };
    for (const auto& [key, value] : items) {
        u16(entries, static_cast<uint16_t>(keys.size()));
        u16(entries, 0x0204);
        u32(entries, static_cast<uint32_t>(value.size() + 1));
        u32(entries, 64);
        u32(entries, static_cast<uint32_t>(values.size()));
        keys += key;
        keys += '\0';
        values += value;
        values.resize(values.size() + 64 - value.size(), '\0');
    }
    std::string sfo("\0PSF\x01\x01\x00\x00", 8);
    const uint32_t key_table = static_cast<uint32_t>(0x14 + entries.size());
    const uint32_t padded = static_cast<uint32_t>((keys.size() + 3) / 4 * 4);
    u32(sfo, key_table);
    u32(sfo, key_table + padded);
    u32(sfo, static_cast<uint32_t>(items.size()));
    sfo += entries;
    sfo += keys;
    sfo.resize(key_table + padded, '\0');
    sfo += values;
    return sfo;
}

bool Same(const fs::path& a, const fs::path& b) {
    std::error_code error;
    return !a.empty() && !b.empty() && fs::equivalent(a, b, error);
}

void Names() {
    Check(rel::IsTitleId("CUSA01127") && rel::IsTitleId("CUSA01114") && !rel::IsTitleId("cusa01127") && !rel::IsTitleId("CUSA0112") && !rel::IsTitleId("CUSA011270"),
          "title id shape");
    Check(rel::TitleIdOfName("CUSA01114") == "CUSA01114" && rel::TitleIdOfName("cusa01114") == "CUSA01114" && rel::TitleIdOfName("CUSA01114-app") == "CUSA01114" &&
              rel::TitleIdOfName("CUSA01114_copy") == "CUSA01114" && rel::TitleIdOfName("CUSA011140").empty() && rel::TitleIdOfName("game").empty() &&
              rel::TitleIdOfName("My CUSA01114").empty(),
          "title id of a folder name");
    Check(std::string(rel::RegionOfContentId("UP4511-CUSA01127_00-PPPPPPPPTTTTTTTT")) == "US" &&
              std::string(rel::RegionOfContentId("EP4511-CUSA01114_00-PPPPPPPPTTTTTTTT")) == "Europe" &&
              std::string(rel::RegionOfContentId("JP0000-CUSA00000_00-0000000000000000")) == "Japan" &&
              std::string(rel::RegionOfContentId("HP0000-CUSA00000_00-0000000000000000")) == "Asia" &&
              std::string(rel::RegionOfContentId("KP0000-CUSA00000_00-0000000000000000")) == "Korea" && !rel::RegionOfContentId("XP0000-CUSA00000_00-0") &&
              !rel::RegionOfContentId("EP4511") && !rel::RegionOfContentId(""),
          "region of a content id");
    Check(rel::TitleOfContentId("EP4511-CUSA01114_00-PPPPPPPPTTTTTTTT") == "CUSA01114" && rel::TitleOfContentId("EP4511").empty(), "title of a content id");
    Check(rel::ExtractionFolderName("CUSA01127") == "CUSA01127" && rel::ExtractionFolderName("CUSA01114") == "CUSA01114" &&
              rel::ExtractionFolderName("") == "PT-game-files" && rel::ExtractionFolderName("not an id") == "PT-game-files",
          "extraction folder name");
    Check(rel::FindKnownTitle("CUSA01127") && rel::FindKnownTitle("CUSA01127")->confirmed && rel::FindKnownTitle("CUSA01114") &&
              rel::FindKnownTitle("CUSA01114")->confirmed && !rel::FindKnownTitle("CUSA99999"),
          "known titles");
}

void Picked(const fs::path& root) {
    MakeGame(root / "picked" / "US" / "CUSA01127");
    MakeGame(root / "picked" / "EU" / "CUSA01114");
    MakeGame(root / "picked" / "any" / "my P.T. dump (2014)");
    Check(Same(gf::FindGameFolder(root / "picked" / "US" / "CUSA01127"), root / "picked" / "US" / "CUSA01127"), "picked: the US folder itself");
    Check(Same(gf::FindGameFolder(root / "picked" / "EU" / "CUSA01114"), root / "picked" / "EU" / "CUSA01114"), "picked: the EU folder itself");
    Check(Same(gf::FindGameFolder(root / "picked" / "any" / "my P.T. dump (2014)"), root / "picked" / "any" / "my P.T. dump (2014)"), "picked: a folder with any name");
    Check(Same(gf::FindGameFolder(root / "picked" / "US"), root / "picked" / "US" / "CUSA01127"), "picked: the parent of the US folder");
    Check(Same(gf::FindGameFolder(root / "picked" / "EU"), root / "picked" / "EU" / "CUSA01114"), "picked: the parent of the EU folder");
    Check(Same(gf::FindGameFolder(root / "picked" / "any"), root / "picked" / "any" / "my P.T. dump (2014)"), "picked: the parent of an arbitrary name");
    MakeGame(root / "picked" / "deep" / "release" / "CUSA01114-app");
    Check(Same(gf::FindGameFolder(root / "picked" / "deep"), root / "picked" / "deep" / "release" / "CUSA01114-app"), "picked: two levels down");
    MakeGame(root / "picked" / "far" / "a" / "b" / "game");
    Check(gf::FindGameFolder(root / "picked" / "far").empty(), "picked: three levels down is not searched");
    MakeGame(root / "picked" / "both" / "CUSA01114");
    MakeGame(root / "picked" / "both" / "CUSA01127");
    MakeGame(root / "picked" / "both" / "zzz");
    Check(Same(gf::FindGameFolder(root / "picked" / "both"), root / "picked" / "both" / "CUSA01127"), "picked: US before EU before any name");
    fs::remove_all(root / "picked" / "both" / "CUSA01127");
    Check(Same(gf::FindGameFolder(root / "picked" / "both"), root / "picked" / "both" / "CUSA01114"), "picked: EU before any name");
    Write(root / "picked" / "half" / "CUSA01114" / "chunk1.psarc");
    Write(root / "picked" / "other" / "CUSA01114" / "texture.qar");
    fs::create_directories(root / "picked" / "empty" / "CUSA01114");
    Check(gf::FindGameFolder(root / "picked" / "half").empty(), "picked: chunk1.psarc alone is not the game");
    Check(gf::FindGameFolder(root / "picked" / "other").empty(), "picked: texture.qar alone is not the game");
    Check(gf::FindGameFolder(root / "picked" / "empty").empty() && gf::FindGameFolder(root / "picked" / "missing").empty() && gf::FindGameFolder({}).empty(),
          "picked: an empty or missing folder");
    MakeGame(root / "picked" / "sys" / "sce_sys");
    Check(gf::FindGameFolder(root / "picked" / "sys").empty(), "picked: sce_sys is skipped");
    fs::create_directories(root / "picked" / "dirs" / "x" / "chunk1.psarc");
    fs::create_directories(root / "picked" / "dirs" / "x" / "texture.qar");
    Check(gf::FindGameFolder(root / "picked" / "dirs").empty(), "picked: folders named like the archives");
}

void NearProgram(const fs::path& root) {
    MakeGame(root / "near" / "us" / "game" / "CUSA01127");
    gf::Found found = gf::FindNearProgram(root / "near" / "us");
    Check(Same(found.folder, root / "near" / "us" / "game" / "CUSA01127") && found.where == "next to the program", "near: game/CUSA01127");
    MakeGame(root / "near" / "us2" / "CUSA01127");
    found = gf::FindNearProgram(root / "near" / "us2");
    Check(Same(found.folder, root / "near" / "us2" / "CUSA01127") && found.where == "next to the program", "near: CUSA01127 next to the program");
    MakeGame(root / "near" / "eu" / "CUSA01114");
    found = gf::FindNearProgram(root / "near" / "eu");
    Check(Same(found.folder, root / "near" / "eu" / "CUSA01114"), "near: CUSA01114 next to the program");
    MakeGame(root / "near" / "eu2" / "game" / "CUSA01114");
    found = gf::FindNearProgram(root / "near" / "eu2");
    Check(Same(found.folder, root / "near" / "eu2" / "game" / "CUSA01114"), "near: game/CUSA01114");
    MakeGame(root / "near" / "app" / "CUSA01114-app");
    found = gf::FindNearProgram(root / "near" / "app");
    Check(Same(found.folder, root / "near" / "app" / "CUSA01114-app"), "near: CUSA01114-app next to the program");
    MakeGame(root / "near" / "any" / "PT dump");
    found = gf::FindNearProgram(root / "near" / "any");
    Check(Same(found.folder, root / "near" / "any" / "PT dump"), "near: any name next to the program");
    MakeGame(root / "near" / "any2" / "game" / "dump 2");
    found = gf::FindNearProgram(root / "near" / "any2");
    Check(Same(found.folder, root / "near" / "any2" / "game" / "dump 2"), "near: any name in game/");
    MakeGame(root / "near" / "any3" / "game");
    found = gf::FindNearProgram(root / "near" / "any3");
    Check(Same(found.folder, root / "near" / "any3" / "game"), "near: the game folder is called game");
    MakeGame(root / "near" / "up" / "CUSA01114");
    fs::create_directories(root / "near" / "up" / "a" / "b" / "c" / "d");
    found = gf::FindNearProgram(root / "near" / "up" / "a" / "b" / "c" / "d");
    Check(Same(found.folder, root / "near" / "up" / "CUSA01114") && found.where == "4 folders above the program", "near: a title ID's name four folders above");
    fs::create_directories(root / "near" / "up" / "a" / "b" / "c" / "d" / "e");
    found = gf::FindNearProgram(root / "near" / "up" / "a" / "b" / "c" / "d" / "e");
    Check(found.folder.empty(), "near: five folders above is too far");
    MakeGame(root / "near" / "upany" / "dump");
    fs::create_directories(root / "near" / "upany" / "x" / "program");
    found = gf::FindNearProgram(root / "near" / "upany" / "x" / "program");
    Check(found.folder.empty(), "near: an arbitrary name above the program is not picked up");
    MakeGame(root / "near" / "pick" / "CUSA01127");
    MakeGame(root / "near" / "pick" / "x" / "CUSA01114");
    found = gf::FindNearProgram(root / "near" / "pick" / "x");
    Check(Same(found.folder, root / "near" / "pick" / "x" / "CUSA01114") && found.where == "next to the program", "near: the nearer release wins");
    MakeGame(root / "near" / "both" / "CUSA01114");
    MakeGame(root / "near" / "both" / "CUSA01127");
    found = gf::FindNearProgram(root / "near" / "both");
    Check(Same(found.folder, root / "near" / "both" / "CUSA01127"), "near: the US name first at one level");
    Write(root / "near" / "half" / "CUSA01114" / "chunk1.psarc");
    Check(gf::FindNearProgram(root / "near" / "half").folder.empty(), "near: an incomplete folder");
    Check(gf::FindNearProgram(root / "near" / "nothing").folder.empty(), "near: a missing program folder");
    found = gf::FindNearProgram(fs::path((root / "near" / "us").native() + fs::path::preferred_separator));
    Check(Same(found.folder, root / "near" / "us" / "game" / "CUSA01127"), "near: a folder with a trailing separator");
}

void Identity(const fs::path& root, const fs::path& real_sfo_folder) {
    const fs::path eu = root / "id" / "eu";
    MakeGame(eu);
    Write(eu / "sce_sys" / "param.sfo", ParamSfo("CUSA01114", "EP4511-CUSA01114_00-PPPPPPPPTTTTTTTT"));
    rel::Identity id = rel::Identify(eu);
    Check(id.title_id == "CUSA01114" && id.content_id == "EP4511-CUSA01114_00-PPPPPPPPTTTTTTTT" && id.region == "Europe" && id.how == "sce_sys/param.sfo" &&
              id.known && id.confirmed,
          "identity: an EU param.sfo");
    MakeGame(root / "id" / "dump" / "game");
    Write(root / "id" / "dump" / "sce_sys" / "param.sfo", ParamSfo("CUSA01127", "UP4511-CUSA01127_00-PPPPPPPPTTTTTTTT"));
    id = rel::Identify(root / "id" / "dump" / "game");
    Check(id.title_id == "CUSA01127" && id.region == "US" && id.how == "sce_sys/param.sfo", "identity: the param.sfo one folder above");
    const fs::path src = root / "id" / "src";
    MakeGame(src);
    Write(src / "source.txt", "content_id=EP0101-CUSA01114_00-0000000000000000\ntitle_id=CUSA01114\nregion=Europe\nsource=pkg\nwarning=x\n");
    id = rel::Identify(src);
    Check(id.title_id == "CUSA01114" && id.region == "Europe" && id.how == "source.txt" && id.known, "identity: source.txt");
    Write(root / "id" / "old" / "source.txt", "title_id=CUSA01127\r\nregion=US\r\nsource=folder\r\n");
    id = rel::Identify(root / "id" / "old");
    Check(id.title_id == "CUSA01127" && id.region == "US" && id.content_id.empty(), "identity: an older source.txt");
    const fs::path unknown = root / "id" / "unknown";
    MakeGame(unknown);
    Write(unknown / "sce_sys" / "param.sfo", ParamSfo("CUSA09999", "HP4511-CUSA09999_00-PPPPPPPPTTTTTTTT"));
    id = rel::Identify(unknown);
    Check(id.title_id == "CUSA09999" && !id.known && id.region == "Asia", "identity: an unknown title ID with a store prefix");
    Write(unknown / "sce_sys" / "param.sfo", ParamSfo("CUSA09999", "XX0000-CUSA09999_00-0000000000000000"));
    id = rel::Identify(unknown);
    Check(id.title_id == "CUSA09999" && !id.known && id.region == "unknown", "identity: an unknown title ID without a region");
    Write(unknown / "sce_sys" / "param.sfo", ParamSfo("CUSA01098", ""));
    id = rel::Identify(unknown);
    Check(id.title_id == "CUSA01098" && id.known && !id.confirmed && id.region == "Japan", "identity: a listed title ID nobody has seen");
    MakeGame(root / "id" / "CUSA01114-app");
    id = rel::Identify(root / "id" / "CUSA01114-app");
    Check(id.title_id == "CUSA01114" && id.how == "the folder name" && id.region == "Europe", "identity: the folder's name");
    MakeGame(root / "id" / "plain");
    id = rel::Identify(root / "id" / "plain");
    Check(id.how.empty() && id.title_id.empty() && id.region.empty(), "identity: nothing says");
    Write(root / "id" / "bad" / "sce_sys" / "param.sfo", "PSF but not really");
    Write(root / "id" / "bad" / "source.txt", "no equals here\n");
    id = rel::Identify(root / "id" / "bad");
    Check(id.how.empty(), "identity: damaged files");
    if (!real_sfo_folder.empty()) {
        id = rel::Identify(real_sfo_folder);
        std::printf("real param.sfo in %s: title_id=%s content_id=%s region=%s (by %s)\n", real_sfo_folder.string().c_str(), id.title_id.c_str(),
                    id.content_id.c_str(), id.region.c_str(), id.how.c_str());
        Check(!id.title_id.empty() && !id.content_id.empty() && !id.region.empty() && id.region != "unknown", "identity: a real param.sfo");
    }
}

}

int main(int argc, char** argv) {
    const fs::path root = fs::temp_directory_path() / ("pt_game_folder_test_" + std::to_string(std::rand()) + "_" + std::to_string(static_cast<unsigned>(std::time(nullptr))));
    fs::remove_all(root);
    fs::create_directories(root);
    Names();
    Picked(root);
    NearProgram(root);
    Identity(root, argc > 1 ? fs::path(argv[1]) : fs::path());
    std::error_code error;
    fs::remove_all(root, error);
    std::printf("game folder: %d failures\n", failures);
    return failures ? 1 : 0;
}
