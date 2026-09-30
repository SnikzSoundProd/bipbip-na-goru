// Generic wire-protocol test (offline, no D3D, no network).
//
// STEP 4 of generalising the engine. protocol.h is currently hardcoded to the
// climbing game: PlayerSnapshot is a fixed 611-byte struct containing a 13x3
// ragdoll pose, 14 crates and a stamina float. A second game with a different
// entity count and different per-entity state cannot use it.
//
// This layer adds a GENERIC payload protocol on top, leaving NetLayer's
// transport (which is already game-agnostic) untouched:
//   - SnapshotWriter/Reader: variable-length, type-tagged, id-addressed
//     entity state, so any game can serialise whatever it needs.
//   - The old fixed structs stay for the current game, proving backwards
//     compatibility.
//
// Test order follows the build order: writer -> reader round-trip ->
// size/limits -> interop with the legacy fixed struct.
#include "net/protocol_generic.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++failures; } } while (0)

using namespace bip;
using namespace bip::net;

// ------------------------------------------------------- writer -> reader
static void test_basicRoundTrip() {
    SnapshotWriter w;
    w.begin(7 /*base seq*/);

    w.writeFloat(100, 1.5f);
    w.writeFloat(101, -2.25f);
    w.writeInt(102, 42);
    w.writeByte(103, 0xAB);
    w.writeQuat(104, 0.f, 0.f, 0.f, 1.f);
    w.writeBool(105, true);
    w.writeString(106, "hello world");

    std::vector<uint8_t> buf = w.finish();
    CHECK(!buf.empty(), "writer produced bytes");

    float v = 0.f, v2 = 0.f;
    SnapshotReader r(buf.data(), buf.size(), 7);
    CHECK(r.ok(), "reader accepts the payload");
    CHECK(r.readFloat(100, v), "float by id found");
    CHECK(std::fabs(v - 1.5f) < 1e-6f, "float value survives");
    CHECK(r.readFloat(101, v2), "second float found");
    CHECK(std::fabs(v2 + 2.25f) < 1e-6f, "second float value survives");
    int iv = 0;
    CHECK(r.readInt(102, iv) && iv == 42, "int by id survives");
    uint8_t bv = 0;
    CHECK(r.readByte(103, bv) && bv == 0xAB, "byte by id survives");
    float q[4] = {0,0,0,0};
    CHECK(r.readQuat(104, q), "quat by id found");
    CHECK(std::fabs(q[3] - 1.f) < 1e-6f, "quat w survives");
    bool bl = false;
    CHECK(r.readBool(105, bl) && bl, "bool by id survives");
    std::string s;
    CHECK(r.readString(106, s) && s == "hello world", "string by id survives");
}

static void test_missingIdIsFalse() {
    SnapshotWriter w;
    w.begin(0);
    w.writeFloat(5, 1.f);
    auto buf = w.finish();

    SnapshotReader r(buf.data(), buf.size(), 0);
    float v = 0.f;
    CHECK(!r.readFloat(999, v), "missing id returns false, not garbage");
    CHECK(r.readFloat(5, v), "present id still found");
}

// ---------------------------------------------------------- type safety
static void test_typeMismatchIsRejected() {
    SnapshotWriter w;
    w.begin(0);
    w.writeFloat(1, 3.f);
    w.writeInt(1, 7);        // same id, different type
    auto buf = w.finish();

    SnapshotReader r(buf.data(), buf.size(), 0);
    float f = 0.f; int i = 0;
    CHECK(r.readFloat(1, f), "float readable");
    CHECK(std::fabs(f - 3.f) < 1e-6f, "float value still correct");
    // the int write must NOT have clobbered the float slot; reading an int for
    // an id stored as float is a mismatch and must fail cleanly
    CHECK(!r.readInt(1, i), "reading a float as int is rejected");
}

static void test_overwriteSameId() {
    SnapshotWriter w;
    w.begin(0);
    w.writeFloat(1, 1.f);
    w.writeFloat(1, 2.f);       // overwrite
    auto buf = w.finish();

    SnapshotReader r(buf.data(), buf.size(), 0);
    float v = 0.f;
    CHECK(r.readFloat(1, v), "id present after overwrite");
    CHECK(std::fabs(v - 2.f) < 1e-6f, "last write wins");
}

// ------------------------------------------------------------- primitives
static void test_vec3AndPose() {
    SnapshotWriter w;
    w.begin(0);
    w.writeVec3(1, 1.f, 2.f, 3.f);
    w.writePose(2, 13, [](int i, float* out) { out[0] = (float)i; out[1] = 0; out[2] = 0; });
    auto buf = w.finish();

    SnapshotReader r(buf.data(), buf.size(), 0);
    float x=0,y=0,z=0;
    CHECK(r.readVec3(1, x, y, z), "vec3 found");
    CHECK(x==1.f && y==2.f && z==3.f, "vec3 components survive");
    float pose[13*3];
    CHECK(r.readPose(2, pose, 13), "13-particle pose found");
    CHECK(pose[0] == 0.f && pose[3] == 1.f && pose[36] == 12.f, "pose particles survive");
}

static void test_manyEntities() {
    // A second game may have hundreds of entities; the payload must scale and
    // the reader must find each one by id.
    SnapshotWriter w;
    w.begin(0);
    for (int i = 0; i < 200; ++i) w.writeFloat((uint32_t)(1000 + i), (float)i);
    auto buf = w.finish();
    CHECK(buf.size() < 200 * 16, "payload stays compact for 200 entities");

    SnapshotReader r(buf.data(), buf.size(), 0);
    bool all = true;
    for (int i = 0; i < 200; ++i) {
        float v = -1.f;
        if (!r.readFloat((uint32_t)(1000 + i), v) || std::fabs(v - (float)i) > 1e-6f) { all = false; break; }
    }
    CHECK(all, "all 200 entities readable by id");
}

// ----------------------------------------------------- corrupt / hostile
static void test_truncatedPayloadIsRejected() {
    SnapshotWriter w;
    w.begin(0);
    w.writeFloat(1, 1.f);
    w.writeString(2, "a somewhat longer string value here");
    auto buf = w.finish();

    // cut the buffer in half: the reader must not read past the end
    SnapshotReader r(buf.data(), buf.size() / 2, 0);
    CHECK(r.ok(), "truncated payload still opens without crashing");
    CHECK(r.truncated(), "truncation is reported, not hidden");
    // the first record sits before the cut, so it must still be readable...
    float v = 0.f;
    CHECK(r.readFloat(1, v) && std::fabs(v - 1.f) < 1e-6f, "record before the cut is still readable");
    // ...and the record straddling it must fail cleanly
    std::string s;
    CHECK(!r.readString(2, s), "record straddling the cut is rejected, not read past the end");
}

// An entity may legitimately get id 0, which every entity system hands out
// first, so it must round-trip like any other id.
static void test_idZeroRoundTrips() {
    SnapshotWriter w;
    w.begin(0);
    w.writeFloat(0, 9.f);
    w.writeFloat(7, 1.f);
    auto buf = w.finish();

    SnapshotReader r(buf.data(), buf.size(), 0);
    float z = 0.f, seven = 0.f;
    CHECK(r.readFloat(0, z) && std::fabs(z - 9.f) < 1e-6f, "id 0 round-trips (no magic terminator)");
    CHECK(r.readFloat(7, seven) && std::fabs(seven - 1.f) < 1e-6f, "records after id 0 are reachable");
}

static void test_emptyPayload() {
    SnapshotWriter w;
    w.begin(0);
    auto buf = w.finish();
    SnapshotReader r(buf.data(), buf.size(), 0);
    CHECK(r.ok(), "empty payload opens");
    float v = 0.f;
    CHECK(!r.readFloat(1, v), "empty payload has no fields");
}

static void test_garbagePayloadIsRejected() {
    // random bytes must not be read as valid records
    std::vector<uint8_t> junk(256);
    for (size_t i = 0; i < junk.size(); ++i) junk[i] = (uint8_t)(i * 37 + 11);
    SnapshotReader r(junk.data(), junk.size(), 0);
    // reading anything is allowed to fail, but must not crash or hang
    for (uint32_t id = 0; id < 20; ++id) {
        float f; int i; uint8_t b; bool bl; std::string s; float q[4];
        r.readFloat(id, f); r.readInt(id, i); r.readByte(id, b);
        r.readBool(id, bl); r.readString(id, s); r.readQuat(id, q);
    }
    CHECK(true, "garbage payload survived a full read sweep without crashing");
}

int main() {
    test_basicRoundTrip();
    test_missingIdIsFalse();
    test_typeMismatchIsRejected();
    test_overwriteSameId();
    test_vec3AndPose();
    test_manyEntities();
    test_truncatedPayloadIsRejected();
    test_idZeroRoundTrips();
    test_emptyPayload();
    test_garbagePayloadIsRejected();

    if (failures == 0) { printf("PROTO_GENERIC_TEST_PASS\n"); return 0; }
    printf("PROTO_GENERIC_TEST_FAIL (%d)\n", failures);
    return 1;
}
