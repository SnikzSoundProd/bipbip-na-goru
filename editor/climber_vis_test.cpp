// Visibility sanity: how many body parts does a climber actually produce?
// If this returns 0, the player/buddy render as nothing in the editor.
#include "physics/verlet.h"
#include "world/heightfield.h"
#include "game/player/climber.h"
#include <cstdio>
#include <cmath>

using namespace bip;

int main() {
    HeightField hf; hf.generate(1337, 220.f, 256);
    VerletWorld phys; phys.init(&hf);

    Climber c;
    Vec3 spawn{0.f, hf.heightAt(0.f, 118.f) + 1.2f, 118.f};
    c.init(&phys, &hf, spawn);
    printf("spawn = (%.2f, %.2f, %.2f)\n", (double)spawn.x, (double)spawn.y, (double)spawn.z);

    Climber::PartBox parts[64];
    int n = c.collectParts(parts);
    printf("collectParts -> %d\n", n);
    if (n <= 0) { printf("FAIL: no body parts rendered\n"); return 1; }

    // bounding box of the rendered body, to see how big it is on screen
    float minx=1e9f,miny=1e9f,minz=1e9f,maxx=-1e9f,maxy=-1e9f,maxz=-1e9f;
    for (int i = 0; i < n; ++i) {
        const auto& p = parts[i];
        printf("  part %d: center=(%.2f,%.2f,%.2f) half=(%.2f,%.2f,%.2f) color=(%.2f,%.2f,%.2f)\n",
               i, (double)p.center.x,(double)p.center.y,(double)p.center.z,
               (double)p.half.x,(double)p.half.y,(double)p.half.z,
               (double)p.color.x,(double)p.color.y,(double)p.color.z);
        minx=std::fmin(minx,p.center.x-p.half.x); maxx=std::fmax(maxx,p.center.x+p.half.x);
        miny=std::fmin(miny,p.center.y-p.half.y); maxy=std::fmax(maxy,p.center.y+p.half.y);
        minz=std::fmin(minz,p.center.z-p.half.z); maxz=std::fmax(maxz,p.center.z+p.half.z);
    }
    printf("body size: %.2f x %.2f x %.2f\n",
           (double)(maxx-minx),(double)(maxy-miny),(double)(maxz-minz));

    // step a bit so limbs settle, then re-check
    for (int i = 0; i < 60; ++i) { phys.step(1.f/60.f); c.simulate(1.f/60.f); }
    int n2 = c.collectParts(parts);
    printf("after 60 steps: collectParts -> %d\n", n2);
    Vec3 pp = c.pelvisPos();
    printf("pelvis after = (%.2f, %.2f, %.2f)\n", (double)pp.x,(double)pp.y,(double)pp.z);

    printf("CLIMBER_VIS_TEST_PASS\n");
    return 0;
}
