#pragma once
// bipbip asset pipeline (scaffold): lightweight file watcher for hot-reload.
// Polls mtime on a set of files and reports which ones changed since last check.
// Intentionally simple — no OS-specific change notifications yet.
#include <string>
#include <vector>
#include <unordered_map>

namespace bip {

class AssetWatcher {
public:
    // Register a file path to watch. Safe to call repeatedly.
    void watch(const std::string& path);

    // Call once per frame. Returns the list of watched paths whose mtime
    // changed (or that were created) since the last poll.
    std::vector<std::string> poll();

    // Forget all watched paths.
    void clear() { mtimes_.clear(); }

private:
    static long long mtimeOf(const std::string& path);

    std::unordered_map<std::string, long long> mtimes_;
};

} // namespace bip
