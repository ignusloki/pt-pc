# iOS development bundle, adapted from Alex Blount (ilsonb), PR #5.
# Android keeps the maintained .NET launcher and dynamic voice runtime.

target_compile_definitions(pt_engine PUBLIC PT_WHISPER_STATIC=1)
# the true end's word: whisper.cpp in the game (cmake/Dependencies.cmake), its base.en model and the voice activity model
# beside the game (the larger second opinion model stays on the PC: 190 MB, and the heat of running it on a phone)
target_link_libraries(pt_engine PUBLIC whisper pt_bcdecode "-framework CoreFoundation" "-framework AVFoundation" "-framework Foundation")
target_sources(pt_engine PRIVATE src/engine/platform/macos_permissions.mm)
set_source_files_properties(src/engine/platform/macos_permissions.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
# ggml's optional debugger backtrace path uses fork/exec even on Apple mobile.
# Generate a sandbox-compatible copy; retain native symbols and leave dependency checkouts untouched.
file(READ "${whisper_SOURCE_DIR}/ggml/src/ggml.c" pt_ggml_source)
string(FIND "${pt_ggml_source}" "void ggml_print_backtrace(void) {" pt_backtrace_start)
string(FIND "${pt_ggml_source}" "\n}\n#else\nvoid ggml_print_backtrace(void)" pt_backtrace_end)
if(pt_backtrace_start EQUAL -1 OR pt_backtrace_end EQUAL -1 OR pt_backtrace_end LESS pt_backtrace_start)
  message(FATAL_ERROR "ggml backtrace source changed; review iOS sandbox adaptation")
endif()
string(SUBSTRING "${pt_ggml_source}" 0 ${pt_backtrace_start} pt_ggml_prefix)
string(SUBSTRING "${pt_ggml_source}" ${pt_backtrace_end} -1 pt_ggml_suffix)
file(WRITE "${CMAKE_BINARY_DIR}/generated/ggml_ios.c"
  "${pt_ggml_prefix}void ggml_print_backtrace(void) {\n    if (!getenv(\"GGML_NO_BACKTRACE\")) ggml_print_backtrace_symbols();${pt_ggml_suffix}")
set_source_files_properties("${whisper_SOURCE_DIR}/ggml/src/ggml.c" TARGET_DIRECTORY ggml-base PROPERTIES HEADER_FILE_ONLY ON)
target_sources(ggml-base PRIVATE "${CMAKE_BINARY_DIR}/generated/ggml_ios.c")
target_include_directories(ggml-base PRIVATE "${whisper_SOURCE_DIR}/ggml/src")

set(PT_MOBILE_VOICE_MODELS "${CMAKE_BINARY_DIR}/voice/ggml-base.en-q5_1.bin" "${CMAKE_BINARY_DIR}/voice/ggml-silero-v6.2.0.bin")

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  # Vulkan is MoltenVK: the official release's dynamic framework (PT_MOLTENVK_IOS, MoltenVK-ios.tar:
  # MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework), embedded in the bundle's Frameworks/ and loaded by
  # SDL and the game (vk_context.cpp VulkanLibraryPath). Not linked statically: volk's own vkGetInstanceProcAddr would clash.
  set(PT_MOLTENVK_IOS "" CACHE PATH "MoltenVK.framework for ios-arm64 (the dynamic xcframework's)")
  if(NOT EXISTS "${PT_MOLTENVK_IOS}/MoltenVK")
    message(FATAL_ERROR "Set PT_MOLTENVK_IOS to the ios-arm64 MoltenVK.framework of MoltenVK-ios.tar (docs/mobile.md)")
  endif()
  target_link_libraries(pt PRIVATE "-framework UIKit" "-framework Foundation")
  # iOS has no system(): a copy of Lua's loslib.c whose os.execute reports failure (mods cannot run commands there anyway)
  file(READ "${lua51_SOURCE_DIR}/src/loslib.c" pt_loslib)
  string(REPLACE "system(luaL_optstring(L, 1, NULL))" "-1" pt_loslib "${pt_loslib}")
  file(WRITE "${CMAKE_BINARY_DIR}/generated/loslib_ios.c" "${pt_loslib}")
  set_source_files_properties("${lua51_SOURCE_DIR}/src/loslib.c" PROPERTIES HEADER_FILE_ONLY ON)
  target_sources(pt_thirdparty PRIVATE "${CMAKE_BINARY_DIR}/generated/loslib_ios.c")
  target_include_directories(pt_thirdparty PRIVATE "${lua51_SOURCE_DIR}/src")
  if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
    add_custom_command(TARGET pt POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${PT_MOLTENVK_IOS}" "$<TARGET_FILE_DIR:pt>/Frameworks/MoltenVK.framework" VERBATIM)
  endif()

  set(PT_IOS_BUNDLE_ID "org.ignusloki.pt.mobile" CACHE STRING "iOS bundle identifier (a development team's own for signing)")
  set_target_properties(pt PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/ios/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${PT_IOS_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_NAME "P.T."
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PT_VERSION}")
  set_target_properties(pt PROPERTIES
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_EMBED_FRAMEWORKS "${PT_MOLTENVK_IOS}"
    XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY YES
    XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY YES)
  if(CMAKE_GENERATOR STREQUAL "Xcode")
    set_target_properties(pt PROPERTIES
      XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${PT_IOS_BUNDLE_ID}"
      MACOSX_BUNDLE_GUI_IDENTIFIER "$(PRODUCT_BUNDLE_IDENTIFIER)")
  endif()
  configure_file("${bcdecode_SOURCE_DIR}/LICENSE" "${CMAKE_BINARY_DIR}/licenses/bcdecode-LICENSE.txt" COPYONLY)
  add_custom_command(TARGET pt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pt>/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/voice/licenses" "$<TARGET_FILE_DIR:pt>/licenses/voice"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/licenses/bcdecode-LICENSE.txt" "$<TARGET_FILE_DIR:pt>/licenses/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${google_sbc_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:pt>/licenses/google-libsbc-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${zstd_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:pt>/licenses/zstd-LICENSE.txt" VERBATIM)
  foreach(entry
      "${CMAKE_SOURCE_DIR}/LICENSE|pt-MIT.txt"
      "${CMAKE_SOURCE_DIR}/third_party/macos_notices/MoltenVK-Apache-2.0.txt|MoltenVK-Apache-2.0.txt"
      "${sdl3_SOURCE_DIR}/LICENSE.txt|SDL3-LICENSE.txt"
      "${sdl3_SOURCE_DIR}/src/hidapi/LICENSE.txt|hidapi-LICENSE.txt"
      "${zlib_SOURCE_DIR}/LICENSE|zlib-LICENSE.txt"
      "${volk_SOURCE_DIR}/LICENSE.md|volk-LICENSE.md"
      "${vma_SOURCE_DIR}/LICENSE.txt|VMA-LICENSE.txt"
      "${glm_SOURCE_DIR}/copying.txt|GLM-copying.txt"
      "${imgui_SOURCE_DIR}/LICENSE.txt|ImGui-LICENSE.txt"
      "${stb_SOURCE_DIR}/LICENSE|stb-LICENSE.txt"
      "${lua51_SOURCE_DIR}/COPYRIGHT|Lua-COPYRIGHT.txt"
      "${harfbuzz_SOURCE_DIR}/COPYING|HarfBuzz-COPYING.txt"
      "${ogg_SOURCE_DIR}/COPYING|libogg-COPYING.txt"
      "${vorbis_SOURCE_DIR}/COPYING|libvorbis-COPYING.txt")
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 source)
    list(GET parts 1 name)
    if(EXISTS "${source}")
      add_custom_command(TARGET pt POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${source}" "$<TARGET_FILE_DIR:pt>/licenses/${name}" VERBATIM)
    endif()
  endforeach()
  # the bundle is flat on iOS: shaders/ and fonts/ go beside the executable, where ResourceDir() looks
  add_custom_command(TARGET pt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:pt>/shaders"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pt>/voice"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PT_MOBILE_VOICE_MODELS} "$<TARGET_FILE_DIR:pt>/voice/" VERBATIM)
endif()
