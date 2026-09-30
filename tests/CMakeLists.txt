# ---------------------------------------------------------------------------
# Tests — every suite is a standalone executable registered with CTest, so
#   cmake --build . && ctest
# is the single entry point. No hand-rolled g++ command lines.
#
# (enable_testing() lives in the top-level CMakeLists.txt — see the note there)

# Small helper: add a test executable + register it with CTest.
#   bipbip_add_test(<name> <sources...>)
function(bipbip_add_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/engine
        ${CMAKE_SOURCE_DIR}/game
    )
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )
    # Tests read asset files with paths relative to the project root, so run
    # them from there (ctest otherwise executes in the build dir).
    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endfunction()

# --- engine: config ---------------------------------------------------------
bipbip_add_test(game_config_test
    ${CMAKE_SOURCE_DIR}/engine/core/game_config_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/core/game_config.cpp
)

# --- engine: scene serialization -------------------------------------------
bipbip_add_test(scene_test
    ${CMAKE_SOURCE_DIR}/engine/core/scene_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/core/scene.cpp
)

# --- engine: physics sleep --------------------------------------------------
bipbip_add_test(physics_sleep_test
    ${CMAKE_SOURCE_DIR}/engine/physics/physics_sleep_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/physics/verlet.cpp
    ${CMAKE_SOURCE_DIR}/engine/world/heightfield.cpp
)

# --- engine: frustum culling ------------------------------------------------
bipbip_add_test(frustum_test
    ${CMAKE_SOURCE_DIR}/engine/render/frustum_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/render/camera.cpp
)

# --- engine: asset pipeline (obj loader + file watcher) ---------------------
bipbip_add_test(asset_pipeline_test
    ${CMAKE_SOURCE_DIR}/engine/render/asset_pipeline_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/render/obj_loader.cpp
    ${CMAKE_SOURCE_DIR}/engine/core/asset_watch.cpp
)

# --- game: menu state / IPv4 validation -------------------------------------
bipbip_add_test(menu_state_test
    ${CMAKE_SOURCE_DIR}/game/ui/menu_state_test.cpp
    ${CMAKE_SOURCE_DIR}/game/ui/menu_state.cpp
)

# --- editor: camera fly-to --------------------------------------------------
bipbip_add_test(camera_fly_test
    ${CMAKE_SOURCE_DIR}/editor/camera_fly_test.cpp
)

# --- editor: scene sits on the ground (not buried) --------------------------
# Needs the scene format + terrain height sampling.
bipbip_add_test(scene_ground_test
    ${CMAKE_SOURCE_DIR}/editor/scene_ground_test.cpp
    ${CMAKE_SOURCE_DIR}/engine/core/scene.cpp
    ${CMAKE_SOURCE_DIR}/engine/world/heightfield.cpp
)

# --- editor: PIE world (start / step / stop / restart) ----------------------
# Pulls gameplay + net headers, so it links the shared game lib.
bipbip_add_test(pie_test
    ${CMAKE_SOURCE_DIR}/editor/pie_test.cpp
    ${CMAKE_SOURCE_DIR}/editor/pie_world.cpp
)
target_link_libraries(pie_test PRIVATE bipbip_game bipbip_engine)
if(ENGINE_GNS_LIB)
    target_link_libraries(pie_test PRIVATE ${ENGINE_GNS_LIB})
endif()

# --- editor: climber produces visible body parts ---------------------------
bipbip_add_test(climber_vis_test
    ${CMAKE_SOURCE_DIR}/editor/climber_vis_test.cpp
)
target_link_libraries(climber_vis_test PRIVATE bipbip_game bipbip_engine)
if(ENGINE_GNS_LIB)
    target_link_libraries(climber_vis_test PRIVATE ${ENGINE_GNS_LIB})
endif()

# Tests that need runtime DLLs next to them (protobuf etc.), same as the game.
foreach(t game_config_test scene_test physics_sleep_test frustum_test
          asset_pipeline_test menu_state_test camera_fly_test scene_ground_test
          pie_test climber_vis_test)
    bipbip_copy_runtime_dlls(${t})
endforeach()

    # entity/format test
    bipbip_add_test(entity_test
        engine/core/scene.cpp
        LABELS "core")
    set_target_properties(entity_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

# --- co-op smoke test ------------------------------------------------------
# Spins up a real host + client (two processes — NetLayer has a static
# instance, so one process cannot do both) and verifies the handshake on both
# sides. Depends on the game exe being built.
if(WIN32)
    add_test(NAME coop_smoke_test
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/scripts/coop_smoke.ps1)
    set_tests_properties(coop_smoke_test PROPERTIES
        TIMEOUT 180
        LABELS "network")
endif()
