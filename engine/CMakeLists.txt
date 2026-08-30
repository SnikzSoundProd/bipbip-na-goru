# bipbip-engine: platform/render/physics/net/world/audio/assets
add_library(bipbip_engine STATIC)

target_include_directories(bipbip_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS *.cpp)
# Keep standalone *_test.cpp units out of the engine lib; they are built
# separately (offline, no D3D) during verification.
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "_test\\.cpp$")
target_sources(bipbip_engine PRIVATE ${ENGINE_SOURCES})

# Physics: custom verlet solver (see engine/physics/verlet.*)
# Jolt abandoned: define-sync hell + 90MB lib; our genre needs bespoke feel anyway.

# GameNetworkingSockets (static, BCrypt crypto — see thirdparty/BUILD_NOTES.md)
# STEAMNETWORKINGSOCKETS_STATIC_LINK: headers otherwise declare dllimport
target_compile_definitions(bipbip_engine PUBLIC STEAMNETWORKINGSOCKETS_STATIC_LINK)
target_include_directories(bipbip_engine SYSTEM PUBLIC ${ENGINE_GNS_SRC_INC})
target_link_libraries(bipbip_engine PUBLIC ${ENGINE_GNS_LIB})
# GNS static pulls protobuf + abseil (installed manually from repo.msys2.org)
target_link_directories(bipbip_engine PUBLIC C:/msys64/mingw64/lib)
# Dynamic protobuf: link the import lib, and copy the DLL next to the exe
# (see game/CMakeLists.txt post-build step) so it runs from any folder.
target_link_libraries(bipbip_engine PUBLIC
    protobuf
    absl_strings absl_status absl_statusor absl_time absl_civil_time
    absl_time_zone absl_log_severity absl_log_initialize absl_log_globals
    absl_log_internal_format absl_log_internal_globals
    absl_log_internal_conditions absl_log_internal_message
    absl_log_internal_check_op absl_log_internal_log_sink_set absl_log_sink
    absl_log_entry absl_throw_delegate absl_symbolize
    absl_debugging_internal absl_demangle_internal absl_demangle_rust
    absl_stacktrace absl_examine_stack absl_decode_rust_punycode
    absl_utf8_for_code_point absl_synchronization absl_graphcycles_internal
    absl_kernel_timeout_internal absl_malloc_internal absl_spinlock_wait
    absl_raw_logging_internal absl_hashtablez_sampler
    absl_random_seed_sequences absl_random_internal_randen
    absl_random_internal_randen_slow absl_random_internal_randen_hwaes
    absl_random_internal_randen_hwaes_impl absl_random_internal_platform
    absl_random_internal_seed_material absl_random_internal_entropy_pool
    absl_random_distributions absl_random_seed_gen_exception
    absl_crc32c absl_crc_cord_state absl_crc_cpu_detect absl_crc_internal
    absl_leak_check absl_periodic_sampler absl_scoped_set_env
)

target_link_libraries(bipbip_engine PUBLIC
    d3d11 dxgi d3dcompiler ws2_32 bcrypt crypt32 winmm xinput iphlpapi psapi userenv dbghelp
    windowscodecs ole32 uuid
)
