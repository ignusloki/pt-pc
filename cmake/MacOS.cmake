# macOS (docs/macos.md): Vulkan there is MoltenVK, Vulkan 1.4 on Metal (Apache-2.0). The official release's libMoltenVK.dylib,
# pinned by its SHA-256 like the other downloads, goes next to pt, where the game looks for it in a build folder; the app
# bundle (tools/macos/make_app.py) carries it in Contents/Frameworks. PT_MOLTENVK_LIBRARY takes a local one instead.
# One build per CPU architecture, arm64 on Apple silicon and x86_64 on Intel: ggml's CPU variants (the voice recognizer) are
# chosen from the target's processor, so a universal build would only get one architecture's variants. The default is the host's.
if(CMAKE_OSX_ARCHITECTURES MATCHES ";")
  message(FATAL_ERROR "Build macOS for one architecture at a time (CMAKE_OSX_ARCHITECTURES=arm64 or x86_64), not \"${CMAKE_OSX_ARCHITECTURES}\"; docs/macos.md")
endif()
set(PT_MOLTENVK_VERSION "1.4.2")
set(PT_MOLTENVK_SHA256 f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e)
set(PT_MOLTENVK_LIBRARY "" CACHE FILEPATH "libMoltenVK.dylib to ship instead of the pinned MoltenVK release")
set(PT_MOLTENVK_DIR "${CMAKE_BINARY_DIR}/_deps/moltenvk-${PT_MOLTENVK_VERSION}")
set(pt_moltenvk_tar "${PT_MOLTENVK_DIR}/MoltenVK-macos.tar")
set(pt_moltenvk_dylib "${PT_MOLTENVK_DIR}/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib")
set(pt_moltenvk_license "${PT_MOLTENVK_DIR}/MoltenVK/LICENSE")
if(NOT EXISTS "${pt_moltenvk_dylib}" OR NOT EXISTS "${pt_moltenvk_license}")
  if(EXISTS "${pt_moltenvk_tar}")
    file(SHA256 "${pt_moltenvk_tar}" pt_moltenvk_existing)
  else()
    set(pt_moltenvk_existing "")
  endif()
  if(NOT pt_moltenvk_existing STREQUAL PT_MOLTENVK_SHA256)
    file(DOWNLOAD "https://github.com/KhronosGroup/MoltenVK/releases/download/v${PT_MOLTENVK_VERSION}/MoltenVK-macos.tar" "${pt_moltenvk_tar}"
      EXPECTED_HASH SHA256=${PT_MOLTENVK_SHA256} STATUS pt_moltenvk_status)
    list(GET pt_moltenvk_status 0 pt_moltenvk_code)
    if(NOT pt_moltenvk_code EQUAL 0)
      file(REMOVE "${pt_moltenvk_tar}")
      message(FATAL_ERROR "cannot download MoltenVK ${PT_MOLTENVK_VERSION}: ${pt_moltenvk_status}")
    endif()
  endif()
  file(ARCHIVE_EXTRACT INPUT "${pt_moltenvk_tar}" DESTINATION "${PT_MOLTENVK_DIR}"
    PATTERNS "MoltenVK/LICENSE" "MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib")
endif()
if(PT_MOLTENVK_LIBRARY)
  set(pt_moltenvk_dylib "${PT_MOLTENVK_LIBRARY}")
endif()

if(NOT pt_moltenvk_dylib MATCHES "[.]dylib$" OR NOT EXISTS "${pt_moltenvk_dylib}")
  message(FATAL_ERROR "PT_MOLTENVK_LIBRARY must name an existing libMoltenVK.dylib")
endif()
find_library(PT_COCOA_FRAMEWORK Cocoa REQUIRED)
find_library(PT_METAL_FRAMEWORK Metal REQUIRED)
find_library(PT_METALFX_FRAMEWORK MetalFX REQUIRED)
target_sources(pt_engine PRIVATE src/engine/render/upscale/metalfx_backend.mm)
set_source_files_properties(src/engine/render/upscale/metalfx_backend.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
target_link_libraries(pt_engine PRIVATE ${PT_METAL_FRAMEWORK} ${PT_METALFX_FRAMEWORK})
foreach(game pt pt_release)
  set_target_properties(${game} PROPERTIES BUILD_RPATH "@executable_path;@executable_path/../Frameworks")
  add_custom_command(TARGET ${game} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${pt_moltenvk_dylib}" "$<TARGET_FILE_DIR:${game}>/libMoltenVK.dylib"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${game}>/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${pt_moltenvk_license}" "$<TARGET_FILE_DIR:${game}>/licenses/MoltenVK_LICENSE.txt" VERBATIM)
endforeach()
add_executable(pt_setup_macos EXCLUDE_FROM_ALL installer/Native/setup_macos.mm src/engine/platform/os.cpp)
target_include_directories(pt_setup_macos PRIVATE src installer/Native "${CMAKE_BINARY_DIR}/generated")
add_dependencies(pt_setup_macos pt_version)
target_compile_options(pt_setup_macos PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
target_link_libraries(pt_setup_macos PRIVATE zlibstatic ${PT_COCOA_FRAMEWORK} Threads::Threads)
