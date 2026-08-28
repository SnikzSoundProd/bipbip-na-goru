#pragma once
// bipbip net: wire protocol — POD structs with static_assert sizes.
// All little-endian (x86 only), memcpy-based serialization.
#include <cstdint>

namespace bip::net {

constexpr uint16_t kDefaultPort = 27015;
constexpr int32_t  kProtocolVersion = 1;

// client -> host @20Hz
#pragma pack(push, 1)
struct InputPacket {
    int32_t  proto = kProtocolVersion;
    uint32_t seq;              // input sequence number
    float    moveX, moveZ;     // world-space move dir (already camera-relative on client)
    uint8_t  buttons;          // bit0 jump, bit1 grabL, bit2 grabR
    float    camYaw;
    static constexpr uint8_t BTN_JUMP=1, BTN_GRABL=2, BTN_GRABR=4;
};
static_assert(sizeof(InputPacket) == 21, "InputPacket size");

// host -> client @20Hz: pelvis transform of the remote player + flags
struct PlayerSnapshot {
    int32_t  proto = kProtocolVersion;
    uint32_t lastSeq;             // ack
    float    px, py, pz;          // pelvis position
    float    vyaw;                // facing yaw
    float    stamina;
    uint8_t  flags;               // bit0 grabbingL, bit1 grabbingR, bit2 finished
    float    pose[13][3];          // complete ragdoll particle positions
    struct BoxState {
        float px, py, pz;
        float qx, qy, qz, qw;
        uint8_t owner;              // 0=host, 1=client
    } boxes[14];                    // host-authoritative shared crates
    static constexpr uint8_t F_GRABL=1, F_GRABR=2, F_DONE=4;
};
static_assert(sizeof(PlayerSnapshot) == 591, "PlayerSnapshot size");

// host tells client where to spawn / seed info at connect accept
struct WelcomePacket {
    int32_t  proto = kProtocolVersion;
    uint64_t seed;
    float    spawnX, spawnY, spawnZ;
};
static_assert(sizeof(WelcomePacket) == 24, "WelcomePacket size");
#pragma pack(pop)

enum class MsgType : uint8_t { Input = 1, Snapshot = 2, Welcome = 3 };

} // namespace bip::net
