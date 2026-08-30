# bipbip_editor.exe — Unreal-style level editor (separate, lightweight target).
# Links the SAME engine + gameplay libs as the game, so the editor runs the
# real simulation (no duplicated logic that can drift out of sync).
add_executable(bipbip_editor
    editor_main.cpp
    pie_world.cpp
)

target_include_directories(bipbip_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(bipbip_editor PRIVATE
    bipbip_game
    bipbip_engine
    imgui
)

if(ENGINE_GNS_LIB)
    target_link_libraries(bipbip_editor PRIVATE ${ENGINE_GNS_LIB})
endif()

set(BIPBIP_DLL_SRC "C:/msys64/mingw64/bin")
add_custom_command(TARGET bipbip_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${BIPBIP_DLL_SRC}/libprotobuf.dll"
        "${BIPBIP_DLL_SRC}/libprotobuf-lite.dll"
        "${BIPBIP_DLL_SRC}/libgcc_s_seh-1.dll"
        "${BIPBIP_DLL_SRC}/libstdc++-6.dll"
        "${BIPBIP_DLL_SRC}/libwinpthread-1.dll"
        "$<TARGET_FILE_DIR:bipbip_editor>"
    COMMENT "Copying runtime DLLs next to bipbip_editor.exe"
)
