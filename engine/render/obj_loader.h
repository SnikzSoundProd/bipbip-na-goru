#pragma once
// bipbip asset pipeline (scaffold): OBJ mesh loader.
// Parses Wavefront .obj (positions / uvs / normals / faces) into the engine
// Vertex layout used by Mesh. This is infrastructure only — the game's current
// box/capsule geometry is NOT replaced by this; meshes are loaded on demand.
#include <string>
#include <vector>
#include <cstdint>

namespace bip {

struct Vertex; // from render/mesh.h

// Load an OBJ file and append triangles to the given vertex/index buffers.
// Returns false if the file cannot be opened or has no faces.
// `centerAndScale` normalizes the model to roughly unit size around the origin
// so authored assets drop in at a sane scale.
bool loadObj(const std::string& path, std::vector<Vertex>& outVerts,
             std::vector<uint32_t>& outIndices, bool centerAndScale = true);

} // namespace bip
