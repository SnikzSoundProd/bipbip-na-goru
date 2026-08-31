# bipbip_editor.exe — Unreal-style level editor (separate, lightweight target).
# Links the SAME engine + gameplay libs as the game, so the editor runs the
# real simulation (no duplicated logic that can drift out of sync).
set(BIPBIP_EDITOR_NAME "bipbip_editor" CACHE STRING "Output name for the editor exe")

add_executable(bipbip_editor
    editor_main.cpp
    pie_world.cpp
)

set_target_properties(bipbip_editor PROPERTIES OUTPUT_NAME "${BIPBIP_EDITOR_NAME}")

target_include_directories(bipbip_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(bipbip_editor PRIVATE
    bipbip_game
    bipbip_engine
    imgui
)

if(ENGINE_GNS_LIB)
    target_link_libraries(bipbip_editor PRIVATE ${ENGINE_GNS_LIB})
endif()

# runtime DLLs next to the exe so it runs from any folder
bipbip_copy_runtime_dlls(bipbip_editor)
