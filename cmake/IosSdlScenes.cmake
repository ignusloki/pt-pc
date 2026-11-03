# SDL 3.4.16 starts SDL_main for every UIWindowScene, including AirPlay's
# noninteractive external scene. Keep one game on the application scene and let
# iOS mirror it. Compile a marked generated copy, preserving the dependency cache.
# Apple DTS: https://developer.apple.com/forums/thread/815376
file(READ "${sdl3_SOURCE_DIR}/src/video/uikit/SDL_uikitappdelegate.m" pt_sdl_delegate)

function(pt_ios_sdl_replace before after)
  string(FIND "${pt_sdl_delegate}" "${before}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "The iOS SDL scene guard no longer matches the pinned SDL source")
  endif()
  string(REPLACE "${before}" "${after}" pt_sdl_delegate "${pt_sdl_delegate}")
  set(pt_sdl_delegate "${pt_sdl_delegate}" PARENT_SCOPE)
endfunction()

pt_ios_sdl_replace("static int exit_status;" [=[static int exit_status;
// P.T. modification: one application scene and one main invocation per process.
#include "engine/platform/ios_scene_state.h"
static PTIosSceneState pt_scene_state;
]=])

pt_ios_sdl_replace("    UIWindowScene *windowScene = (UIWindowScene *)scene;" [=[    fprintf(stderr, "iOS scene: connecting role=%s scene=%p\n", session.role.UTF8String, (__bridge void *)scene);
    if (!PTIosAcceptScene(&pt_scene_state, (__bridge const void *)scene,
                         [session.role isEqualToString:UIWindowSceneSessionRoleApplication])) {
        // No external window: UIKit keeps ordinary AirPlay screen mirroring.
        fprintf(stderr, "iOS scene: ignored secondary scene; keep mirroring\n");
        return;
    }
    UIWindowScene *windowScene = (UIWindowScene *)scene;]=])

# Ignored external scenes must not pause/resume the primary game's Metal work.
foreach(callback sceneDidBecomeActive sceneWillResignActive sceneWillEnterForeground sceneDidEnterBackground)
  pt_ios_sdl_replace("- (void)${callback}:(UIScene *)scene\n{"
    "- (void)${callback}:(UIScene *)scene\n{\n    if (!PTIosIsPrimaryScene(&pt_scene_state, (__bridge const void *)scene)) return;")
endforeach()

# Guard BEFORE toggling SDL's nested event pump, for scene and legacy delegates.
# Returning from a duplicate SDL_main alone would disable the running game's pump.
pt_ios_sdl_replace("- (void)postFinishLaunch\n{" [=[- (void)postFinishLaunch
{
    if (!PTIosBeginMain(&pt_scene_state)) {
        fprintf(stderr, "iOS scene: duplicate game launch ignored\n");
        return;
    }]=])

set(pt_sdl_delegate_file "${CMAKE_BINARY_DIR}/generated/SDL_uikitappdelegate_pt.m")
file(CONFIGURE OUTPUT "${pt_sdl_delegate_file}" CONTENT "${pt_sdl_delegate}" @ONLY)
set_source_files_properties("${sdl3_SOURCE_DIR}/src/video/uikit/SDL_uikitappdelegate.m"
  TARGET_DIRECTORY SDL3-static PROPERTIES HEADER_FILE_ONLY ON)
target_sources(SDL3-static PRIVATE "${pt_sdl_delegate_file}")
target_include_directories(SDL3-static PRIVATE "${CMAKE_SOURCE_DIR}/src" "${sdl3_SOURCE_DIR}/src/video/uikit")
