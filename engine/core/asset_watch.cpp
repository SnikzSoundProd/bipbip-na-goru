#include "core/asset_watch.h"
#include <windows.h>
#include <sys/stat.h>
#include <cstdio>

namespace bip {

long long AssetWatcher::mtimeOf(const std::string& path) {
    // _stat / _stat64 on MinGW only expose WHOLE SECONDS (st_mtime is time_t
    // or __time64_t; there is no st_mtim), so an edit saved within the same
    // second looked unchanged and hot-reload silently missed it. Mix the
    // second-resolution mtime with a content fingerprint (FNV-1a over the
    // bytes) so any real edit is detected regardless of timestamp granularity.
    struct _stat64 st{};
    if (_stat64(path.c_str(), &st) != 0) return -1;

    unsigned long long h = 1469598103934665603ULL;          // FNV offset basis
    if (FILE* f = fopen(path.c_str(), "rb")) {
        unsigned char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            for (size_t i = 0; i < n; ++i) { h ^= buf[i]; h *= 1099511628211ULL; }
        }
        fclose(f);
    }
    // keep it positive and non-zero for an empty-but-existing file
    long long v = ((long long)st.st_mtime << 24) ^ (long long)(h & 0xFFFFFF);
    return v;
}

void AssetWatcher::watch(const std::string& path) {
    if (mtimes_.find(path) == mtimes_.end())
        mtimes_[path] = mtimeOf(path); // seed baseline; reports change only on next edit
}

std::vector<std::string> AssetWatcher::poll() {
    std::vector<std::string> changed;
    for (auto& kv : mtimes_) {
        long long now = mtimeOf(kv.first);
        if (now != kv.second) {
            kv.second = now;
            changed.push_back(kv.first);
        }
    }
    return changed;
}

} // namespace bip
