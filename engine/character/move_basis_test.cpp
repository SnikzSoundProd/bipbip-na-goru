// Movement-basis test: WASD must be camera-relative, and A/D must not cancel.
//
// Found by three real bugs in psycho, all in one line of hand-rolled input:
//
//   1. A/D were inverted relative to the climbing game. Game 1 builds a proper
//      orthonormal basis: f = (sin y, 0, cos y), r = (cos y, 0, -sin y), then
//      move = ±f ±r. psycho wrote `sy*fwd - cy*right` for x, which is not the
//      right vector at all — it is the wrong basis, so strafing steered the
//      player backwards.
//   2. `back` and `left` were read from the keyboard but never used in the
//      expression, so S and A did nothing while D produced a wrong direction.
//   3. The character never received setFacingYaw(), so the body kept its spawn
//      facing and the mouse only moved the camera behind it.
//
// This test re-derives the basis the same way the game does and asserts the
// four cardinal moves point the four cardinal directions.
//
// Offline: pure math, no D3D, no window.
#include "core/math.h"
#include "character/move_basis.h"
#include <cstdio>
#include <cmath>
#include <string>
#include <windows.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++failures; } } while (0)

using namespace bip;

// The basis is asserted against the ENGINE's own implementation, not a copy of
// it — a duplicated formula in the test would let both drift together.
static Vec3 moveFor(float yaw, float fwd, float back, float left, float right) {
    return moveBasisFor(yaw, fwd, back, left, right);
}

static std::string readFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return std::string();
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

// psycho's loop must build movement through the engine's shared basis and must
// tell the rig which way to face. The original bug had `left`/`back` bound but
// never applied, so the compiler never complained — only reading the source
// catches it. What matters is that all four keys REACH the helper, not that
// the four `if`s are written out locally.
static void checkSource() {
    const std::string src = readFile("C:/Users/apex/bipbip/psycho/main.cpp");
    if (src.empty()) {
        printf("FAIL: could not read psycho/main.cpp\n");
        ++failures;
        return;
    }
    CHECK(src.find("#include \"character/move_basis.h\"") != std::string::npos,
          "psycho must include the shared movement basis header");
    CHECK(src.find("moveBasisFor(") != std::string::npos,
          "psycho must compute movement through moveBasisFor()");
    // every key must be passed into it — the bug was binding them and dropping them
    const std::string call = "moveBasisFor(cam.yaw, fwd, back, left, right)";
    CHECK(src.find(call) != std::string::npos,
          "psycho must pass all four movement keys into moveBasisFor()");
    CHECK(src.find("setFacingYaw") != std::string::npos,
          "psycho must call setFacingYaw so the body turns with the camera");
    // and it must not hand-roll a basis any more
    CHECK(src.find("sy * fwd") == std::string::npos &&
          src.find("cy * right") == std::string::npos,
          "psycho must not hand-roll an inline movement basis");
}

static void checkDir(float yaw, const char* key, float fwd, float back,
                     float left, float right, float ex, float ez, const char* label) {
    const Vec3 m = moveFor(yaw, fwd, back, left, right);
    const bool ok = std::fabs(m.x - ex) < 1e-5f && std::fabs(m.z - ez) < 1e-5f;
    if (!ok) {
        printf("FAIL: %s at yaw=%.2f -> got (%.3f, %.3f) want (%.3f, %.3f)\n",
               key, yaw, m.x, m.z, ex, ez);
        ++failures;
    }
}

int main() {
    // At yaw = 0 the camera looks toward +Z, so W -> +Z, S -> -Z,
    // A -> -X, D -> +X. This is the case that the broken expression got wrong.
    checkDir(0.f, "W", 1, 0, 0, 0,  0.f,  1.f, "W must go forward at yaw 0");
    checkDir(0.f, "S", 0, 1, 0, 0,  0.f, -1.f, "S must go backward at yaw 0");
    checkDir(0.f, "A", 0, 0, 1, 0, -1.f,  0.f, "A must strafe left at yaw 0");
    checkDir(0.f, "D", 0, 0, 0, 1,  1.f,  0.f, "D must strafe right at yaw 0");

    // Turning 90 degrees must rotate the whole basis, not just forward.
    const float h = 0.70710678f;
    checkDir(1.5707963f, "W", 1, 0, 0, 0,  1.f,  0.f, "W must rotate with yaw");
    checkDir(1.5707963f, "D", 0, 0, 0, 1,  0.f, -1.f, "D must rotate with yaw");

    // Opposite keys cancel to exactly zero, and every result stays unit length.
    const Vec3 c = moveFor(0.7f, 1, 1, 0, 0);
    CHECK(std::fabs(c.x) < 1e-6f && std::fabs(c.z) < 1e-6f, "W+S must cancel exactly");

    for (int i = 0; i < 8; ++i) {
        const float yaw = (float)i * 0.9f;
        const Vec3 m = moveFor(yaw, 1, 0, 0, 1);          // W+D diagonal
        CHECK(std::fabs(length(m) - 1.f) < 1e-5f, "diagonal move must stay unit length");
    }

    // The maths above only proves the convention. It does not prove psycho uses
    // it, and the original bug was a hand-rolled expression in psycho/main.cpp
    // that ignored back/left entirely. So assert the shipped source does not
    // regress to an inline basis.
    checkSource();

    if (failures == 0) { printf("MOVE_BASIS_TEST_PASS\n"); return 0; }
    printf("MOVE_BASIS_TEST_FAIL (%d)\n", failures);
    return 1;
}
