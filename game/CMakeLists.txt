# bipbip.exe — the game itself
add_executable(bipbip
    main.cpp
    player/climber.cpp
)

# repo root for "game/..." and engine dir for direct includes
target_include_directories(bipbip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(bipbip PRIVATE bipbip_engine)

if(ENGINE_JOLT_LIB)
    target_link_libraries(bipbip PRIVATE ${ENGINE_JOLT_LIB})
endif()
if(ENGINE_GNS_LIB)
    target_link_libraries(bipbip PRIVATE ${ENGINE_GNS_LIB})
endif()
