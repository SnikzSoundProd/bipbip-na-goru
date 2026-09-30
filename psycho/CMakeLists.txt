# psycho — the second game on the bipbip engine.
#
# It links bipbip_engine ONLY. It does not link bipbip_game: the climbing
# game's Route/RunState/HUD/menu are not psycho's business, and depending on
# them would make the "reusable engine" claim fictional. If this target ever
# starts needing something from bipbip_game, that something belongs in engine/.
add_executable(psycho main.cpp)

target_include_directories(psycho PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(psycho PRIVATE bipbip_engine)

if(ENGINE_GNS_LIB)
    target_link_libraries(psycho PRIVATE ${ENGINE_GNS_LIB})
endif()

# runtime DLLs next to the exe so it runs from any folder
bipbip_copy_runtime_dlls(psycho)
