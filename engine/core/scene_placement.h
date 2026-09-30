#pragma once
// bipbip scene: how AUTHORED positions become WORLD positions.
//
// A scene is authored by hand, so pos.y means "height above the ground". It
// cannot mean an absolute world height, because the world is a procedural
// mountain whose ground near the origin sits at y ≈ 48 — a box authored at
// y = 2 would be 46 metres underground, invisible and non-colliding.
//
// This is not hypothetical: psycho shipped exactly that bug for one commit. All
// six of its boxes were buried under the terrain, and only the character (which
// the terrain is solid under) kept moving, so it looked like a working game.
//
// The climbing game never hit this because it generates its crates in code with
// hf.heightAt(x, z) + offset instead of round-tripping a scene through physics.
// psycho was the first consumer of a scene -> physics path, and therefore the
// first to need this rule stated in one place.
//
// Rule, applied uniformly:
//   world.y = heightAt(world.x, world.z) + authored.y
// A box authored at y = 0 is therefore RESOLVED so that its BOTTOM face rests
// on the ground: pass pos.y + half.y as the centre.

#include "core/math.h"
#include "core/scene.h"
#include "world/heightfield.h"

namespace bip {

// Authored position -> world position for a plain entity.
inline Vec3 authoredToWorld(const HeightField& hf, const Entity& e) {
    return Vec3{ e.pos[0], hf.heightAt(e.pos[0], e.pos[2]) + e.pos[1], e.pos[2] };
}

// Same, for a box authored to REST on the ground: the centre is raised by the
// box's own half-height so the bottom face touches the terrain.
inline Vec3 authoredBoxToWorld(const HeightField& hf, const Entity& e) {
    const float halfY = e.box ? e.box->half[1] : 0.f;
    return Vec3{ e.pos[0], hf.heightAt(e.pos[0], e.pos[2]) + e.pos[1] + halfY, e.pos[2] };
}

// Spawn point: the same rule, and the offset is added on top of the character's
// own pelvis height, so callers should pass 0 unless they want a hop.
inline Vec3 authoredSpawnToWorld(const HeightField& hf, const Scene& s) {
    return Vec3{ s.spawn[0], hf.heightAt(s.spawn[0], s.spawn[2]) + s.spawn[1], s.spawn[2] };
}

} // namespace bip
