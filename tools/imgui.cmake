# Dear ImGui (docking branch, vendored in thirdparty/imgui).
# Used by the editor UI. The game itself does NOT link ImGui — its HUD stays
# the in-game text renderer so the shipped build keeps the game's own style.
add_library(imgui STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/backends/imgui_impl_win32.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/backends/imgui_impl_dx11.cpp
)

target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/backends
)

target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(imgui PUBLIC d3d11 dxgi dwmapi)
