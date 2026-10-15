# Presentation through Direct3D 12 (src/engine/render/d3d12/, docs/upscaling.md "Intel XeSS Frame Generation"): the swapchain
# owners that exist for DirectX 12 only present the Vulkan frame through a D3D12 device on the same GPU. It needs nothing
# beyond the Windows SDK's headers (d3d12.h, dxgi1_6.h): d3d12.dll and dxgi.dll are the system's, loaded at run time, and no
# D3D12 Agility SDK is used (shader model 6.4, which XeSS-FG needs, is in the inbox runtime of Windows 10 19043 and later).
# The import libraries are not linked, so pt.exe starts without loading D3D12.
if(WIN32)
  option(PT_D3D12_PRESENT "Build the Direct3D 12 presentation path (XeSS Frame Generation)" ON)
else()
  set(PT_D3D12_PRESENT OFF)
endif()
if(PT_D3D12_PRESENT)
  target_compile_definitions(pt_engine PRIVATE PT_WITH_D3D12=1)
endif()

# The Rendering API choice (DirectX 12 or Vulkan; engine/platform/rendering_api.h, docs/rhi.md): the PC settings row and the first
# start question. On since 2026-10-15, when the D3D12 backend drew the whole game (the walkthrough on DirectX 12, parity on the
# fixed shots); DirectX 12 is offered only where a device can be made (D3D12Usable), and a start on DirectX 12 that fails sets
# pt.ini back to Vulkan. OFF hides both again; PT_RENDERING_API_UI=1 shows them in any Windows build for tests.
if(WIN32)
  option(PT_D3D12_RENDERING_API_CHOICE "Show players the Rendering API choice (DirectX 12 or Vulkan)" ON)
  # a build folder configured while the default was OFF takes the new default once (it can be set OFF again afterwards)
  if(NOT DEFINED PT_D3D12_RENDERING_API_CHOICE_DEFAULT_ON)
    set(PT_D3D12_RENDERING_API_CHOICE ON CACHE BOOL "Show players the Rendering API choice (DirectX 12 or Vulkan)" FORCE)
    set(PT_D3D12_RENDERING_API_CHOICE_DEFAULT_ON 1 CACHE INTERNAL "")
  endif()
  if(PT_D3D12_RENDERING_API_CHOICE)
    target_compile_definitions(pt_engine PUBLIC PT_D3D12_SELECTABLE=1)
  endif()
endif()
