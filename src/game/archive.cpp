#include "game/archive.h"

#include <array>
#include <format>
#include <unordered_set>

#include "game/ui/pc_settings.h"

namespace pt::game {
namespace {

using S = ArchiveSection;
using M = ArchiveMedia;

constexpr std::string_view kNoteImage = "pc_note_archive_image";
constexpr std::string_view kNoteVoice = "pc_note_archive_voice";
constexpr std::string_view kNotePhoto = "pc_note_archive_photo";
constexpr std::string_view kNoteDemo = "pc_note_archive_demo";
constexpr std::string_view kNoteModel = "pc_note_archive_model";
constexpr std::string_view kNoteUnused = "pc_note_archive_unused";
constexpr std::string_view kNoteFree = "pc_note_archive_free";
constexpr std::string_view kNoteMusic = "pc_note_archive_music";
constexpr std::string_view kNoteMusicUnused = "pc_note_archive_music_unused";
constexpr std::string_view kHintPlays = "pc_note_archive_hint_plays_loop";
constexpr std::string_view kHintLook = "pc_note_archive_hint_plays_look";
constexpr std::string_view kHintZoom = "pc_note_archive_hint_plays_zoom";
constexpr std::string_view kHintLeave = "pc_note_archive_hint_plays_leave";
constexpr std::string_view kHintLobby = "pc_note_archive_hint_lobby";
constexpr std::string_view kHintNewGame = "pc_note_archive_hint_new_game";
constexpr std::string_view kHintWake = "pc_note_archive_hint_wake";
constexpr std::string_view kHintBoot = "pc_note_archive_hint_boot";
constexpr std::string_view kHintFirstDoor = "pc_note_archive_hint_first_door";
constexpr std::string_view kHintEveryDoor = "pc_note_archive_hint_every_door";
constexpr std::string_view kHintLoopStart = "pc_note_archive_hint_loop_start";
constexpr std::string_view kHintCaught = "pc_note_archive_hint_caught";
constexpr std::string_view kHintFinish = "pc_note_archive_hint_finish";
constexpr std::string_view kHintAfterFinish = "pc_note_archive_hint_after_finish";
constexpr std::string_view kHintUnused = "pc_note_archive_hint_unused";
constexpr std::string_view kHintPhotoFlash = "pc_note_archive_hint_photo_flash";
constexpr std::string_view kHintRandomFinal = "pc_note_archive_hint_random_final";
constexpr std::string_view kHintFlash = "pc_note_archive_hint_flash";
constexpr std::string_view kHintPeephole = "pc_note_archive_hint_peephole";
constexpr std::string_view kHintCrash = "pc_note_archive_hint_crash";
constexpr std::string_view kHintRadio = "pc_note_archive_hint_radio";
constexpr std::string_view kHintRadioWait = "pc_note_archive_hint_radio_wait";
constexpr std::string_view kHintPhone = "pc_note_archive_hint_phone";
constexpr std::string_view kHintBag = "pc_note_archive_hint_bag";
constexpr std::string_view kHintMaze = "pc_note_archive_hint_maze";
constexpr std::string_view kHintPiece = "pc_note_archive_hint_photo_piece";
constexpr std::string_view kHintOption = "pc_note_archive_hint_photo_option";
constexpr std::string_view kHintComplete = "pc_note_archive_hint_photo_complete";
constexpr std::string_view kHintAppears = "pc_note_archive_hint_appears";
constexpr std::string_view kHintStartRoom = "pc_note_archive_hint_start_room";
constexpr std::string_view kHintFromLoop = "pc_note_archive_hint_from_loop";
constexpr std::string_view kHintHerald = "pc_note_archive_hint_herald";
constexpr std::string_view kHintFlashlight = "pc_note_archive_hint_flashlight";

constexpr ArchiveEntry kEntries[] = {
    {.id = "sub01", .section = S::Images, .media = M::String, .label = "pc_archive_sub_it", .number = 1,
     .asset = "/Assets/sh/effect/vfx_pic/text/text_sub01_alp.ftex", .unlock = "sub:1", .note = kNoteImage, .hint = kHintPhotoFlash},
    {.id = "sub02", .section = S::Images, .media = M::String, .label = "pc_archive_sub_it", .number = 2,
     .asset = "/Assets/sh/effect/vfx_pic/text/text_sub02_alp.ftex", .unlock = "sub:2", .note = kNoteImage, .hint = kHintPhotoFlash},
    {.id = "sub03", .section = S::Images, .media = M::String, .label = "pc_archive_sub_de", .number = 1,
     .asset = "/Assets/sh/effect/vfx_pic/text/text_sub03_alp.ftex", .unlock = "sub:3", .note = kNoteImage, .hint = kHintPhotoFlash},
    {.id = "sub04", .section = S::Images, .media = M::String, .label = "pc_archive_sub_de", .number = 2,
     .asset = "/Assets/sh/effect/vfx_pic/text/text_sub04_alp.ftex", .unlock = "sub:4", .note = kNoteImage, .hint = kHintPhotoFlash},
    {.id = "sub05", .section = S::Images, .media = M::String, .label = "pc_archive_sub_pt", .number = 1,
     .asset = "/Assets/sh/effect/vfx_pic/text/text_sub05_alp.ftex", .unlock = "sub:5", .note = kNoteImage, .hint = kHintRandomFinal},
    {.id = "sub06", .section = S::Images, .media = M::String, .label = "pc_archive_sub_pt", .number = 2,
     .asset = "/Assets/sh/effect/vfx_pic/text/text_sub06_alp.ftex", .unlock = "sub:6", .note = kNoteImage, .hint = kHintRandomFinal},
    {.id = "sub07", .section = S::Images, .media = M::String, .label = "pc_archive_sub_ja", .number = 1,
     .asset = "/Assets/sh/effect/vfx_pic/text/text_sub07_alp.ftex", .unlock = "sub:7", .note = kNoteImage, .hint = kHintRandomFinal},
    {.id = "sub08", .section = S::Images, .media = M::String, .label = "pc_archive_sub_ja", .number = 2,
     .asset = "/Assets/sh/effect/vfx_pic/text/text_sub08_alp.ftex", .unlock = "sub:8", .note = kNoteImage, .hint = kHintRandomFinal},
    {.id = "sub09", .section = S::Images, .media = M::String, .label = "pc_archive_sub_en", .number = 1,
     .asset = "/Assets/sh/effect/vfx_pic/text/text_sub09_alp.ftex", .unlock = "sub:9", .note = kNoteImage, .hint = kHintRandomFinal},
    {.id = "sub10", .section = S::Images, .media = M::String, .label = "pc_archive_sub_en", .number = 2,
     .asset = "/Assets/sh/effect/vfx_pic/text/text_sub10_alp.ftex", .unlock = "sub:10", .note = kNoteImage, .hint = kHintRandomFinal},
    {.id = "noise", .section = S::Images, .media = M::Image, .label = "pc_archive_noise",
     .asset = "/Assets/sh/effect/vfx_pic/noise/Noise_00007.ftex", .unlock = "noise", .note = kNoteImage, .hint = kHintFlash},
    {.id = "noise_normal", .section = S::Images, .media = M::Image, .label = "pc_archive_noise_normal",
     .asset = "/Assets/sh/effect/vfx_pic/noise/Noise_00007_nrm.ftex", .unlock = "noise", .note = kNoteImage, .hint = kHintFlash},
    {.id = "peephole_mask", .section = S::Images, .media = M::Image, .label = "pc_archive_peephole_mask",
     .asset = "/Assets/sh/effect/vfx_pic/holl/holl_002_alp.ftex", .unlock = "overlay", .note = kNoteImage, .hint = kHintPeephole, .hint_loop = 13},

    {.id = "crash_1", .section = S::Crash, .media = M::Screen, .label = "pc_archive_crash_halted", .asset = "/Assets/sh/ui/ModelAsset/sys_bug/Pictures/sh_bug_1.ftex",
     .unlock = "bug:sh_bug_1", .note = kNoteImage, .hint = kHintCrash, .hint_loop = 14},
    {.id = "crash_2", .section = S::Crash, .media = M::Screen, .label = "pc_archive_crash_fix", .asset = "/Assets/sh/ui/ModelAsset/sys_bug/Pictures/sh_bug_2.ftex",
     .unlock = "bug:sh_bug_2", .note = kNoteImage, .hint = kHintCrash, .hint_loop = 14},
    {.id = "crash_3", .section = S::Crash, .media = M::Screen, .label = "pc_archive_crash_letter", .asset = "/Assets/sh/ui/ModelAsset/sys_bug/Pictures/sh_bug_4.ftex",
     .unlock = "bug:sh_bug_4", .note = kNoteImage, .hint = kHintCrash, .hint_loop = 14},
    {.id = "crash_4", .section = S::Crash, .media = M::Screen, .label = "pc_archive_crash_heading", .asset = "/Assets/sh/ui/ModelAsset/sys_bug/Pictures/sh_bug_5.ftex",
     .unlock = "bug:sh_bug_5", .note = kNoteImage, .hint = kHintCrash, .hint_loop = 14},
    {.id = "crash_5", .section = S::Crash, .media = M::Screen, .label = "pc_archive_crash_call", .asset = "/Assets/sh/ui/ModelAsset/sys_bug/Pictures/sh_bug_6.ftex",
     .unlock = "bug:sh_bug_6", .note = kNoteImage, .hint = kHintCrash, .hint_loop = 14},
    {.id = "crash_6", .section = S::Crash, .media = M::Screen, .label = "pc_archive_crash_fiction", .asset = "/Assets/sh/ui/ModelAsset/sys_bug/Pictures/sh_bug_7.ftex",
     .unlock = "bug:sh_bug_7", .note = kNoteImage, .hint = kHintCrash, .hint_loop = 14},
    {.id = "crash_7", .section = S::Crash, .media = M::Screen, .label = "pc_archive_crash_free", .asset = "/Assets/sh/ui/ModelAsset/sys_bug/Pictures/sh_bug_3.ftex",
     .unlock = "bug:sh_bug_3", .note = kNoteUnused, .hint = kHintUnused, .unused = true},

    {.id = "radio_news", .section = S::Radio, .media = M::Sound, .label = "pc_archive_radio_news", .asset = "Play_radio_f010",
     .extra = "tria1000_101010", .unlock = "voice:tria1000_101010", .note = kNoteVoice, .hint = kHintRadio, .hint_loop = 1},
    {.id = "radio_news2", .section = S::Radio, .media = M::Sound, .label = "pc_archive_radio_news2", .asset = "Play_radio_f050",
     .extra = "tria1000_1n1010", .unlock = "voice:tria1000_1n1010", .note = kNoteVoice, .hint = kHintRadio, .hint_loop = 7},
    {.id = "radio_04", .section = S::Radio, .media = M::Sound, .label = "pc_archive_radio_hose", .asset = "Play_radio_voice_04",
     .extra = "tria1000_1b1010", .unlock = "voice:tria1000_1b1010", .note = kNoteVoice, .hint = kHintRadio, .hint_loop = 10},
    {.id = "sermon", .section = S::Radio, .media = M::Sound, .label = "pc_archive_sermon", .asset = "Play_radio_religion_01",
     .unlock = "event:Play_radio_religion_01", .note = kNoteVoice, .hint = kHintRadio, .hint_loop = 12},
    {.id = "peephole_voice", .section = S::Radio, .media = M::Sound, .label = "pc_archive_peephole_voice", .asset = "Play_Peephole_Theater",
     .extra = "tria1000_151010", .unlock = "event:Play_Peephole_Theater", .note = kNoteVoice, .hint = kHintPeephole, .hint_loop = 13},
    {.id = "radio_06", .section = S::Radio, .media = M::Sound, .label = "pc_archive_radio_son", .asset = "Play_radio_voice_06",
     .extra = "tria1000_1d1010", .unlock = "voice:tria1000_1d1010", .note = kNoteVoice, .hint = kHintRadioWait, .hint_loop = 7, .hint_loop2 = 8},
    {.id = "radio_07", .section = S::Radio, .media = M::Sound, .label = "pc_archive_radio_heaven", .asset = "Play_radio_voice_07",
     .extra = "tria1000_1e1010", .unlock = "voice:tria1000_1e1010", .note = kNoteVoice, .hint = kHintRadioWait, .hint_loop = 7, .hint_loop2 = 8},
    {.id = "radio_08", .section = S::Radio, .media = M::Sound, .label = "pc_archive_radio_murderer", .asset = "Play_radio_voice_08",
     .extra = "tria1000_1f1010", .unlock = "voice:tria1000_1f1010", .note = kNoteVoice, .hint = kHintRadioWait, .hint_loop = 7, .hint_loop2 = 8},
    {.id = "radio_01", .section = S::Radio, .media = M::Sound, .label = "pc_archive_radio_dial", .asset = "Play_radio_voice_01",
     .extra = "tria1000_181010", .unlock = "voice:tria1000_181010", .note = kNoteVoice, .hint = kHintUnused, .unused = true},
    {.id = "radio_02", .section = S::Radio, .media = M::Sound, .label = "pc_archive_radio_water", .asset = "Play_radio_voice_02",
     .extra = "tria1000_191010", .unlock = "voice:tria1000_191010", .note = kNoteVoice, .hint = kHintUnused, .unused = true},
    {.id = "radio_03", .section = S::Radio, .media = M::Sound, .label = "pc_archive_radio_behind", .asset = "Play_radio_voice_03",
     .extra = "tria1000_1a1010", .unlock = "voice:tria1000_1a1010", .note = kNoteVoice, .hint = kHintUnused, .unused = true},
    {.id = "radio_05", .section = S::Radio, .media = M::Sound, .label = "pc_archive_radio_204863", .asset = "Play_radio_voice_05",
     .extra = "tria1000_1c1010", .unlock = "voice:tria1000_1c1010", .note = kNoteVoice, .hint = kHintUnused, .unused = true},
    {.id = "riddle_1", .section = S::Radio, .media = M::Sound, .label = "pc_archive_riddle", .number = 1, .asset = "Play_radio_riddle_01",
     .extra = "tria1000_151010", .unlock = "event:Play_radio_riddle_01", .note = kNoteVoice, .hint = kHintUnused, .unused = true},
    {.id = "riddle_2", .section = S::Radio, .media = M::Sound, .label = "pc_archive_riddle", .number = 2, .asset = "Play_radio_riddle_02",
     .extra = "tria1000_161010", .unlock = "event:Play_radio_riddle_02", .note = kNoteVoice, .hint = kHintUnused, .unused = true},

    {.id = "bag", .section = S::Voices, .media = M::Dialogue, .label = "pc_archive_bag", .asset = "pab", .extra = "TRIA1000_111010_0_mib",
     .unlock = "voice:tria1000_111010", .note = kNoteVoice, .hint = kHintBag},
    {.id = "fetus_2", .section = S::Voices, .media = M::Sound, .label = "pc_archive_fetus_cord", .asset = "Play_voice_baby_mimicry_02",
     .extra = "tria1000_1g1010", .unlock = "voice:tria1000_1g1010", .note = kNoteVoice, .hint = kHintPlays, .hint_loop = 10},
    {.id = "fetus_1", .section = S::Voices, .media = M::Sound, .label = "pc_archive_fetus_remember", .asset = "Play_voice_baby_mimicry_01",
     .extra = "tria1000_121010", .unlock = "voice:tria1000_121010", .note = kNoteVoice, .hint = kHintMaze, .hint_loop = 13},
    {.id = "phone", .section = S::Voices, .media = M::Sound, .label = "pc_archive_phone", .asset = "Play_tel_voice_01",
     .extra = "tria1000_171010", .unlock = "voice:tria1000_171010", .note = kNoteVoice, .hint = kHintPhone},

    {.id = "photo_lisa", .section = S::Photos, .media = M::Photo, .label = "pc_archive_photo_lisa",
     .asset = "/Assets/sh/environ/object/shsb/label/shsb_labl001/scenes/shsb_labl001_mapc004.fmdl", .unlock = "photo:PhotoLisa", .note = kNotePhoto,
     .hint = kHintPiece, .hint_loop = 3},
    {.id = "photo_tree", .section = S::Photos, .media = M::Photo, .label = "pc_archive_photo_tree",
     .asset = "/Assets/sh/environ/object/shsb/label/shsb_labl001/scenes/shsb_labl001_mapc005.fmdl", .unlock = "photo:PhotoTree", .note = kNotePhoto,
     .hint = kHintPiece, .hint_loop = 3},
    {.id = "photo_gap", .section = S::Photos, .media = M::Photo, .label = "pc_archive_photo_gap",
     .asset = "/Assets/sh/environ/object/shsb/label/shsb_labl001/scenes/shsb_labl001_mapc008.fmdl", .unlock = "photo:PhotoGap", .note = kNotePhoto,
     .hint = kHintPiece, .hint_loop = 3},
    {.id = "photo_bath", .section = S::Photos, .media = M::Photo, .label = "pc_archive_photo_bath",
     .asset = "/Assets/sh/environ/object/shsb/label/shsb_labl001/scenes/shsb_labl001_mapc003.fmdl", .unlock = "photo:PhotoBath", .note = kNotePhoto,
     .hint = kHintPiece, .hint_loop = 3},
    {.id = "photo_stair", .section = S::Photos, .media = M::Photo, .label = "pc_archive_photo_stair",
     .asset = "/Assets/sh/environ/object/shsb/label/shsb_labl001/scenes/shsb_labl001_mapc006.fmdl", .unlock = "photo:PhotoStair", .note = kNotePhoto,
     .hint = kHintPiece, .hint_loop = 3},
    {.id = "photo_option", .section = S::Photos, .media = M::Photo, .label = "pc_archive_photo_option",
     .asset = "/Assets/sh/environ/object/shsb/label/shsb_labl001/scenes/shsb_labl001_mapc007.fmdl", .unlock = "photo:PhotoOption", .note = kNotePhoto,
     .hint = kHintOption},
    {.id = "photo_complete", .section = S::Photos, .media = M::Photo, .label = "pc_archive_photo_complete", .unlock = "photo:complete",
     .note = kNotePhoto, .hint = kHintComplete},

    {.id = "preface", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_preface", .asset = "gc_p02_500", .extra = "preface",
     .floor = "f000", .note = kNoteDemo, .hint = kHintBoot},
    {.id = "awakening", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_awakening", .asset = "gc_p00_022", .extra = "opening",
     .floor = "f000", .note = kNoteDemo, .hint = kHintNewGame},
    {.id = "opening", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_opening", .asset = "gc_p00_020", .extra = "opening",
     .floor = "f010", .note = kNoteDemo, .hint = kHintWake},
    {.id = "first_exit", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_first_exit", .asset = "gc_p00_160", .extra = "room",
     .floor = "f000", .note = kNoteDemo, .hint = kHintFirstDoor},
    {.id = "door", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_door", .asset = "gc_p00_010", .extra = "room",
     .floor = "f010", .note = kNoteDemo, .hint = kHintEveryDoor},
    {.id = "clock", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_clock", .asset = "gc_p05_010", .floor = "f010", .note = kNoteDemo,
     .hint = kHintLoopStart},
    {.id = "gc_p04_310", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_roaches", .number = 2, .asset = "gc_p04_310", .floor = "f005",
     .note = kNoteDemo, .hint = kHintPlays, .hint_loop = 2},
    {.id = "gc_p01_070", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_bathroom_rattle", .asset = "gc_p01_070", .floor = "f005",
     .note = kNoteDemo, .hint = kHintPlays, .hint_loop = 2},
    {.id = "gc_p04_340", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_roaches", .number = 3, .asset = "gc_p04_340", .floor = "f020",
     .note = kNoteDemo, .hint = kHintPlays, .hint_loop = 3},
    {.id = "gc_p01_081", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_bathroom_bang", .asset = "gc_p01_081", .floor = "f020",
     .note = kNoteDemo, .hint = kHintPlays, .hint_loop = 3},
    {.id = "gc_p01_011", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_lobby_slam", .asset = "gc_p01_011", .floor = "f030",
     .note = kNoteDemo, .hint = kHintPlays, .hint_loop = 4},
    {.id = "gc_p01_020", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_bathroom_crack", .asset = "gc_p01_020", .floor = "f030",
     .note = kNoteDemo, .hint = kHintPlays, .hint_loop = 4},
    {.id = "gc_p03_010", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_roaches", .number = 4, .asset = "gc_p03_010", .floor = "f030",
     .note = kNoteDemo, .hint = kHintPlays, .hint_loop = 4},
    {.id = "gc_p01_021", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_lisa_slam", .asset = "gc_p01_021", .floor = "f030",
     .note = kNoteDemo, .hint = kHintZoom, .hint_loop = 4},
    {.id = "gc_p01_010", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_lobby_open", .asset = "gc_p01_010", .floor = "f030",
     .note = kNoteDemo, .hint = kHintLobby, .hint_loop = 4, .hint_loop2 = 8},
    {.id = "gc_p04_300", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_roaches", .number = 5, .asset = "gc_p04_300", .floor = "f040",
     .note = kNoteDemo, .hint = kHintPlays, .hint_loop = 5},
    {.id = "gc_p04_350", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_lights_out", .asset = "gc_p04_350", .floor = "f040",
     .note = kNoteDemo, .hint = kHintPlays, .hint_loop = 5},
    {.id = "gc_p03_070", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_lights", .asset = "gc_p03_070", .floor = "f060",
     .note = kNoteDemo, .hint = kHintPlays, .hint_loop = 6},
    {.id = "gc_p03_011", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_bathroom_roaches", .asset = "gc_p03_011", .floor = "f060",
     .note = kNoteDemo, .hint = kHintPlays, .hint_loop = 6},
    {.id = "gc_p01_110", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_bathroom_wide", .asset = "gc_p01_110", .floor = "f060",
     .note = kNoteDemo, .hint = kHintPlays, .hint_loop = 6},
    {.id = "gc_p00_030", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_flashlight", .asset = "gc_p00_030", .floor = "f060",
     .note = kNoteDemo, .hint = kHintLook, .hint_loop = 6},
    {.id = "gc_p01_050", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_bathroom_shut", .asset = "gc_p01_050", .extra = "lit", .floor = "f060",
     .note = kNoteDemo, .hint = kHintLook, .hint_loop = 6},
    {.id = "gc_p01_090", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_sink", .asset = "gc_p01_090", .extra = "lit", .floor = "f060",
     .note = kNoteDemo, .hint = kHintLook, .hint_loop = 6},
    {.id = "gc_p01_022", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_bathroom_leave", .asset = "gc_p01_022", .extra = "lit", .floor = "f060",
     .note = kNoteDemo, .hint = kHintLeave, .hint_loop = 6},
    {.id = "gc_p04_320", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_roaches", .number = 7, .asset = "gc_p04_320", .floor = "f050",
     .note = kNoteDemo, .hint = kHintPlays, .hint_loop = 7},
    {.id = "gc_p04_120", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_window", .asset = "gc_p04_120", .floor = "f070",
     .note = kNoteDemo, .hint = kHintPlays, .hint_loop = 9},
    {.id = "gc_p04_280", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_footsteps", .asset = "gc_p04_280", .floor = "f070",
     .note = kNoteDemo, .hint = kHintPlays, .hint_loop = 9},
    {.id = "gc_p02_520", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_radio_fridge", .asset = "gc_p02_520", .floor = "f080",
     .note = kNoteDemo, .hint = kHintPlays, .hint_loop = 10},
    {.id = "gc_p02_090", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_maze", .asset = "gc_p02_090", .floor = "f110",
     .note = kNoteDemo, .hint = kHintPlays, .hint_loop = 13},
    {.id = "gc_p02_080", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_cs_crash", .asset = "gc_p02_080", .floor = "f120",
     .note = kNoteDemo, .hint = kHintPlays, .hint_loop = 14},
    {.id = "kill", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_kill", .asset = "gc_p07_030", .extra = "kill", .floor = "f040",
     .note = kNoteDemo, .hint = kHintCaught},
    {.id = "ending", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_ending", .asset = "gc_p06_010_final", .extra = "ending",
     .floor = "f160", .note = kNoteDemo, .hint = kHintFinish},
    {.id = "teaser", .section = S::Cutscenes, .media = M::Demo, .label = "pc_archive_teaser", .asset = "gc_p06_010_final", .extra = "teaser",
     .floor = "f160", .unlock = "finished", .note = kNoteDemo, .hint = kHintAfterFinish},

    {.id = "lisa_stand", .section = S::Models, .media = M::Model, .label = "pc_archive_lisa_stand", .asset = "Ocho", .extra = "OchoStop",
     .floor = "f010", .unlock = "gimmick:Ocho", .note = kNoteModel, .hint = kHintAppears, .hint_loop = 7},
    {.id = "lisa_walk", .section = S::Models, .media = M::Model, .label = "pc_archive_lisa_walk", .asset = "Ocho", .extra = "Ocho",
     .floor = "f010", .unlock = "gimmick:Ocho", .note = kNoteModel, .hint = kHintAppears, .hint_loop = 7},
    {.id = "lisa_dash", .section = S::Models, .media = M::Model, .label = "pc_archive_lisa_dash", .asset = "Ocho", .extra = "OchoDash",
     .floor = "f010", .unlock = "gimmick:Ocho", .note = kNoteModel, .hint = kHintAppears, .hint_loop = 7},
    {.id = "lisa_door", .section = S::Models, .media = M::Model, .label = "pc_archive_lisa_door",
     .asset = "/Assets/sh/environ/object/shsb/house/shsb_hous001/scenes/shsb_hous001_ocho001.fmdl", .floor = "f010", .unlock = "demo:gc_p00_022",
     .note = kNoteModel, .hint = kHintNewGame},
    {.id = "lisa_bathroom_walk", .section = S::Models, .media = M::Demo, .label = "pc_archive_lisa_bathroom_walk", .asset = "gc_p01_090", .extra = "lit",
     .floor = "f060", .note = kNoteFree, .hint = kHintLook, .hint_loop = 6, .free_camera = true, .free_offset = {10.5f, 0.4f, 0.0f}},
    {.id = "lisa_backwards", .section = S::Models, .media = M::Demo, .label = "pc_archive_lisa_backwards", .asset = "gc_p04_120", .floor = "f070",
     .note = kNoteFree, .hint = kHintPlays, .hint_loop = 9, .free_camera = true, .free_offset = {3.0f, -0.6f, 0.0f}},
    {.id = "lisa_catch", .section = S::Models, .media = M::Demo, .label = "pc_archive_lisa_catch", .asset = "gc_p07_030", .extra = "kill", .floor = "f040",
     .note = kNoteFree, .hint = kHintCaught, .free_camera = true, .free_offset = {2.2f, 0.3f, -1.2f}},
    {.id = "baby", .section = S::Models, .media = M::Model, .label = "pc_archive_baby", .asset = "Baby", .extra = "Baby", .floor = "f010",
     .unlock = "gimmick:Baby", .note = kNoteModel, .hint = kHintAppears, .hint_loop = 1},
    {.id = "fridge", .section = S::Models, .media = M::Model, .label = "pc_archive_fridge", .asset = "Freezer", .extra = "Freezer", .floor = "f010",
     .unlock = "gimmick:Freezer", .note = kNoteModel, .hint = kHintAppears, .hint_loop = 10},
    {.id = "bag_model", .section = S::Models, .media = M::Model, .label = "pc_archive_bag_model", .asset = "Bag", .extra = "BagTalk", .floor = "f010",
     .unlock = "gimmick:Bag", .note = kNoteModel, .hint = kHintStartRoom},
    {.id = "lamp", .section = S::Models, .media = M::Model, .label = "pc_archive_lamp", .asset = "CeilLamp", .extra = "CeilLamp", .floor = "f010",
     .unlock = "gimmick:CeilLamp", .note = kNoteModel, .hint = kHintAppears, .hint_loop = 1},
    {.id = "roach", .section = S::Models, .media = M::Model, .label = "pc_archive_roach", .asset = "/Assets/sh/chara/coc/Scenes/coc0_main0_def.fmdl",
     .floor = "f010", .unlock = "demo:gc_p02_510", .note = kNoteModel, .hint = kHintNewGame},
    {.id = "flashlight", .section = S::Models, .media = M::Model, .label = "pc_archive_flashlight",
     .asset = "/Assets/sh/item/lig/Scenes/shl0_main0_def.fmdl", .floor = "f010", .unlock = "demo:gc_p00_030", .note = kNoteModel, .hint = kHintFlashlight,
     .hint_loop = 6},
    {.id = "radio", .section = S::Models, .media = M::Model, .label = "pc_archive_radio",
     .asset = "/Assets/sh/environ/object/shsb/radio/shsb_radi001/scenes/shsb_radi001.fmdl", .floor = "f010", .unlock = "floor:f010",
     .note = kNoteModel, .hint = kHintAppears, .hint_loop = 1},
    {.id = "phone_model", .section = S::Models, .media = M::Model, .label = "pc_archive_phone_model",
     .asset = "/Assets/sh/environ/object/shsb/telephone/shsb_tlph001/scenes/shsb_tlph001.fmdl", .floor = "f010", .unlock = "floor:f160",
     .note = kNoteModel, .hint = kHintAppears, .hint_loop = 15},
    {.id = "xmark_photo", .section = S::Models, .media = M::Model, .label = "pc_archive_xmark_photo",
     .asset = "/Assets/sh/environ/object/shsb/house/shsb_hous001/scenes/shsb_hous001_pc1a.fmdl", .floor = "f010", .unlock = "floor:f050",
     .note = kNoteModel, .hint = kHintAppears, .hint_loop = 7},
    {.id = "clock_model", .section = S::Models, .media = M::Model, .label = "pc_archive_clock_model",
     .asset = "/Assets/sh/environ/object/shsb/clock/shsb_clck001/scenes/shsb_clck001.fmdl", .floor = "f010", .unlock = "floor:f005",
     .note = kNoteModel, .hint = kHintAppears, .hint_loop = 2},
    {.id = "man", .section = S::Models, .media = M::Model, .label = "pc_archive_man", .asset = "/Assets/sh/chara/plr/Scenes/plr0_main0_def.fmdl",
     .floor = "f010", .unlock = "demo:gc_p06_010_final", .note = kNoteModel, .hint = kHintFinish},

    {.id = "music_startroom", .section = S::Music, .media = M::Music, .label = "pc_archive_music_startroom", .asset = "media:584815259",
     .extra = "Play_bg_startroom01", .unlock = "event:Play_bg_startroom01", .note = kNoteMusic, .hint = kHintStartRoom},
    {.id = "music_hallway", .section = S::Music, .media = M::Music, .label = "pc_archive_music_hallway", .asset = "media:174571516",
     .extra = "Play_bg_passage01", .unlock = "event:Play_bg_passage01", .note = kNoteMusic, .hint = kHintFromLoop, .hint_loop = 1},
    {.id = "music_unrest03", .section = S::Music, .media = M::Music, .label = "pc_archive_music_unrest03", .asset = "media:264280594",
     .extra = "Play_bgm_unrest03", .unlock = "event:Play_bgm_unrest03", .note = kNoteMusic, .hint = kHintPlays, .hint_loop = 4},
    {.id = "music_herald", .section = S::Music, .media = M::Music, .label = "pc_archive_music_herald", .asset = "media:364685121",
     .extra = "Play_bgm_herald01", .unlock = "event:Play_bgm_herald01", .note = kNoteMusic, .hint = kHintHerald, .hint_loop = 6},
    {.id = "music_unrest02", .section = S::Music, .media = M::Music, .label = "pc_archive_music_unrest02", .asset = "media:432599416",
     .extra = "Play_bgm_unrest02", .unlock = "event:Play_bgm_unrest02", .note = kNoteMusic, .hint = kHintPlays, .hint_loop = 10},
    {.id = "music_f110", .section = S::Music, .media = M::Music, .label = "pc_archive_music_f110", .asset = "media:112489217",
     .extra = "Play_bgm_corridor_f110", .unlock = "event:Play_bgm_corridor_f110", .note = kNoteMusic, .hint = kHintPlays, .hint_loop = 13,
     .seconds = 292.5f, .loop_from = 42.5f},
    {.id = "music_ending", .section = S::Music, .media = M::Music, .label = "pc_archive_music_ending",
     .asset = "stream:/Assets/sh/demo/demo_stream/gc_p06_010_final.fsm", .extra = "gc_p06_010_final", .unlock = "demo:gc_p06_010_final",
     .note = kNoteMusic, .hint = kHintFinish},
    {.id = "music_unrest01", .section = S::Music, .media = M::Music, .label = "pc_archive_music_unused", .number = 1, .asset = "media:812266907",
     .extra = "Play_bgm_unrest01", .unlock = "finished", .note = kNoteMusicUnused, .hint = kHintUnused},
    {.id = "music_mystery01", .section = S::Music, .media = M::Music, .label = "pc_archive_music_unused", .number = 2, .asset = "media:513779340",
     .extra = "Play_bgm_mystery01", .unlock = "finished", .note = kNoteMusicUnused, .hint = kHintUnused},
    {.id = "music_1a95368d", .section = S::Music, .media = M::Music, .label = "pc_archive_music_unused", .number = 3, .asset = "media:356893170",
     .extra = "event 1A95368D", .unlock = "finished", .note = kNoteMusicUnused, .hint = kHintUnused},

    {.id = "bathtub_lisa", .section = S::Unused, .media = M::Model, .label = "pc_archive_bathtub",
     .asset = "/Assets/sh/environ/object/shsb/bath/shsb_bath001/scenes/shsb_bath001_ocho001.fmdl", .floor = "f010", .unlock = "finished",
     .note = kNoteUnused, .hint = kHintUnused, .unused = true},

    {.id = "coffee_lisa", .section = S::GamePlus, .media = M::Scene, .label = "pc_archive_coffee_lisa", .unlock = "lisa_date",
     .note = "pc_note_archive_coffee_lisa", .unused = true, .secret = true},
};

constexpr std::string_view kFinishKeys[] = {
    "demo:gc_p02_500", "demo:gc_p00_022", "demo:gc_p02_510", "demo:gc_p00_020", "demo:gc_p00_160", "demo:gc_p00_010", "demo:gc_p05_010",
    "demo:gc_p04_310", "demo:gc_p01_070", "demo:gc_p04_340", "demo:gc_p01_081", "demo:gc_p01_011", "demo:gc_p01_020", "demo:gc_p03_010",
    "demo:gc_p04_300", "demo:gc_p04_350", "demo:gc_p03_070", "demo:gc_p03_011", "demo:gc_p01_110", "demo:gc_p04_320", "demo:gc_p04_120",
    "demo:gc_p04_280", "demo:gc_p02_520", "demo:gc_p02_090", "demo:gc_p02_080", "demo:gc_p06_010_final",
    "demo:gc_p00_030", "demo:gc_p01_050", "demo:gc_p01_090", "demo:gc_p01_022", "event:Play_bgm_herald01",
    "gimmick:Bag", "gimmick:Baby", "gimmick:CeilLamp", "gimmick:Ocho", "gimmick:Freezer", "floor:f010", "floor:f005", "floor:f050", "floor:f160",
    "voice:tria1000_101010", "voice:tria1000_1n1010", "voice:tria1000_1b1010", "event:Play_radio_religion_01", "overlay",
    "event:Play_Peephole_Theater", "voice:tria1000_1g1010", "voice:tria1000_171010",
    "event:Play_bg_startroom01", "event:Play_bg_passage01", "event:Play_bgm_unrest03", "event:Play_bgm_unrest02", "event:Play_bgm_corridor_f110",
};

const std::unordered_set<std::string>& UnlockKeys() {
    static const std::unordered_set<std::string> keys = [] {
        std::unordered_set<std::string> out;
        for (const ArchiveEntry& e : kEntries) out.insert(ArchiveUnlockKey(e));
        return out;
    }();
    return keys;
}

}

std::span<const ArchiveEntry> ArchiveEntries() {
    return kEntries;
}

const ArchiveEntry* FindArchiveEntry(std::string_view id) {
    for (const ArchiveEntry& e : kEntries) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

std::string ArchiveUnlockKey(const ArchiveEntry& entry) {
    if (!entry.unlock.empty()) return std::string(entry.unlock);
    if (entry.media == ArchiveMedia::Demo) return "demo:" + std::string(entry.asset);
    return std::string(entry.id);
}

bool IsArchiveUnlockKey(std::string_view key) {
    return UnlockKeys().contains(std::string(key));
}

std::string ArchiveBugScreenKey(std::string_view texture_stem) {
    return "bug:" + std::string(texture_stem);
}

std::string ArchiveHint(const ArchiveEntry& entry) {
    std::string note(entry.hint.empty() ? std::string_view("pc_note_archive_locked") : entry.hint);
    for (const int loop : {entry.hint_loop, entry.hint_loop2}) {
        if (loop > 0) note += kPcNoteArgument + std::to_string(loop);
    }
    return note;
}

std::span<const std::string_view> ArchiveFinishKeys() {
    return kFinishKeys;
}

std::string_view ArchiveSectionTitle(ArchiveSection section) {
    static constexpr std::array<std::string_view, kArchiveSectionCount> kTitles = {
        "pc_archive_images", "pc_archive_hall_crash", "pc_archive_hall_radio", "pc_archive_hall_voices", "pc_archive_photos", "pc_archive_cutscenes",
        "pc_archive_models", "pc_archive_music", "pc_archive_unused", "pc_archive_hall_gameplus"};
    return kTitles[static_cast<size_t>(section)];
}

std::string_view ArchiveSectionNote(ArchiveSection section) {
    static constexpr std::array<std::string_view, kArchiveSectionCount> kNotes = {
        "pc_note_archive_images", "pc_note_archive_crash_section", "pc_note_archive_radio_section", "pc_note_archive_voices_section",
        "pc_note_archive_photos", "pc_note_archive_cutscenes", "pc_note_archive_models", "pc_note_archive_music_section",
        "pc_note_archive_unused_section", "pc_note_archive_gameplus_section"};
    return kNotes[static_cast<size_t>(section)];
}

std::string ArchiveLabel(const ArchiveEntry& entry, int language) {
    std::string text(PcText(entry.label, language));
    if (entry.label.starts_with("pc_loop_")) {
        return entry.number > 0 ? std::format("{} ({})", text, entry.number) : text;
    }
    if (const size_t at = text.find("{1}"); at != std::string::npos) text.replace(at, 3, std::to_string(entry.number));
    return text;
}

std::string ArchiveCaption(const ArchiveEntry& entry) {
    auto stem = [](std::string_view path) {
        const size_t slash = path.find_last_of('/');
        std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
        const size_t dot = name.find_last_of('.');
        return std::string(dot == std::string_view::npos ? name : name.substr(0, dot));
    };
    switch (entry.media) {
    case ArchiveMedia::Image:
    case ArchiveMedia::String:
    case ArchiveMedia::Screen:
        return stem(entry.asset);
    case ArchiveMedia::Sound:
    case ArchiveMedia::Dialogue:
        return entry.extra.empty() ? std::string(entry.asset) : std::format("{} / {}", entry.asset, entry.extra);
    case ArchiveMedia::Photo:
        return entry.asset.empty() ? std::string("shsb_labl001 / pt14_hallway") : stem(entry.asset);
    case ArchiveMedia::Demo:
        return std::format("{} / {}", entry.asset, entry.floor);
    case ArchiveMedia::Model:
        return entry.extra.empty() ? stem(entry.asset) : std::format("{} / {}", entry.asset, entry.extra);
    case ArchiveMedia::Scene:
        return "lisa_date / Game+ 4";
    case ArchiveMedia::Music: {
        std::string source = entry.asset.starts_with("media:") ? "media " + std::string(entry.asset.substr(6)) : stem(entry.asset.substr(entry.asset.find(':') + 1));
        return entry.extra.empty() || entry.extra == source ? source : std::format("{} / {}", entry.extra, source);
    }
    }
    return {};
}

std::string_view ArchivePictureOf(const ArchiveEntry& entry) {
    if (entry.media != ArchiveMedia::Sound && entry.media != ArchiveMedia::Dialogue) return {};
    if (entry.asset.starts_with("Play_radio")) return "radio";
    if (entry.asset.starts_with("Play_tel")) return "phone_model";
    if (entry.asset.starts_with("Play_Peephole")) return "peephole_mask";
    if (entry.asset.starts_with("Play_voice_baby")) return "baby";
    if (entry.media == ArchiveMedia::Dialogue) return "bag_model";
    return {};
}

std::string_view ArchiveSectionCover(ArchiveSection section) {
    static constexpr std::array<std::string_view, kArchiveSectionCount> kCovers = {
        "sub01", "crash_4", "radio_news", "phone", "photo_complete", "door", "lisa_stand", "music_f110", "bathtub_lisa", "coffee_lisa"};
    return kCovers[static_cast<size_t>(section)];
}

int ArchiveThumbnailFrames(const ArchiveEntry& entry) {
    if (entry.media == ArchiveMedia::Model) return 30;
    if (entry.id == "preface" || entry.id == "awakening") return 420;
    if (entry.id == "teaser") return 60;
    if (entry.id == "ending") return 600;
    if (entry.id == "lisa_bathroom_walk") return 900;
    if (entry.id == "lisa_backwards") return 900;
    if (entry.id == "lisa_catch") return 200;
    return 150;
}

}
