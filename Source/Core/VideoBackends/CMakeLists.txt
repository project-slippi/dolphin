# slippi change: no OpenGL on Emscripten (WebGPU backend comes in a later phase);
# the Software backend presents through OGL, so it stays desktop-only too.
if(NOT EMSCRIPTEN)
  add_subdirectory(OGL)
  add_subdirectory(Software)
endif()
add_subdirectory(Null)

if(WIN32)
  add_subdirectory(D3DCommon)
  add_subdirectory(D3D)
  add_subdirectory(D3D12)
endif()

if(APPLE)
  add_subdirectory(Metal)
endif()

if(ENABLE_VULKAN)
  add_subdirectory(Vulkan)
endif()
