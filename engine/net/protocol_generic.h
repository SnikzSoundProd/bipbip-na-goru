#pragma once
// bipbip net: GENERIC wire payload format.
//
// STEP 4 of generalising the engine. The fixed structs in protocol.h are
// specific to the climbing game: PlayerSnapshot is 611 bytes containing a 13x3
// ragdoll pose, 14 crates and a stamina float. A second game with a different
// entity count and different per-entity state cannot use them.
//
// This layer is a small type-tagged, id-addressed record format on top of the
// same transport. The game decides what to write under which id, so:
//   - no fixed size, no static_assert of a magic number
//   - entities that do not exist this frame simply are not written
//   - adding a field does not change any other game's format
//
// Layout (all little-endian, no padding):
//   [u32 magic][u32 baseSeq][u32 recordCount]
//   exactly recordCount records:
//     [u32 id][u8 type][payload]
//
// Ids 0..0xFFFFFFFF are all valid. The list is delimited by an explicit count
// in the header rather than a magic id, so an entity is not forced to avoid
// id 0 (which every entity system wants to hand out first).
//
// The reader is defensive by construction: every read is bounds-checked against
// the buffer length, an unknown type or a truncated record makes the read return
// false, and it never reads past the end even on hostile input.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace bip::net {

constexpr uint32_t kGenericMagic = 0x42495047;  // 'BIPG'

enum class FieldType : uint8_t {
    Float  = 1,
    Int    = 2,
    Byte   = 3,
    Bool   = 4,
    Vec3   = 5,
    Quat   = 6,
    String = 7,
    Pose   = 8,   // N x vec3, N encoded in the payload
};

// Every scalar is read back with memcpy, never by casting the byte buffer to
// float*/uint32_t*: the payload offsets are arbitrary, so a cast would be both
// a strict-aliasing violation and a potential unaligned load that the compiler
// is free to miscompile at -O2.
namespace detail {
inline float  loadF(const uint8_t* p) { float v;  std::memcpy(&v, p, 4); return v; }
inline uint32_t loadU(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
}

// ---------------------------------------------------------------- writer
class SnapshotWriter {
public:
    void begin(uint32_t baseSeq) {
        buf_.clear();
        put32(kGenericMagic);
        put32(baseSeq);
        put32(0);          // recordCount placeholder, filled in finish()
        count_ = 0;
    }

    void writeFloat(uint32_t id, float v)  { rec(id, FieldType::Float);  put32f(v); }
    // An int is stored as 4 real int bytes, NOT as a float: readInt() looks the
    // record up by type, so encoding it as a float would make it unreachable.
    void writeInt  (uint32_t id, int32_t v){ rec(id, FieldType::Int);    put32((uint32_t)v); }
    void writeByte (uint32_t id, uint8_t v) { rec(id, FieldType::Byte);   buf_.push_back(v); }
    void writeBool (uint32_t id, bool v)   { rec(id, FieldType::Bool);   buf_.push_back(v ? 1 : 0); }
    void writeQuat (uint32_t id, float x, float y, float z, float w) {
        rec(id, FieldType::Quat);
        put32f(x); put32f(y); put32f(z); put32f(w);
    }
    void writeVec3 (uint32_t id, float x, float y, float z) {
        rec(id, FieldType::Vec3);
        put32f(x); put32f(y); put32f(z);
    }
    void writeString(uint32_t id, const std::string& s) {
        rec(id, FieldType::String);
        put32((uint32_t)s.size());
        buf_.insert(buf_.end(), s.begin(), s.end());
    }
    // N x vec3 (e.g. a 13-particle ragdoll pose)
    template <typename F>
    void writePose(uint32_t id, uint32_t n, F&& fill) {
        rec(id, FieldType::Pose);
        put32(n);
        float p[3];
        for (uint32_t i = 0; i < n; ++i) {
            p[0] = p[1] = p[2] = 0.f;
            fill((int)i, p);
            put32f(p[0]); put32f(p[1]); put32f(p[2]);
        }
    }

    // Backfills the record count and returns the payload (move).
    std::vector<uint8_t> finish() {
        if (buf_.size() >= 12) {
            uint32_t c = (uint32_t)count_;
            buf_[8]  = (uint8_t)c;         buf_[9]  = (uint8_t)(c >> 8);
            buf_[10] = (uint8_t)(c >> 16); buf_[11] = (uint8_t)(c >> 24);
        }
        return std::move(buf_);
    }
    size_t recordCount() const { return count_; }

private:
    // id 0 is a perfectly valid id: the list is delimited by the header count,
    // not by a magic terminator, so nothing here can silently swallow a record.
    void rec(uint32_t id, FieldType t) { put32(id); buf_.push_back((uint8_t)t); ++count_; }
    void put32(uint32_t v) { buf_.push_back((uint8_t)(v)); buf_.push_back((uint8_t)(v >> 8));
                             buf_.push_back((uint8_t)(v >> 16)); buf_.push_back((uint8_t)(v >> 24)); }
    void put32f(float f) { uint32_t v; std::memcpy(&v, &f, 4); put32(v); }

    std::vector<uint8_t> buf_;
    size_t count_ = 0;
};

// ---------------------------------------------------------------- reader
class SnapshotReader {
public:
    SnapshotReader(const uint8_t* data, size_t len, uint32_t baseSeq)
        : d_(data), n_(len) {
        if (!data || len < 12) { good_ = false; return; }
        if (rd32() != kGenericMagic) { good_ = false; return; }
        uint32_t seq = rd32();
        (void)seq;
        uint32_t declared = rd32();
        // A record is at least 5 bytes, so a count larger than the remaining
        // buffer can only be corruption. Clamp instead of trusting it.
        if (declared > (n_ - 12) / 5) { declared = (n_ - 12) / 5; truncated_ = true; }
        baseSeq_ = baseSeq;
        good_ = true;
        index(declared);
    }

    bool ok() const { return good_; }
    bool truncated() const { return truncated_; }   // header valid, body cut short
    uint32_t baseSeq() const { return baseSeq_; }
    size_t   count() const { return map_.size(); }

    // All reads return false when the id is absent, the type differs, or the
    // record is truncated. They never read past the end.
    bool readFloat(uint32_t id, float& out) const {
        const Rec* r = find(id, FieldType::Float); if (!r) return false;
        out = detail::loadF(d_ + r->off); return true;
    }
    bool readInt(uint32_t id, int32_t& out) const {
        const Rec* r = find(id, FieldType::Int); if (!r) return false;
        if (r->off + 4 > n_) return false;
        out = (int32_t)detail::loadU(d_ + r->off); return true;
    }
    bool readByte(uint32_t id, uint8_t& out) const {
        const Rec* r = find(id, FieldType::Byte); if (!r) return false;
        if (r->off + 1 > n_) return false;
        out = d_[r->off]; return true;
    }
    bool readBool(uint32_t id, bool& out) const {
        // Look up by Bool, not via readByte(): the record's type tag is Bool,
        // so delegating to readByte would never find it.
        const Rec* r = find(id, FieldType::Bool); if (!r) return false;
        if (r->off + 1 > n_) return false;
        out = (d_[r->off] != 0); return true;
    }
    bool readVec3(uint32_t id, float& x, float& y, float& z) const {
        const Rec* r = find(id, FieldType::Vec3); if (!r) return false;
        if (r->off + 12 > n_) return false;
        std::memcpy(&x, d_ + r->off, 4);
        std::memcpy(&y, d_ + r->off + 4, 4);
        std::memcpy(&z, d_ + r->off + 8, 4);
        return true;
    }
    bool readQuat(uint32_t id, float q[4]) const {
        const Rec* r = find(id, FieldType::Quat); if (!r) return false;
        if (r->off + 16 > n_) return false;
        std::memcpy(q, d_ + r->off, 16);
        return true;
    }
    bool readString(uint32_t id, std::string& out) const {
        const Rec* r = find(id, FieldType::String); if (!r) return false;
        if (r->off + 4 > n_) return false;
        uint32_t len = detail::loadU(d_ + r->off);
        if (r->off + 4 + (size_t)len > n_) return false;   // truncated
        out.assign((const char*)(d_ + r->off + 4), len);
        return true;
    }
    bool readPose(uint32_t id, float* out, uint32_t maxParticles) const {
        const Rec* r = find(id, FieldType::Pose); if (!r) return false;
        if (r->off + 4 > n_) return false;
        uint32_t cnt = detail::loadU(d_ + r->off);
        if (cnt > maxParticles) return false;              // refuse oversized
        if (r->off + 4 + (size_t)cnt * 12 > n_) return false;
        std::memcpy(out, d_ + r->off + 4, (size_t)cnt * 12);
        return true;
    }

private:
    struct Rec { uint8_t type; size_t off; };

    const Rec* find(uint32_t id, FieldType t) const {
        for (const auto& kv : map_)
            if (kv.first == id) return (kv.second.type == (uint8_t)t) ? &kv.second : nullptr;
        return nullptr;
    }

    uint32_t rd32() {
        if (p_ + 4 > n_) return 0;
        uint32_t v = detail::loadU(d_ + p_);
        p_ += 4; return v;
    }

    // Single pass: record where each id's payload starts. Records with an
    // unknown type or a bad length are skipped, never dereferenced.
    void index(uint32_t declared) {
        for (uint32_t k = 0; k < declared; ++k) {
            if (p_ + 4 > n_) { truncated_ = true; return; }
            uint32_t id = rd32();
            if (p_ + 1 > n_) { truncated_ = true; return; }
            uint8_t type = d_[p_++];
            size_t payload = p_;
            size_t skip = 0;
            switch (type) {
                case (uint8_t)FieldType::Float:
                case (uint8_t)FieldType::Int:   skip = 4; break;
                case (uint8_t)FieldType::Byte:
                case (uint8_t)FieldType::Bool:  skip = 1; break;
                case (uint8_t)FieldType::Vec3:  skip = 12; break;
                case (uint8_t)FieldType::Quat:  skip = 16; break;
                case (uint8_t)FieldType::String: {
                    if (p_ + 4 > n_) { truncated_ = true; return; }
                    uint32_t len = detail::loadU(d_ + p_);
                    skip = 4 + (size_t)len;
                    break;
                }
                case (uint8_t)FieldType::Pose: {
                    if (p_ + 4 > n_) { truncated_ = true; return; }
                    uint32_t cnt = detail::loadU(d_ + p_);
                    skip = 4 + (size_t)cnt * 12;
                    break;
                }
                default:
                    // Unknown type: we cannot know its length, so stop indexing.
                    // Fields written before it stay readable.
                    return;
            }
            if (payload + skip > n_) { truncated_ = true; return; }  // stop, header stays valid
            bool replaced = false;
            for (auto& kv : map_) {
                if (kv.first != id) continue;
                replaced = true;
                // Same type: last write wins. Different type: the first record
                // is authoritative, so a later mismatched write is ignored
                // rather than clobbering a value the reader may already hold.
                if (kv.second.type == type) kv.second.off = payload;
                break;
            }
            if (!replaced) map_.emplace_back(id, Rec{ type, payload });
            p_ = payload + skip;
        }
    }

    const uint8_t* d_ = nullptr;
    size_t n_ = 0, p_ = 0;
    bool good_ = false;
    bool truncated_ = false;   // header was valid, record stream was cut short
    uint32_t baseSeq_ = 0;
    std::vector<std::pair<uint32_t, Rec>> map_;
};

} // namespace bip::net
