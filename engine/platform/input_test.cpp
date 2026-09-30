// Input-contract test: every frame loop must reset per-frame input deltas.
//
// Found by a real bug: psycho spun its camera "like crazy" and the pitch was
// stuck at its clamp. Cause: InputState accumulates mouseDX/mouseDY from RAWINPUT
// during pumpMessages, and only endFrame() clears them. psycho's loop never
// called it, so a single 5px mouse move kept adding 5 to mouseDX EVERY frame
// forever. The first game and the editor both called endFrame(); psycho did not.
//
// Unit-testing InputState itself would not catch this — the class behaves
// correctly. The defect is a missing call in a frame loop, so the test asserts
// the call is present in every shipped loop.
//
// Offline: reads source files, no D3D, no window.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++failures; } } while (0)

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

// Count calls in main() only: editor_main.cpp legitimately has other functions.
static int countInMain(const std::string& src, const char* needle) {
    size_t m = src.find("int main(");
    if (m == std::string::npos) return -1;
    const std::string body = src.substr(m);
    int n = 0;
    size_t p = 0;
    while ((p = body.find(needle, p)) != std::string::npos) { ++n; p += strlen(needle); }
    return n;
}

static void checkLoop(const char* rel, const char* label) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "C:/Users/apex/bipbip/%s", rel);
    const std::string src = readFile(path);
    if (src.empty()) {
        printf("FAIL: could not read %s\n", rel);
        ++failures;
        return;
    }
    const int n = countInMain(src, "input.endFrame()");
    CHECK(n > 0, label);
    if (n <= 0) {
        printf("       %s: main() never calls input.endFrame() — raw mouse deltas\n"
               "       accumulate every frame and the camera spins uncontrollably\n", rel);
    } else {
        printf("       %s: input.endFrame() x%d\n", rel, n);
    }
}

int main() {
    checkLoop("game/main.cpp",   "climbing game loop resets per-frame input");
    checkLoop("editor/editor_main.cpp", "editor loop resets per-frame input");
    checkLoop("psycho/main.cpp", "psycho loop resets per-frame input");

    if (failures == 0) { printf("INPUT_TEST_PASS\n"); return 0; }
    printf("INPUT_TEST_FAIL (%d)\n", failures);
    return 1;
}
