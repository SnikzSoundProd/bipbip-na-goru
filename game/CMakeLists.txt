# bipbip_game — static lib of shared gameplay code (player, route, run, menu, hud).
# Both bipbip.exe (the game) and bipbip_editor.exe link this, so the editor
# runs the REAL simulation instead of a reimplementation that can drift.
add_library(bipbip_game STATIC
    world/route.cpp
    gameplay/run.cpp
    ui/menu_state.cpp
    hud/hud_overlay.cpp
)

target_include_directories(bipbip_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(bipbip_game PUBLIC bipbip_engine)

# ---------------------------------------------------------------------------
# bipbip.exe — the game itself
add_executable(bipbip main.cpp)
target_include_directories(bipbip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(bipbip PRIVATE bipbip_game bipbip_engine)

if(ENGINE_GNS_LIB)
    target_link_libraries(bipbip PRIVATE ${ENGINE_GNS_LIB})
endif()

# runtime DLLs next to the exe so it runs from any folder
bipbip_copy_runtime_dlls(bipbip)
