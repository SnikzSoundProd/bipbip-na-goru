# bipbip-engine: platform/render/physics/net/world/audio/assets
add_library(bipbip_engine STATIC)

target_include_directories(bipbip_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS *.cpp)
target_sources(bipbip_engine PRIVATE ${ENGINE_SOURCES})

# Physics: custom verlet solver (see engine/physics/verlet.*)
# Jolt abandoned: define-sync hell + 90MB lib; our genre needs bespoke feel anyway.
target_link_libraries(bipbip_engine PUBLIC
    d3d11 dxgi d3dcompiler ws2_32 bcrypt crypt32 winmm xinput
)
