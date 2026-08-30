#include "core/asset_watch.h"
#include <windows.h>
#include <sys/stat.h>

namespace bip {

long long AssetWatcher::mtimeOf(const std::string& path) {
    struct _stat st{};
    if (_stat(path.c_str(), &st) != 0) return -1;
    return (long long)st.st_mtime;
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
