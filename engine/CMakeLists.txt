# bipbip-engine: platform/render/physics/net/world/audio/assets
add_library(bipbip_engine STATIC)

target_include_directories(bipbip_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS *.cpp)
target_sources(bipbip_engine PRIVATE ${ENGINE_SOURCES})

# DX11 + Win32 system libs (mingw64 import libs)
target_link_libraries(bipbip_engine PUBLIC
    d3d11 dxgi d3dcompiler ws2_32 bcrypt crypt32 winmm xinput
)
