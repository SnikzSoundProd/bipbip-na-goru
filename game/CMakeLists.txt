# bipbip.exe — the game itself
add_executable(bipbip main.cpp)

target_link_libraries(bipbip PRIVATE bipbip_engine)

if(ENGINE_JOLT_LIB)
    target_link_libraries(bipbip PRIVATE ${ENGINE_JOLT_LIB})
endif()
if(ENGINE_GNS_LIB)
    target_link_libraries(bipbip PRIVATE ${ENGINE_GNS_LIB})
endif()
