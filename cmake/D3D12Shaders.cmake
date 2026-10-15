# D3D12 shaders (Windows, option PT_D3D12_SHADERS, docs/d3d12-shaders.md): every shaders/*.vert|frag|comp goes
#   glslc -DPT_D3D12 -> SPIR-V -> SPIRV-Cross (tools/dx12/spirv_to_hlsl.cpp) -> HLSL SM 6.6 -> DXC -> DXIL -> dxv
# into build/<dir>/shaders/<name>.dxil, with the resources the shader uses (class, set, binding, register space, register, count) in
# <name>.dxil.json next to it, which the D3D12 backend checks its root signature against (docs/rhi.md). The Vulkan SPIR-V rules (CMakeLists.txt)
# are untouched: tools/spv_hash.py compares them with a manifest. Intermediates are in build/<dir>/d3d12_work.
#
# Tools, hash-pinned and fetched like the other dependencies (FETCHCONTENT_SOURCE_DIR_DXC and _SPIRV_CROSS point at a local copy):
#   SPIRV-Cross  KhronosGroup commit 6c09849f (tag vulkan-sdk-1.4.357.0, the one shipped with the Vulkan SDK that builds the game's
#                SPIR-V), compiled into pt_spirv_to_hlsl here
#   DXC          microsoft/DirectXShaderCompiler v1.9.2609, dxc_2026_09_29.zip: dxc.exe, dxcompiler.dll, dxil.dll, dxv.exe
# tools/dx12/fetch_tools.py holds the same pins for a build outside CMake. To update a pin, change URL and SHA-256 here and there.
include(FetchContent)

set(PT_D3D12_SHADER_MODEL "66" CACHE STRING "HLSL shader model of the DXIL, 66 = 6.6 (65 is the least: inline ray query)")
string(SUBSTRING "${PT_D3D12_SHADER_MODEL}" 1 1 PT_D3D12_SM_MINOR)
set(PT_SPIRV_CROSS_COMMIT 6c09849fe88c48eaed08413aa022aaa136a3a057)
FetchContent_Declare(spirv_cross URL https://github.com/KhronosGroup/SPIRV-Cross/archive/${PT_SPIRV_CROSS_COMMIT}.zip
  URL_HASH SHA256=6114250b398aa30d3f77840e868b1dcdf2537ecc3f70ed0a754f0907e8889d49)
FetchContent_Declare(dxc URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip
  URL_HASH SHA256=ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1)
FetchContent_Populate(spirv_cross)
FetchContent_Populate(dxc)

if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
  set(PT_DXC_BIN_DIR "${dxc_SOURCE_DIR}/bin/arm64")
else()
  set(PT_DXC_BIN_DIR "${dxc_SOURCE_DIR}/bin/x64")
endif()
foreach(tool dxc.exe dxv.exe dxcompiler.dll dxil.dll)
  if(NOT EXISTS "${PT_DXC_BIN_DIR}/${tool}")
    message(FATAL_ERROR "D3D12 shaders: ${PT_DXC_BIN_DIR}/${tool} is missing from the DXC package (configure with -DPT_D3D12_SHADERS=OFF to skip)")
  endif()
endforeach()

# the SPIR-V -> HLSL translator: the SPIRV-Cross sources it needs, compiled without their warnings
set(PT_SPIRV_CROSS_SOURCES
  ${spirv_cross_SOURCE_DIR}/spirv_cfg.cpp
  ${spirv_cross_SOURCE_DIR}/spirv_cross.cpp
  ${spirv_cross_SOURCE_DIR}/spirv_cross_parsed_ir.cpp
  ${spirv_cross_SOURCE_DIR}/spirv_glsl.cpp
  ${spirv_cross_SOURCE_DIR}/spirv_hlsl.cpp
  ${spirv_cross_SOURCE_DIR}/spirv_parser.cpp)
add_executable(pt_spirv_to_hlsl tools/dx12/spirv_to_hlsl.cpp ${PT_SPIRV_CROSS_SOURCES})
target_include_directories(pt_spirv_to_hlsl PRIVATE ${spirv_cross_SOURCE_DIR})
target_compile_options(pt_spirv_to_hlsl PRIVATE /w /EHsc)
set_target_properties(pt_spirv_to_hlsl PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/d3d12_tools")

set(PT_D3D12_WORK_DIR ${CMAKE_BINARY_DIR}/d3d12_work)
# spirv-opt -O --preserve-interface in place of glslc -O: a fragment shader keeps the inputs it declares but does not read, so its
# DXIL input signature has the vertex shader's registers (D3D12 matches every semantic's register between the stages; without the
# unused TEXCOORD0, subsurface.frag's SV_Position moved to register 0 and its pipeline with fullscreen.vert failed)
get_filename_component(PT_GLSLC_DIR ${Vulkan_GLSLC_EXECUTABLE} DIRECTORY)
find_program(PT_SPIRV_OPT spirv-opt HINTS ${PT_GLSLC_DIR} REQUIRED)
set(PT_DXIL_OUTPUTS)
foreach(shader ${PT_SHADERS})
  get_filename_component(name ${shader} NAME)
  get_filename_component(ext ${shader} LAST_EXT)
  if(ext STREQUAL ".vert")
    set(stage vs)
    set(flip --flip-y)  # Vulkan clip space has Y down, D3D up
  elseif(ext STREQUAL ".frag")
    set(stage ps)
    set(flip)
  else()
    set(stage cs)
    set(flip)
  endif()
  set(spv ${PT_D3D12_WORK_DIR}/${name}.spv)
  set(hlsl ${PT_D3D12_WORK_DIR}/${name}.hlsl)
  set(dxil ${PT_SPIRV_DIR}/${name}.dxil)
  set(json ${PT_SPIRV_DIR}/${name}.dxil.json)
  add_custom_command(OUTPUT ${dxil} BYPRODUCTS ${json}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PT_D3D12_WORK_DIR}
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} --target-env=vulkan1.3 -DPT_D3D12 -MD -MF ${spv}.d -MT ${dxil} ${shader} -o ${spv}.raw
    COMMAND ${PT_SPIRV_OPT} --target-env=vulkan1.3 -O --preserve-interface ${spv}.raw -o ${spv}
    COMMAND pt_spirv_to_hlsl ${spv} ${hlsl} --sm ${PT_D3D12_SHADER_MODEL} ${flip} --json ${json}
    COMMAND ${PT_DXC_BIN_DIR}/dxc.exe -nologo -T ${stage}_6_${PT_D3D12_SM_MINOR} -E main -Fo ${dxil} ${hlsl}
    COMMAND ${PT_DXC_BIN_DIR}/dxv.exe ${dxil}
    DEPENDS ${shader} pt_spirv_to_hlsl DEPFILE ${spv}.d VERBATIM)
  list(APPEND PT_DXIL_OUTPUTS ${dxil})
endforeach()
add_custom_target(pt_shaders_d3d12 ALL DEPENDS ${PT_DXIL_OUTPUTS})
