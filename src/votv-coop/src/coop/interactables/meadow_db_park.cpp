// coop/interactables/meadow_db_park.cpp -- see coop/interactables/meadow_db_park.h.

#include "coop/interactables/meadow_db_park.h"

#include "ue_wrap/core/log.h"

#include <deque>
#include <vector>

namespace coop::meadow_db_park {
namespace {

constexpr auto kHoldTime = std::chrono::seconds(20);
// About 4 KB at the bound; legitimate entries are a handful.
constexpr size_t kHeldCap = 256;

struct Tomb { uint64_t hash; Clock::time_point until; };
std::vector<Tomb> g_tombs;

constexpr size_t kParkCap = 256;
struct Line { Kind kind; std::vector<uint8_t> blob; uint64_t hash; uint8_t senderSlot; };
std::deque<Line> g_lines;

}  // namespace

bool HoldDelete(uint64_t hash, uint8_t senderSlot, Clock::time_point now) {
    if (g_tombs.size() >= kHeldCap) {
        UE_LOGW("meadow_db: tombstone REFUSED (hash=%016llx, from slot %u) -- at the %zu-entry bound",
                static_cast<unsigned long long>(hash), static_cast<unsigned>(senderSlot), kHeldCap);
        return false;
    }
    g_tombs.push_back({hash, now + kHoldTime});
    return true;
}

bool ConsumeDelete(uint64_t hash) {
    for (auto it = g_tombs.begin(); it != g_tombs.end(); ++it) {
        if (it->hash == hash) {
            g_tombs.erase(it);
            return true;
        }
    }
    return false;
}

void ExpireDeletes(Clock::time_point now) {
    for (auto it = g_tombs.begin(); it != g_tombs.end();) {
        if (now >= it->until) {
            UE_LOGW("meadow_db: delete for hash %016llx expired unmatched", static_cast<unsigned long long>(it->hash));
            it = g_tombs.erase(it);
        } else {
            ++it;
        }
    }
}

void RetryDeletes(ApplyDeleteFn apply) {
    for (auto it = g_tombs.begin(); it != g_tombs.end();) {
        if (apply(it->hash)) it = g_tombs.erase(it);
        else ++it;
    }
}

size_t HeldDeletes() {
    return g_tombs.size();
}

bool Park(Kind kind, std::vector<uint8_t>&& blob, uint64_t hash, uint8_t senderSlot) {
    if (g_lines.size() >= kParkCap) {
        UE_LOGW("meadow_db: a line from slot %u REFUSED while the database is away -- %zu already wait",
                static_cast<unsigned>(senderSlot), kParkCap);
        return false;
    }
    g_lines.push_back({kind, std::move(blob), hash, senderSlot});
    return true;
}

size_t Drain(ReplayFn replay) {
    size_t n = 0;
    while (!g_lines.empty()) {
        const Line& l = g_lines.front();
        if (!replay(l.kind, l.blob, l.hash, l.senderSlot)) break;
        g_lines.pop_front();
        ++n;
    }
    return n;
}

size_t Parked() {
    return g_lines.size();
}

bool ParkedAppend(uint64_t hash) {
    for (const Line& l : g_lines)
        if (l.kind == Kind::Append && l.hash == hash) return true;
    return false;
}

void Clear() {
    g_tombs.clear();
    g_lines.clear();
}

}  // namespace coop::meadow_db_park
