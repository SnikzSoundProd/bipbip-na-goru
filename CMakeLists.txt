cmake_minimum_required(VERSION 3.24)
project(bipbip CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# thirdparty artifacts (built separately, see thirdparty/BUILD_NOTES.md)
set(ENGINE_JOLT_INCLUDE "C:/Users/apex/bipbip/thirdparty/JoltPhysics" CACHE PATH "Jolt headers")
set(ENGINE_JOLT_LIB     "C:/Users/apex/bipbip/thirdparty/jolt-build/libJolt.a" CACHE FILEPATH "Jolt static lib")
set(ENGINE_GNS_SRC_INC  "C:/Users/apex/Downloads/GameNetworkingSockets-1.6.0/GameNetworkingSockets-1.6.0/include" CACHE PATH "GNS source headers")
set(ENGINE_GNS_GEN_INC  "C:/Users/apex/bipbip/thirdparty/gns-build/include" CACHE PATH "GNS generated headers")
set(ENGINE_GNS_LIB      "C:/Users/apex/bipbip/thirdparty/gns-build/src/libGameNetworkingSockets_s.a" CACHE FILEPATH "GNS static lib")

include(tools/imgui.cmake)
include(tools/runtime_dlls.cmake)

# Enable CTest at the TOP LEVEL so `ctest` in the build dir finds the suites
# registered by the subdirectories (enable_testing() only affects the current
# directory and below, which is why it must live here, not in tests/).
enable_testing()

add_subdirectory(engine)
add_subdirectory(game)
add_subdirectory(editor)

# psycho — the second game. Links the engine only, proving the engine is
# reusable without depending on the first game.
add_subdirectory(psycho)

# Test suites (registered with CTest): cmake --build . && ctest
add_subdirectory(tests)
