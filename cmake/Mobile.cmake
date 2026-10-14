# iOS and Android (docs/mobile.md). The game target is an iOS app bundle or Android's libmain.so (CMakeLists.txt); here the
# platform's libraries and the files the game reads beside its executable (shaders/, fonts/).

target_compile_definitions(pt_engine PUBLIC PT_MOBILE=1)

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
  add_custom_command(TARGET pt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PT_MOLTENVK_IOS}" "$<TARGET_FILE_DIR:pt>/Frameworks/MoltenVK.framework" VERBATIM)

  set(PT_IOS_BUNDLE_ID "com.blounty.pt" CACHE STRING "iOS bundle identifier")
  set_target_properties(pt PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/ios/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${PT_IOS_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_NAME "P.T."
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PT_VERSION}")
  # the bundle is flat on iOS: shaders/ and fonts/ go beside the executable, where ResourceDir() looks
  add_custom_command(TARGET pt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:pt>/shaders" VERBATIM)
elseif(ANDROID)
  target_link_libraries(pt PRIVATE android log)
endif()
