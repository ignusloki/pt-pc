#include "game/museum_transcripts.h"

namespace pt::game {
namespace {

constexpr const char* kNews = "tria1000_101010";
constexpr const char* kMan = "tria1000_1n1010";
constexpr MuseumLine kBroadcastAgain[] = {
    {0.0f, kNews, 0},   {4.3f, kNews, 1},   {9.7f, kNews, 2},   {15.3f, kNews, 3},  {21.4f, kMan, 0},   {24.9f, kNews, 4},
    {29.1f, kMan, 1},   {30.7f, kNews, 5},  {35.1f, kNews, 6},  {43.3f, kNews, 7},  {47.2f, kNews, 8},  {49.9f, kMan, 2},
    {53.0f, kNews, 9},  {58.8f, kNews, 10}, {61.3f, kMan, 3},   {63.2f, kMan, 4},   {66.5f, kNews, 11}, {70.1f, kNews, 12},
    {74.0f, kNews, 13}, {80.4f, kNews, 14}, {83.9f, kNews, 15}, {88.3f, kMan, 5},   {92.0f, kNews, 16},
};

constexpr const char* kRiddle = "tria1000_151010";
constexpr MuseumLine kRiddle1[] = {
    {1.0f, {}, -1, "museum_riddle_listen"},
    {5.3f, {}, -1, "museum_riddle_message"},
    {12.4f, {}, -1, "museum_riddle_sit_tight"},
    {15.4f, kRiddle, 0},
    {17.0f, kRiddle, 1},
    {18.8f, kRiddle, 2},
};

constexpr MuseumLine kPeephole[] = {
    {0.0f, {}, -1, "museum_peephole_message"},
    {6.87f, kRiddle, 0},
    {8.83f, kRiddle, 1},
    {11.34f, kRiddle, 2},
    {21.13f, kRiddle, 3},
    {23.21f, kRiddle, 4},
};

constexpr MuseumLine kSermon[] = {
    {0.0f, {}, -1, "museum_sermon_01", "Slut dina ögon."},
    {9.6f, {}, -1, "museum_sermon_02", "Låt dina öron lyssna till radion."},
    {13.5f, {}, -1, "museum_sermon_03", "Hör du min röst?"},
    {15.0f, {}, -1, "museum_sermon_04", "Kan du höra din egen själs skrik?"},
    {18.9f, {}, -1, "museum_sermon_05", "Låt oss välja: min röst som förtäljer framtiden,"},
    {23.7f, {}, -1, "museum_sermon_06", "eller dina plågade tankar?"},
    {27.5f, {}, -1, "museum_sermon_07", "Nå? Vad väljer du?"},
    {31.0f, {}, -1, "museum_sermon_08", "Du kan välja."},
    {32.9f, {}, -1, "museum_sermon_09", "Ditt liv. Din framtid!"},
    {37.2f, {}, -1, "museum_sermon_10", "Klok som du är, så kanske du redan upptäckt det."},
    {41.3f, {}, -1, "museum_sermon_11", "Ja, radiodramat från 75 år sedan var sanning."},
    {47.4f, {}, -1, "museum_sermon_12", "De är här, på vår jord."},
    {51.2f, {}, -1, "museum_sermon_13", "Och de övervakar och ser allt."},
    {54.9f, {}, -1, "museum_sermon_14", "Lita ej på nyheterna."},
    {57.3f, {}, -1, "museum_sermon_15", "Lita ej på polisen."},
    {60.0f, {}, -1, "museum_sermon_16", "De är redan kontrollerade av dem."},
    {63.4f, {}, -1, "museum_sermon_17", "Så har det varit i 75 år nu."},
    {67.0f, {}, -1, "museum_sermon_18", "Endast vår mästare kan hålla dem."},
    {70.4f, {}, -1, "museum_sermon_19", "Du har en rätt. En rätt att bli en av oss."},
    {74.9f, {}, -1, "museum_sermon_20", "Så välkommen till vår värld."},
    {78.6f, {}, -1, "museum_sermon_21", "Mycket snart öppnas portarna till en ny dimension."},
    {82.0f, {}, -1, {}, "204863"},
    {85.6f, {}, -1, {}, "204863"},
};

}

std::span<const MuseumLine> MuseumTranscript(std::string_view entry_id) {
    if (entry_id == "radio_news2") return kBroadcastAgain;
    if (entry_id == "riddle_1") return kRiddle1;
    if (entry_id == "peephole_voice") return kPeephole;
    if (entry_id == "sermon") return kSermon;
    return {};
}

}
