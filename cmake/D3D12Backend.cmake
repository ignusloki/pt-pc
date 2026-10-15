# The Direct3D 12 renderer backend (docs/rhi.md, stage b of the DirectX 12 plan): src/engine/render/d3d12/backend as the static
# library pt_gpu_d3d12 and its test pt_d3d12_backend_test (tests/d3d12_backend_test.cpp: the real DXIL shaders on D3D12 against
# the same shaders on Vulkan, and a windowed smoke test). Windows only; the developer game (pt) links it and runs on it with
# --backend d3d12 (PT_D3D12_GAME, docs/rhi.md stage c), and so does the release game (pt_release). Linux and macOS never include
# this file.
#
# Dependencies, hash-pinned and fetched like the others (FETCHCONTENT_SOURCE_DIR_D3D12_AGILITY / _D3D12MA point at a local copy):
#   D3D12 Agility SDK   NuGet Microsoft.Direct3D.D3D12 1.619.6 (2026-09-14): headers (d3d12.h, d3dx12) and D3D12Core.dll /
#                       d3d12SDKLayers.dll, copied to <exe folder>/D3D12/ (agility_exports.h)
#   D3D12MA             GPUOpen D3D12MemoryAllocator v3.2.0
include(FetchContent)

set(PT_D3D12_AGILITY_VERSION 619)
FetchContent_Declare(d3d12_agility
  URL https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.d3d12/1.619.6/microsoft.direct3d.d3d12.1.619.6.nupkg
  DOWNLOAD_NAME microsoft.direct3d.d3d12.1.619.6.zip
  URL_HASH SHA256=08f0489281401aa430fc37322d6c3fc98a8025175aacd714c10d562f4963f1e9)
FetchContent_Declare(d3d12ma
  URL https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator/archive/refs/tags/v3.2.0.zip
  URL_HASH SHA256=66121c8fd2c8ba1fa50a08a10e1684a072dcc72423263ec4420c98f4cc7c4f80)
FetchContent_Populate(d3d12_agility)
FetchContent_Populate(d3d12ma)

set(PT_D3D12_AGILITY_INCLUDE "${d3d12_agility_SOURCE_DIR}/build/native/include")
if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
  set(PT_D3D12_AGILITY_BIN "${d3d12_agility_SOURCE_DIR}/build/native/bin/arm64")
else()
  set(PT_D3D12_AGILITY_BIN "${d3d12_agility_SOURCE_DIR}/build/native/bin/x64")
endif()
foreach(file D3D12Core.dll d3d12SDKLayers.dll)
  if(NOT EXISTS "${PT_D3D12_AGILITY_BIN}/${file}")
    message(FATAL_ERROR "D3D12 backend: ${PT_D3D12_AGILITY_BIN}/${file} is missing from the Agility SDK package (configure with -DPT_D3D12_BACKEND=OFF to skip)")
  endif()
endforeach()

file(GLOB PT_D3D12_BACKEND_SOURCES CONFIGURE_DEPENDS src/engine/render/d3d12/backend/*.cpp src/engine/render/d3d12/backend/*.h)
# ImGui's DirectX 12 renderer for the developer overlay (d3d12_imgui.cpp); ImGui itself is pt_engine's
add_library(pt_gpu_d3d12 STATIC ${PT_D3D12_BACKEND_SOURCES} "${d3d12ma_SOURCE_DIR}/src/D3D12MemAlloc.cpp" "${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp")
# the Agility SDK's headers before the Windows SDK's (Enhanced Barriers, Options16, ID3D12GraphicsCommandList9)
target_include_directories(pt_gpu_d3d12 BEFORE PUBLIC "${PT_D3D12_AGILITY_INCLUDE}" "${d3d12ma_SOURCE_DIR}/include")
target_link_libraries(pt_gpu_d3d12 PUBLIC pt_engine)
# imgui_impl_dx12 calls CreateDXGIFactory1 (the backend itself loads dxgi.dll at run time)
target_link_libraries(pt_gpu_d3d12 PRIVATE dxgi)
target_compile_definitions(pt_gpu_d3d12 PUBLIC PT_D3D12_BACKEND=1 PT_D3D12_AGILITY_VERSION=${PT_D3D12_AGILITY_VERSION})
if(MSVC)
  target_compile_options(pt_gpu_d3d12 PRIVATE /W3 /utf-8 /EHsc)
  set_source_files_properties("${d3d12ma_SOURCE_DIR}/src/D3D12MemAlloc.cpp" PROPERTIES COMPILE_OPTIONS "/w")
endif()

add_executable(pt_d3d12_backend_test tests/d3d12_backend_test.cpp)
target_link_libraries(pt_d3d12_backend_test PRIVATE pt_gpu_d3d12 pt_engine)
if(MSVC)
  target_compile_options(pt_d3d12_backend_test PRIVATE /utf-8 /EHsc)
endif()
add_dependencies(pt_d3d12_backend_test pt_shaders)
if(TARGET pt_shaders_d3d12)
  add_dependencies(pt_d3d12_backend_test pt_shaders_d3d12)
endif()
add_custom_command(TARGET pt_d3d12_backend_test POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pt_d3d12_backend_test>/D3D12"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PT_D3D12_AGILITY_BIN}/D3D12Core.dll" "${PT_D3D12_AGILITY_BIN}/d3d12SDKLayers.dll"
          "$<TARGET_FILE_DIR:pt_d3d12_backend_test>/D3D12"
  VERBATIM)

# the game on DirectX 12 (--backend d3d12, and the Rendering API row once it is shown): the device from this library, the DXIL
# shaders, the Agility SDK runtime. Both the developer game (pt) and the release game (pt_release) link it; tools/package.py
# ships D3D12/D3D12Core.dll and the DXIL shaders with an executable that exports D3D12SDKVersion (agility_exports.h).
# d3d12SDKLayers.dll (the debug layer, PT_D3D12_DEBUG=1) is copied next to the build only, not packaged.
option(PT_D3D12_GAME "Let the game (pt and pt_release) run on the D3D12 backend with --backend d3d12" ON)
if(PT_D3D12_GAME)
  foreach(game pt pt_release)
    target_link_libraries(${game} PRIVATE pt_gpu_d3d12)
    target_compile_definitions(${game} PRIVATE PT_D3D12_GAME=1)
    if(TARGET pt_shaders_d3d12)
      add_dependencies(${game} pt_shaders_d3d12)
    endif()
    add_custom_command(TARGET ${game} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${game}>/D3D12" "$<TARGET_FILE_DIR:${game}>/licenses"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PT_D3D12_AGILITY_BIN}/D3D12Core.dll" "${PT_D3D12_AGILITY_BIN}/d3d12SDKLayers.dll"
              "$<TARGET_FILE_DIR:${game}>/D3D12"
      # the Agility SDK's licence (D3D12Core.dll is in its list of distributable files) and D3D12MA's (MIT, compiled in)
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${d3d12_agility_SOURCE_DIR}/LICENSE.txt"
              "$<TARGET_FILE_DIR:${game}>/licenses/Microsoft_DirectX_Agility_SDK_LICENSE.txt"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${d3d12ma_SOURCE_DIR}/LICENSE.txt"
              "$<TARGET_FILE_DIR:${game}>/licenses/D3D12MemoryAllocator_LICENSE.txt"
      VERBATIM)
  endforeach()
endif()

# the upscalers on DirectX 12 (d3d12_upscale.cpp): the SDKs cmake/Upscalers.cmake fetched, their D3D12 halves
if(pt_dlss_ok)
  target_include_directories(pt_gpu_d3d12 PRIVATE "${PT_DLSS_DIR}/include")
  target_compile_definitions(pt_gpu_d3d12 PRIVATE PT_WITH_DLSS=1)
  target_link_libraries(pt_gpu_d3d12 PRIVATE "${PT_DLSS_DIR}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")
endif()
if(pt_xess_ok)
  target_include_directories(pt_gpu_d3d12 PRIVATE "${PT_XESS_DIR}/inc")
  target_compile_definitions(pt_gpu_d3d12 PRIVATE PT_WITH_XESS=1)
endif()
if(pt_ffx_ok)
  target_include_directories(pt_gpu_d3d12 PRIVATE "${PT_FFX_DIR}/ffx-api/include")
  target_compile_definitions(pt_gpu_d3d12 PRIVATE PT_WITH_FSR=1)
  # AMD's DX12 DLL next to the game (the upscalers and frame generation on DirectX 12; Vulkan keeps amd_fidelityfx_vk.dll)
  if(PT_D3D12_GAME)
    foreach(game pt pt_release)
      add_custom_command(TARGET ${game} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PT_FFX_DIR}/PrebuiltSignedDLL/amd_fidelityfx_dx12.dll" "$<TARGET_FILE_DIR:${game}>"
        VERBATIM)
    endforeach()
  endif()
endif()
