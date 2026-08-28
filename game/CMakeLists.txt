# bipbip.exe — the game itself
add_executable(bipbip
    main.cpp
    player/climber.cpp
    world/route.cpp
    gameplay/run.cpp
)

# repo root for "game/..." and engine dir for direct includes
target_include_directories(bipbip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(bipbip PRIVATE bipbip_engine)

# Jolt abandoned (see BUILD_NOTES.md) — verlet solver is our physics now.
# if(ENGINE_JOLT_LIB)
#     target_link_libraries(bipbip PRIVATE ${ENGINE_JOLT_LIB})
# endif()
if(ENGINE_GNS_LIB)
    target_link_libraries(bipbip PRIVATE ${ENGINE_GNS_LIB})
endif()

# Copy mingw64 runtime DLLs (libprotobuf, libgcc, libstdc++, winpthread) next to
# the exe so it runs from any folder without MSYS on PATH.
set(BIPBIP_DLL_SRC "C:/msys64/mingw64/bin")
add_custom_command(TARGET bipbip POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${BIPBIP_DLL_SRC}/libprotobuf.dll"
        "${BIPBIP_DLL_SRC}/libprotobuf-lite.dll"
        "${BIPBIP_DLL_SRC}/libgcc_s_seh-1.dll"
        "${BIPBIP_DLL_SRC}/libstdc++-6.dll"
        "${BIPBIP_DLL_SRC}/libwinpthread-1.dll"
        "$<TARGET_FILE_DIR:bipbip>"
    COMMENT "Copying runtime DLLs next to bipbip.exe"
)
